/*
 * Copyright © 2010 Codethink Limited
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the licence, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "config.h"

#define _XOPEN_SOURCE 600
#include "dconf-engine.h"

#include "../common/dconf-enums.h"
#include "../common/dconf-paths.h"
#include "../common/dconf-gvdb-utils.h"
#include "gvdb/gvdb-reader.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "dconf-engine-profile.h"

/* The engine has zero or more sources.
 *
 * If it has zero sources then things are very uninteresting.  Nothing
 * is writable, nothing will ever be written and reads will always
 * return NULL.
 *
 * There are two interesting cases when there is a non-zero number of
 * sources.  Writing only ever occurs to the first source, if at all.
 * Non-first sources are never writable.
 *
 * The first source may or may not be writable.  In the usual case the
 * first source is the one in the user's home directory and is writable,
 * but it may be that the profile was setup for read-only access to
 * system sources only.
 *
 * In the case that the first source is not writable (and therefore
 * there are no writable sources), is_writable() will always return
 * FALSE and no writes will ever be performed.
 *
 * It's possible to request changes in three ways:
 *
 *  - synchronous: the D-Bus message is immediately sent to the
 *    dconf service and we block until we receive the reply.  The change
 *    signal will follow soon thereafter (when we receive the signal on
 *    D-Bus).
 *
 *  - asynchronous: typical asynchronous operation: we send the request
 *    and return immediately, notifying using a callback when the
 *    request is completed (and the new value is in the database).  The
 *    change signal follows in the same way as with synchronous.
 *
 *  - fast: we record the value locally and signal the change, returning
 *    immediately, as if the value is already in the database (from the
 *    viewpoint of the local process).  We keep note of the new value
 *    locally until the service has confirmed that the write was
 *    successful.  If the write fails, we emit a change signal.  From
 *    the view of the program it looks like the value was successfully
 *    changed but then quickly changed back again by some external
 *    agent.
 *
 * In fast mode if we were to immediately put all requests "in flight",
 * then we could end up in a situation where the service is kept
 * (needlessly) busy rewriting the database over and over again after a
 * sequence of fast changes on the client side.
 *
 * To avoid the issue we limit the number of in-flight requests to one.
 * If a request is already in flight, subsequent changes are merged into
 * a single aggregated pending change to be submitted as the next write
 * after the in-flight request completes.
 *
 * NB: I tell a lie.  Async is not supported yet.
 *
 * Notes about threading:
 *
 * The engine is oblivious to threads and main contexts.
 *
 * What this means is that the engine has no interaction with GMainLoop
 * and will not schedule idles or anything of the sort.  All calls made
 * by the engine to the client library will be made in response to
 * incoming method calls, from the same thread as the incoming call.
 *
 * If dconf_engine_call_handle_reply() or
 * dconf_engine_handle_dbus_signal() are called from 'exotic' threads
 * (as will often be the case) then the resulting calls to
 * dconf_engine_change_notify() will come from the same thread.  That's
 * left for the client library to deal with.
 *
 * All that said, the engine is completely threadsafe.  The client
 * library can call any method from any thread at any time -- as long as
 * it is willing to deal with receiving the change notifies in those
 * threads.
 *
 * Thread-safety is implemented using three locks.
 *
 * The first lock (sources_lock) protects the sources.  Although the
 * sources are only ever read from, it is necessary to lock them because
 * it is not safe to read during a refresh (when the source is being
 * closed and reopened).  Accordingly, sources_lock need only be
 * acquired when accessing the parts of the sources that are subject to
 * change as a result of refreshes; the static parts (like bus type,
 * object path, etc) can be accessed without holding the lock.  The
 * 'sources' array itself (and 'n_sources') are set at construction and
 * never change after that.
 *
 * The second lock (queue_lock) protects the queue (represented with two
 * fields pending and in_flight) used to implement the "fast" writes
 * described above.
 *
 * The third lock (subscription_count_lock) protects the two hash tables
 * that are used to keep track of the number of subscriptions held by
 * the client library to each path.
 *
 * If sources_lock and queue_lock are held at the same time then then
 * sources_lock must have been acquired first.
 *
 * subscription_count_lock is never held at the same time as
 * sources_lock or queue_lock
 */

static GSList *dconf_engine_global_list;
static GMutex  dconf_engine_global_lock;

struct _DConfEngine
{
  gpointer            user_data;    /* Set at construct time */
  GDestroyNotify      free_func;
  gint                ref_count;

  GMutex              sources_lock;  /* This lock is for the sources (ie: refreshing) and state. */
  guint64             state;         /* Counter that changes every time a source is refreshed. */
  DConfEngineSource **sources;       /* Array never changes, but each source changes internally. */
  gint                n_sources;

  GMutex              queue_lock;    /* This lock is for pending, in_flight, queue_cond */
  GCond               queue_cond;    /* Signalled when there are neither in-flight nor pending changes. */
  DConfChangeset     *pending;       /* Yet to be sent on the wire. */
  DConfChangeset     *in_flight;     /* Already sent but awaiting response. */

  gchar              *last_handled;  /* reply tag from last item in in_flight */

  /**
   * establishing and active, are hash tables storing the number
   * of subscriptions to each path in the two possible states
   */
  /* This lock ensures that transactions involving subscription counts are atomic */
  GMutex              subscription_count_lock;
  /* active on the client side, but awaiting confirmation from the writer */
  GHashTable         *establishing;
  /* active on the client side, and with a D-Bus match rule established */
  GHashTable         *active;
};

/* When taking the sources lock we check if any of the databases have
 * had updates.
 *
 * Anything that is accessing the database (even only reading) needs to
 * be holding the lock (since refreshes could be happening in another
 * thread), so this makes sense.
 *
 * We could probably optimise this to avoid checking some databases in
 * certain cases (ie: we do not need to check the user's database when
 * we are only interested in checking writability) but this works well
 * enough for now and is less prone to errors.
 *
 * We could probably change to a reader/writer situation that is only
 * holding the write lock when actually making changes during a refresh
 * but the engine is probably only ever really in use by two threads at
 * a given time (main thread doing reads, DBus worker thread clearing
 * the queue) so it seems unlikely that lock contention will become an
 * issue.
 *
 * If it does, we can revisit this...
 */
static void
dconf_engine_acquire_sources (DConfEngine *engine)
{
  gint i;

  g_mutex_lock (&engine->sources_lock);

  for (i = 0; i < engine->n_sources; i++)
    if (dconf_engine_source_refresh (engine->sources[i]))
      engine->state++;
}

static void
dconf_engine_release_sources (DConfEngine *engine)
{
  g_mutex_unlock (&engine->sources_lock);
}

static void
dconf_engine_lock_queue (DConfEngine *engine)
{
  g_mutex_lock (&engine->queue_lock);
}

static void
dconf_engine_unlock_queue (DConfEngine *engine)
{
  g_mutex_unlock (&engine->queue_lock);
}

/**
 * Adds the count of subscriptions to @path in @from_table to the
 * corresponding count in @to_table, creating it if it did not exist.
 * Removes the count from @from_table.
 */
static void
dconf_engine_move_subscriptions (GHashTable  *from_counts,
                                 GHashTable  *to_counts,
                                 const gchar *path)
{
  guint from_count = GPOINTER_TO_UINT (g_hash_table_lookup (from_counts, path));
  guint old_to_count = GPOINTER_TO_UINT (g_hash_table_lookup (to_counts, path));
  // Detect overflows
  g_assert (old_to_count <= G_MAXUINT - from_count);
  guint new_to_count = old_to_count + from_count;
  if (from_count != 0)
    {
      g_hash_table_remove (from_counts, path);
      g_hash_table_replace (to_counts,
                            g_strdup (path),
                            GUINT_TO_POINTER (new_to_count));
    }
}

/**
 * Increments the reference count for the subscription to @path, or sets
 * it to 1 if it didn’t previously exist.
 * Returns the new reference count.
 */
static guint
dconf_engine_inc_subscriptions (GHashTable  *counts,
                                const gchar *path)
{
  guint old_count = GPOINTER_TO_UINT (g_hash_table_lookup (counts, path));
  // Detect overflows
  g_assert (old_count < G_MAXUINT);
  guint new_count = old_count + 1;
  g_hash_table_replace (counts, g_strdup (path), GUINT_TO_POINTER (new_count));
  return new_count;
}

/**
 * Decrements the reference count for the subscription to @path, or
 * removes it if the new value is 0. The count must exist and be greater
 * than 0.
 * Returns the new reference count, or 0 if it does not exist.
 */
static guint
dconf_engine_dec_subscriptions (GHashTable  *counts,
                                const gchar *path)
{
  guint old_count = GPOINTER_TO_UINT (g_hash_table_lookup (counts, path));
  g_assert (old_count > 0);
  guint new_count = old_count - 1;
  if (new_count == 0)
    g_hash_table_remove (counts, path);
  else
    g_hash_table_replace (counts, g_strdup (path), GUINT_TO_POINTER (new_count));
  return new_count;
}

/**
 * Returns the reference count for the subscription to @path, or 0 if it
 * does not exist.
 */
static guint
dconf_engine_count_subscriptions (GHashTable  *counts,
                                  const gchar *path)
{
  return GPOINTER_TO_UINT (g_hash_table_lookup (counts, path));
}

/**
 * Acquires the subscription counts lock, which must be held when
 * reading or writing to the subscription counts.
 */
static void
dconf_engine_lock_subscription_counts (DConfEngine *engine)
{
  g_mutex_lock (&engine->subscription_count_lock);
}

/**
 * Releases the subscription counts lock
 */
static void
dconf_engine_unlock_subscription_counts (DConfEngine *engine)
{
  g_mutex_unlock (&engine->subscription_count_lock);
}

DConfEngine *
dconf_engine_new (const gchar    *profile,
                  gpointer        user_data,
                  GDestroyNotify  free_func)
{
  DConfEngine *engine;

  engine = g_slice_new0 (DConfEngine);
  engine->user_data = user_data;
  engine->free_func = free_func;
  engine->ref_count = 1;

  g_mutex_init (&engine->sources_lock);
  g_mutex_init (&engine->queue_lock);
  g_cond_init (&engine->queue_cond);

  engine->sources = dconf_engine_profile_open (profile, &engine->n_sources);

  g_mutex_lock (&dconf_engine_global_lock);
  dconf_engine_global_list = g_slist_prepend (dconf_engine_global_list, engine);
  g_mutex_unlock (&dconf_engine_global_lock);

  g_mutex_init (&engine->subscription_count_lock);
  engine->establishing = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                NULL);
  engine->active = g_hash_table_new_full (g_str_hash,
                                          g_str_equal,
                                          g_free,
                                          NULL);

  return engine;
}

void
dconf_engine_unref (DConfEngine *engine)
{
  gint ref_count;

 again:
  ref_count = engine->ref_count;

  if (ref_count == 1)
    {
      gint i;

      /* We are about to drop the last reference, but there is a chance
       * that a signal may be happening at this very moment, causing the
       * engine to gain another reference (due to its position in the
       * global engine list).
       *
       * Acquiring the lock here means that either we will remove this
       * engine from the list first or we will notice the reference
       * count has increased (and skip the free).
       */
      g_mutex_lock (&dconf_engine_global_lock);
      if (engine->ref_count != 1)
        {
          g_mutex_unlock (&dconf_engine_global_lock);
          goto again;
        }
      dconf_engine_global_list = g_slist_remove (dconf_engine_global_list, engine);
      g_mutex_unlock (&dconf_engine_global_lock);

      g_mutex_clear (&engine->sources_lock);
      g_mutex_clear (&engine->queue_lock);
      g_cond_clear (&engine->queue_cond);

      g_free (engine->last_handled);

      g_clear_pointer (&engine->pending, dconf_changeset_unref);
      g_clear_pointer (&engine->in_flight, dconf_changeset_unref);

      for (i = 0; i < engine->n_sources; i++)
        dconf_engine_source_free (engine->sources[i]);

      g_free (engine->sources);

      g_hash_table_unref (engine->establishing);
      g_hash_table_unref (engine->active);

      g_mutex_clear (&engine->subscription_count_lock);

      if (engine->free_func)
        engine->free_func (engine->user_data);

      g_slice_free (DConfEngine, engine);
    }

  else if (!g_atomic_int_compare_and_exchange (&engine->ref_count, ref_count, ref_count - 1))
    goto again;
}

static DConfEngine *
dconf_engine_ref (DConfEngine *engine)
{
  g_atomic_int_inc (&engine->ref_count);

  return engine;
}

guint64
dconf_engine_get_state (DConfEngine *engine)
{
  guint64 state;

  dconf_engine_acquire_sources (engine);
  state = engine->state;
  dconf_engine_release_sources (engine);

  return state;
}

static gboolean
dconf_engine_is_writable_internal (DConfEngine *engine,
                                   const gchar *key)
{
  gint i;

  /* We must check several things:
   *
   *  - we have at least one source
   *
   *  - the first source is writable
   *
   *  - the key is not locked in a non-writable (ie: non-first) source
   */
  if (engine->n_sources == 0)
    return FALSE;

  if (engine->sources[0]->writable == FALSE)
    return FALSE;

  /* Ignore locks in the first source.
   *
   * Either it is writable and therefore ignoring locks is the right
   * thing to do, or it's non-writable and we caught that case above.
   */
  for (i = 1; i < engine->n_sources; i++)
    if (engine->sources[i]->locks && gvdb_table_has_value (engine->sources[i]->locks, key))
      return FALSE;

  return TRUE;
}

gboolean
dconf_engine_is_writable (DConfEngine *engine,
                          const gchar *key)
{
  gboolean writable;

  dconf_engine_acquire_sources (engine);
  writable = dconf_engine_is_writable_internal (engine, key);
  dconf_engine_release_sources (engine);

  return writable;
}

gchar **
dconf_engine_list_locks (DConfEngine *engine,
                         const gchar *path,
                         gint        *length)
{
  gchar **strv;

  if (dconf_is_dir (path, NULL))
    {
      GHashTable *set;

      set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

      dconf_engine_acquire_sources (engine);

      if (engine->n_sources > 0 && engine->sources[0]->writable)
        {
          gint i, j;

          for (i = 1; i < engine->n_sources; i++)
            {
              if (engine->sources[i]->locks)
                {
                  strv = gvdb_table_get_names (engine->sources[i]->locks, NULL);

                  for (j = 0; strv[j]; j++)
                    {
                      /* It is not currently possible to lock dirs, so we
                       * don't (yet) have to check the other direction.
                       */
                      if (g_str_has_prefix (strv[j], path))
                        g_hash_table_add (set, strv[j]);
                      else
                        g_free (strv[j]);
                    }

                  g_free (strv);
                }
            }
        }
      else
        g_hash_table_add (set, g_strdup (path));

      dconf_engine_release_sources (engine);

      strv = (gchar **) g_hash_table_get_keys_as_array (set, (guint *) length);
      g_hash_table_steal_all (set);
      g_hash_table_unref (set);
    }
  else
    {
      if (dconf_engine_is_writable (engine, path))
        {
          strv = g_new0 (gchar *, 0 + 1);
        }
      else
        {
          strv = g_new0 (gchar *, 1 + 1);
          strv[0] = g_strdup (path);
        }
    }

  return strv;
}

static gboolean
dconf_engine_find_key_in_queue (const GQueue  *queue,
                                const gchar   *key,
                                GVariant     **value)
{
  GList *node;

  /* Tail to head... */
  for (node = queue->tail; node; node = node->prev)
    if (dconf_changeset_get (node->data, key, value))
      return TRUE;

  return FALSE;
}

GVariant *
dconf_engine_read (DConfEngine    *engine,
                   DConfReadFlags  flags,
                   const GQueue   *read_through,
                   const gchar    *key)
{
  GVariant *value = NULL;
  gint lock_level = 0;
  gint i;

  dconf_engine_acquire_sources (engine);

  /* There are a number of situations that this function has to deal
   * with and they interact in unusual ways.  We attempt to write the
   * rules for all cases here:
   *
   * With respect to the steady-state condition with no locks:
   *
   *   This is the case where there are no changes queued, no
   *   read_through and no locks.
   *
   *   The value returned is the one from the lowest-index source that
   *   contains that value.
   *
   * With respect to locks:
   *
   *   If a lock is present (except in source #0 where it is ignored)
   *   then we will only return a value found in the source where the
   *   lock was present, or a higher-index source (following the normal
   *   rule that sources with lower indexes take priority).
   *
   *   This statement includes read_through and queued changes.  If a
   *   lock is found, we will ignore those.
   *
   * With respect to flags:
   *
   *   If DCONF_READ_USER_VALUE is given then we completely ignore all
   *   locks, returning the user value all the time, even if it is not
   *   visible (because of a lock).  This includes any pending value
   *   that is in the read_through or pending queues.
   *
   *   If DCONF_READ_DEFAULT_VALUE is given then we skip the writable
   *   database and the queues (including read_through, which is
   *   meaningless in this case) and skip directly to the non-writable
   *   databases.  This is defined as the value that the user would see
   *   if they were to have just done a reset for that key.
   *
   * With respect to read_through and queued changed:
   *
   *   We only consider read_through and queued changes in the event
   *   that we have a writable source.  This will possibly cause us to
   *   ignore read_through and will have no real effect on the queues
   *   (since they will be empty anyway if we have no writable source).
   *
   *   We only consider read_through and queued changes in the event
   *   that we have not found any locks.
   *
   *   If there is a non-NULL value found in read_through or the queued
   *   changes then we will return that value.
   *
   *   If there is a NULL value (ie: a reset) found in read_through or
   *   the queued changes then we will only ignore any value found in
   *   the first source (which must be writable, or else we would not
   *   have been considering read_through and the queues).  This is
   *   consistent with the fact that a reset will unset any value found
   *   in this source but will not affect values found in lower sources.
   *
   *   Put another way: if a non-writable source contains a value for a
   *   particular key then it is impossible for this function to return
   *   NULL.
   *
   * We implement the above rules as follows.  We have three state
   * tracking variables:
   *
   *   - lock_level: records if and where we found a lock
   *
   *   - found_key: records if we found the key in any queue
   *
   *   - value: records the value of the found key (NULL for resets)
   *
   * We take these steps:
   *
   *  1. check for lockdown.  If we find a lock then we prevent any
   *     other sources (including read_through and pending/in-flight)
   *     from affecting the value of the key.
   *
   *     We record the result of this in the lock_level variable.  Zero
   *     means that no locks were found.  Non-zero means that a lock was
   *     found in the source with the index given by the variable.
   *
   *  2. check the uncommitted changes in the read_through list as the
   *     highest priority.  This is only done if we have a writable
   *     source and no locks were found.
   *
   *     If we found an entry in the read_through then we set
   *     'found_key' to TRUE and set 'value' to the value that we found
   *     (which will be NULL in the case of finding a reset request).
   *
   *  3. check our pending and in-flight "fast" changes (in that order).
   *     This is only done if we have a writable source and no locks
   *     were found.  It is also only done if we did not find the key in
   *     the read_through.
   *
   *  4. check the first source, if there is one.
   *
   *     This is only done if 'found_key' is FALSE.  If 'found_key' is
   *     TRUE then it means that the first database was writable and we
   *     either found a value that will replace it (value != NULL) or
   *     found a pending reset (value == NULL) that will unset it.
   *
   *     We only actually do this step if we have a writable first
   *     source and no locks found, otherwise we just let step 5 do all
   *     the checking.
   *
   *  5. check the remaining sources.
   *
   *     We do this until we have value != NULL.  Even if found_key was
   *     TRUE, the reset that was requested will not have affected the
   *     lower-level databases.
   */

  /* Step 1.  Check for locks.
   *
   * Note: i > 0 (strictly).  Ignore locks for source #0.
   */
  if (~flags & DCONF_READ_USER_VALUE)
    for (i = engine->n_sources - 1; i > 0; i--)
      if (engine->sources[i]->locks && gvdb_table_has_value (engine->sources[i]->locks, key))
        {
          lock_level = i;
          break;
        }

  /* Only do steps 2 to 4 if we have no locks and we have a writable source. */
  if (!lock_level && engine->n_sources != 0 && engine->sources[0]->writable)
    {
      gboolean found_key = FALSE;

      /* If the user has requested the default value only, then ensure
       * that we "find" a NULL value here.  This is equivalent to the
       * user having reset the key, which is the definition of this
       * flag.
       */
      if (flags & DCONF_READ_DEFAULT_VALUE)
        found_key = TRUE;

      /* Step 2.  Check read_through. */
      if (!found_key && read_through)
        found_key = dconf_engine_find_key_in_queue (read_through, key, &value);

      /* Step 3.  Check queued changes if we didn't find it in read_through.
       *
       * NB: We may want to optimise this to avoid taking the lock in
       * the case that we know both queues are empty.
       */
      if (!found_key)
        {
          dconf_engine_lock_queue (engine);

          /* Check the pending first because those were submitted
           * more recently.
           */
          if (engine->pending != NULL)
            found_key = dconf_changeset_get (engine->pending, key, &value);

          if (!found_key && engine->in_flight != NULL)
            found_key = dconf_changeset_get (engine->in_flight, key, &value);

          dconf_engine_unlock_queue (engine);
        }

      /* Step 4.  Check the first source. */
      if (!found_key && engine->sources[0]->values)
        value = gvdb_table_get_value (engine->sources[0]->values, key);

      /* We already checked source #0 (or ignored it, as appropriate).
       *
       * Abuse the lock_level variable to get step 5 to skip this one.
       */
      lock_level = 1;
    }

  /* Step 5.  Check the remaining sources, until value != NULL. */
  if (~flags & DCONF_READ_USER_VALUE)
    for (i = lock_level; value == NULL && i < engine->n_sources; i++)
      {
        if (engine->sources[i]->values == NULL)
          continue;

        if ((value = gvdb_table_get_value (engine->sources[i]->values, key)))
          break;
      }

  dconf_engine_release_sources (engine);

  return value;
}

gchar **
dconf_engine_list (DConfEngine *engine,
                   const gchar *dir,
                   gint        *length)
{
  GHashTable *results;
  GHashTableIter iter;
  gchar **list;
  gint n_items;
  gpointer key;
  gint i;

  /* This function is unreliable in the presence of pending changes.
   * Here's why:
   *
   * Consider the case that we list("/a/") and a pending request has a
   * reset request recorded for "/a/b/c".  The question of if "b/"
   * should appear in the output rests on if "/a/b/d" also exists.
   *
   * Put another way: If "/a/b/c" is the only key in "/a/b/" then
   * resetting it would mean that "/a/b/" stops existing (and we should
   * not include it in the output).  If there are others keys then it
   * will continue to exist and we should include it.
   *
   * Instead of trying to sort this out, we just ignore the pending
   * requests and report what the on-disk file says.
   */

  results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  dconf_engine_acquire_sources (engine);

  for (i = 0; i < engine->n_sources; i++)
    {
      gchar **partial_list;
      gint j;

      if (engine->sources[i]->values == NULL)
        continue;

      partial_list = gvdb_table_list (engine->sources[i]->values, dir);

      if (partial_list != NULL)
        {
          for (j = 0; partial_list[j]; j++)
            /* Steal the keys from the list. */
            g_hash_table_add (results, partial_list[j]);

          /* Free only the list. */
          g_free (partial_list);
        }
    }

  dconf_engine_release_sources (engine);

  n_items = g_hash_table_size (results);
  list = g_new (gchar *, n_items + 1);

  i = 0;
  g_hash_table_iter_init (&iter, results);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      g_hash_table_iter_steal (&iter);
      list[i++] = key;
    }
  list[i] = NULL;
  g_assert_cmpint (i, ==, n_items);

  if (length)
    *length = n_items;

  g_hash_table_unref (results);

  return list;
}

static gboolean
dconf_engine_dir_has_writable_contents (DConfEngine *engine,
                                        const gchar *dir)
{
  DConfChangeset *database;
  GHashTable *current_state;

  /* Read the on disk state */
  if (engine->n_sources == 0 || !engine->sources[0]->writable)
    // If there are no writable sources, there won't be any pending writes either
    return FALSE;

  dconf_engine_acquire_sources (engine);
  database = dconf_gvdb_utils_changeset_from_table (engine->sources[0]->values);
  dconf_engine_release_sources (engine);

  /* Apply pending and in_flight changes to the on disk state */
  dconf_engine_lock_queue (engine);

  if (engine->in_flight != NULL)
    dconf_changeset_change (database, engine->in_flight);

  if (engine->pending != NULL)
    {
      /**
       * We don't want to seal the pending changeset because it may still be changed,
       * and sealing the changeset would be a side effect of passing engine->pending
       * directly into dconf_changeset_change.
       */
      DConfChangeset *changes = dconf_changeset_filter_changes (database, engine->pending);
      if (changes != NULL)
        {
          dconf_changeset_change (database, changes);
          dconf_changeset_unref (changes);
        }
    }

  dconf_engine_unlock_queue (engine);

  /* Check if there are writable contents at the given directory in the current state */
  current_state = dconf_gvdb_utils_table_from_changeset (database);
  gboolean result = g_hash_table_contains (current_state, dir);

  g_hash_table_unref (current_state);
  dconf_changeset_unref (database);
  return result;
}

typedef void (* DConfEngineCallHandleCallback) (DConfEngine  *engine,
                                                gpointer      handle,
                                                GVariant     *parameter,
                                                const GError *error);

struct _DConfEngineCallHandle
{
  DConfEngine                   *engine;
  DConfEngineCallHandleCallback  callback;
  const GVariantType            *expected_reply;
};

static gpointer
dconf_engine_call_handle_new (DConfEngine                   *engine,
                              DConfEngineCallHandleCallback  callback,
                              const GVariantType            *expected_reply,
                              gsize                          size)
{
  DConfEngineCallHandle *handle;

  g_assert (engine != NULL);
  g_assert (callback != NULL);
  g_assert (size >= sizeof (DConfEngineCallHandle));

  handle = g_malloc0 (size);
  handle->engine = dconf_engine_ref (engine);
  handle->callback = callback;
  handle->expected_reply = expected_reply;

  return handle;
}

const GVariantType *
dconf_engine_call_handle_get_expected_type (DConfEngineCallHandle *handle)
{
  if (handle)
    return handle->expected_reply;
  else
    return NULL;
}

void
dconf_engine_call_handle_reply (DConfEngineCallHandle *handle,
                                GVariant              *parameter,
                                const GError          *error)
{
  if (handle == NULL)
    return;

  (* handle->callback) (handle->engine, handle, parameter, error);
}

static void
dconf_engine_call_handle_free (DConfEngineCallHandle *handle)
{
  dconf_engine_unref (handle->engine);
  g_free (handle);
}

/* returns floating */
static GVariant *
dconf_engine_make_match_rule (DConfEngineSource *source,
                              const gchar       *path)
{
  GVariant *params;
  gchar *rule;

  rule = g_strdup_printf ("type='signal',"
                          "interface='ca.desrt.dconf.Writer',"
                          "path='%s',"
                          "arg0path='%s'",
                          source->object_path,
                          path);

  params = g_variant_new ("(s)", rule);

  g_free (rule);

  return params;
}

typedef struct
{
  DConfEngineCallHandle handle;

  guint64  state;
  gint     pending;
  gchar   *path;
} OutstandingWatch;

static void
dconf_engine_watch_established (DConfEngine  *engine,
                                gpointer      handle,
                                GVariant     *reply,
                                const GError *error)
{
  OutstandingWatch *ow = handle;

  /* ignore errors */

  if (--ow->pending)
    /* more on the way... */
    return;

  if (ow->state != dconf_engine_get_state (engine))
    {
      const gchar * const changes[] = { "", NULL };

      /* Our recorded state does not match the current state.  Something
       * must have changed while our watch requests were on the wire.
       *
       * We don't know what changed, so we can just say that potentially
       * everything under the path being watched changed.  This case is
       * very rare, anyway...
       */
      g_debug ("SHM invalidated while establishing subscription to %s - signalling change", ow->path);
      dconf_engine_change_notify (engine, ow->path, changes, NULL, FALSE, NULL, engine->user_data);
    }

  dconf_engine_lock_subscription_counts (engine);
  guint num_establishing = dconf_engine_count_subscriptions (engine->establishing,
                                                             ow->path);
  g_debug ("watch_established: \"%s\" (establishing: %d)", ow->path, num_establishing);
  if (num_establishing > 0)
    // Subscription(s): establishing -> active
    dconf_engine_move_subscriptions (engine->establishing,
                                     engine->active,
                                     ow->path);

  dconf_engine_unlock_subscription_counts (engine);
  g_clear_pointer (&ow->path, g_free);
  dconf_engine_call_handle_free (handle);
}

void
dconf_engine_watch_fast (DConfEngine *engine,
                         const gchar *path)
{
  dconf_engine_lock_subscription_counts (engine);
  guint num_establishing = dconf_engine_count_subscriptions (engine->establishing, path);
  guint num_active = dconf_engine_count_subscriptions (engine->active, path);
  g_debug ("watch_fast: \"%s\" (establishing: %d, active: %d)", path, num_establishing, num_active);
  if (num_active > 0)
    // Subscription: inactive -> active
    dconf_engine_inc_subscriptions (engine->active, path);
  else
    // Subscription: inactive -> establishing
    num_establishing = dconf_engine_inc_subscriptions (engine->establishing,
                                                       path);
  dconf_engine_unlock_subscription_counts (engine);
  if (num_establishing > 1 || num_active > 0)
    return;

  OutstandingWatch *ow;
  gint i;

  if (engine->n_sources == 0)
    return;

  /* It's possible (although rare) that the dconf database could change
   * while our match rule is on the wire.
   *
   * Since we returned immediately (suggesting to the user that the
   * watch was already established) we could have a race.
   *
   * To deal with this, we use the current state counter to ensure that nothing
   * changes while the watch requests are on the wire.
   */
  ow = dconf_engine_call_handle_new (engine, dconf_engine_watch_established,
                                     G_VARIANT_TYPE_UNIT, sizeof (OutstandingWatch));
  ow->state = dconf_engine_get_state (engine);
  ow->path = g_strdup (path);

  /* We start getting async calls returned as soon as we start dispatching them,
   * so we must not touch the 'ow' struct after we send the first one.
   */
  for (i = 0; i < engine->n_sources; i++)
    if (engine->sources[i]->bus_type)
      ow->pending++;

  for (i = 0; i < engine->n_sources; i++)
    if (engine->sources[i]->bus_type)
      dconf_engine_dbus_call_async_func (engine->sources[i]->bus_type, "org.freedesktop.DBus",
                                         "/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch",
                                         dconf_engine_make_match_rule (engine->sources[i], path),
                                         &ow->handle, NULL);
}

void
dconf_engine_unwatch_fast (DConfEngine *engine,
                           const gchar *path)
{
  dconf_engine_lock_subscription_counts (engine);
  guint num_active = dconf_engine_count_subscriptions (engine->active, path);
  guint num_establishing = dconf_engine_count_subscriptions (engine->establishing, path);
  gint i;
  g_debug ("unwatch_fast: \"%s\" (active: %d, establishing: %d)", path, num_active, num_establishing);

  // Client code cannot unsubscribe if it is not subscribed
  g_assert (num_active > 0 || num_establishing > 0);
  if (num_active == 0)
    // Subscription: establishing -> inactive
    num_establishing = dconf_engine_dec_subscriptions (engine->establishing, path);
  else
    // Subscription: active -> inactive
    num_active = dconf_engine_dec_subscriptions (engine->active, path);

  dconf_engine_unlock_subscription_counts (engine);
  if (num_active > 0 || num_establishing > 0)
    return;

  for (i = 0; i < engine->n_sources; i++)
    if (engine->sources[i]->bus_type)
      dconf_engine_dbus_call_async_func (engine->sources[i]->bus_type, "org.freedesktop.DBus",
                                         "/org/freedesktop/DBus", "org.freedesktop.DBus", "RemoveMatch",
                                         dconf_engine_make_match_rule (engine->sources[i], path), NULL, NULL);
}

static void
dconf_engine_handle_match_rule_sync (DConfEngine *engine,
                                     const gchar *method_name,
                                     const gchar *path)
{
  gint i;

  /* We need not hold any locks here because we are only touching static
   * things: the number of sources, and static properties of each source
   * itself.
   *
   * This function silently ignores all errors.
   */

  for (i = 0; i < engine->n_sources; i++)
    {
      GVariant *result;

      if (!engine->sources[i]->bus_type)
        continue;

      result = dconf_engine_dbus_call_sync_func (engine->sources[i]->bus_type, "org.freedesktop.DBus",
                                                 "/org/freedesktop/DBus", "org.freedesktop.DBus", method_name,
                                                 dconf_engine_make_match_rule (engine->sources[i], path),
                                                 G_VARIANT_TYPE_UNIT, NULL);

      if (result)
        g_variant_unref (result);
    }
}

void
dconf_engine_watch_sync (DConfEngine *engine,
                         const gchar *path)
{
  dconf_engine_lock_subscription_counts (engine);
  guint num_active = dconf_engine_inc_subscriptions (engine->active, path);
  dconf_engine_unlock_subscription_counts (engine);
  g_debug ("watch_sync: \"%s\" (active: %d)", path, num_active - 1);
  if (num_active == 1)
    dconf_engine_handle_match_rule_sync (engine, "AddMatch", path);
}

void
dconf_engine_unwatch_sync (DConfEngine *engine,
                           const gchar *path)
{
  dconf_engine_lock_subscription_counts (engine);
  guint num_active = dconf_engine_dec_subscriptions (engine->active, path);
  dconf_engine_unlock_subscription_counts (engine);
  g_debug ("unwatch_sync: \"%s\" (active: %d)", path, num_active + 1);
  if (num_active == 0)
    dconf_engine_handle_match_rule_sync (engine, "RemoveMatch", path);
}

typedef struct
{
  DConfEngineCallHandle handle;

  DConfChangeset *change;
} OutstandingChange;

static GVariant *
dconf_engine_prepare_change (DConfEngine     *engine,
                             DConfChangeset  *change)
{
  GVariant *serialised;

  serialised = dconf_changeset_serialise (change);

  return g_variant_new_from_data (G_VARIANT_TYPE ("(ay)"),
                                  g_variant_get_data (serialised), g_variant_get_size (serialised), TRUE,
                                  (GDestroyNotify) g_variant_unref, g_variant_ref_sink (serialised));
}

/* This function promotes the pending changeset to become the in-flight
 * changeset by sending the appropriate D-Bus message.
 *
 * Of course, this is only possible when there is a pending changeset
 * and no changeset is in-flight already. For this reason, this function
 * gets called in two situations:
 *
 *   - when there is a new pending changeset (due to an API call)
 *
 *   - when in-flight changeset had been delivered (due to a D-Bus
 *     reply having been received)
 */
static void dconf_engine_manage_queue (DConfEngine *engine);

/**
 * a #DConfChangesetPredicate which determines whether the given path and
 * value is already present in the given engine. "Already present" means
 * that setting that path to that value would have no effect on the
 * engine, including for directory resets.
 */
static gboolean
dconf_engine_path_has_value_predicate (const gchar *path,
                                      GVariant *new_value,
                                      gpointer user_data)
{
  DConfEngine *engine = user_data;

  // Path reset are handled specially
  if (g_str_has_suffix (path, "/"))
    return !dconf_engine_dir_has_writable_contents (engine, path);

  g_autoptr(GVariant) current_value = dconf_engine_read (
    engine,
    DCONF_READ_USER_VALUE,
    NULL,
    path
  );
  return ((current_value == NULL && new_value == NULL) ||
          (current_value != NULL && new_value != NULL &&
           g_variant_equal (current_value, new_value)));
}

static void
dconf_engine_emit_changes (DConfEngine    *engine,
                           DConfChangeset *changeset,
                           gpointer        origin_tag)
{
  const gchar *prefix;
  const gchar * const *changes;

  if (dconf_changeset_describe (changeset, &prefix, &changes, NULL))
    dconf_engine_change_notify (engine, prefix, changes, NULL, FALSE, origin_tag, engine->user_data);
}

static void
dconf_engine_change_completed (DConfEngine  *engine,
                               gpointer      handle,
                               GVariant     *reply,
                               const GError *error)
{
  OutstandingChange *oc = handle;
  DConfChangeset *expected;

  dconf_engine_lock_queue (engine);

  expected = g_steal_pointer (&engine->in_flight);
  g_assert (expected && oc->change == expected);

  /* Another request could be sent now. Check for pending changes. */
  dconf_engine_manage_queue (engine);
  dconf_engine_unlock_queue (engine);

  /* Deal with the reply we got. */
  if (reply)
    {
      /* The write worked.
       *
       * We already sent a change notification for this item when we
       * added it to the pending queue and we don't want to send another
       * one again.  At the same time, it's very likely that we're just
       * about to receive a change signal from the service.
       *
       * The tag sent as part of the reply to the Change call will be
       * the same tag as on the change notification signal.  Record that
       * tag so that we can ignore the signal when it comes.
       *
       * last_handled is only ever touched from the worker thread
       */
      g_free (engine->last_handled);
      g_variant_get (reply, "(s)", &engine->last_handled);
    }

  if (error)
    {
      /* Some kind of unexpected failure occurred while attempting to
       * commit the change.
       *
       * There's not much we can do here except to drop our local copy
       * of the change (and notify that it is gone) and print the error
       * message as a warning.
       */
      g_warning ("failed to commit changes to dconf: %s", error->message);
      dconf_engine_emit_changes (engine, oc->change, NULL);
    }

  dconf_changeset_unref (oc->change);
  dconf_engine_call_handle_free (handle);
}

static void
dconf_engine_manage_queue (DConfEngine *engine)
{
  if (engine->pending != NULL && engine->in_flight == NULL)
    {
      OutstandingChange *oc;
      GVariant *parameters;

      oc = dconf_engine_call_handle_new (engine, dconf_engine_change_completed,
                                         G_VARIANT_TYPE ("(s)"), sizeof (OutstandingChange));

      oc->change = engine->in_flight = g_steal_pointer (&engine->pending);
      dconf_changeset_seal (engine->in_flight);

      parameters = dconf_engine_prepare_change (engine, oc->change);

      dconf_engine_dbus_call_async_func (engine->sources[0]->bus_type,
                                         engine->sources[0]->bus_name,
                                         engine->sources[0]->object_path,
                                         "ca.desrt.dconf.Writer", "Change",
                                         parameters, &oc->handle, NULL);
    }

  if (engine->in_flight == NULL)
    {
      /* The in-flight queue should not be empty if we have changes
       * pending...
       */
      g_assert (engine->pending == NULL);

      g_cond_broadcast (&engine->queue_cond);
    }
}

static gboolean
dconf_engine_is_writable_changeset_predicate (const gchar *key,
                                              GVariant    *value,
                                              gpointer     user_data)
{
  DConfEngine *engine = user_data;

  /* Resets absolutely always succeed -- even in the case that there is
   * not even a writable database.
   */
  return value == NULL || dconf_engine_is_writable_internal (engine, key);
}

static gboolean
dconf_engine_changeset_changes_only_writable_keys (DConfEngine    *engine,
                                                   DConfChangeset *changeset,
                                                   GError         **error)
{
  gboolean success = TRUE;

  dconf_engine_acquire_sources (engine);

  if (!dconf_changeset_all (changeset, dconf_engine_is_writable_changeset_predicate, engine))
    {
      g_set_error_literal (error, DCONF_ERROR, DCONF_ERROR_NOT_WRITABLE,
                           "The operation attempted to modify one or more non-writable keys");
      success = FALSE;
    }

  dconf_engine_release_sources (engine);

  return success;
}

gboolean
dconf_engine_change_fast (DConfEngine     *engine,
                          DConfChangeset  *changeset,
                          gpointer         origin_tag,
                          GError         **error)
{
  g_debug ("change_fast");
  if (dconf_changeset_is_empty (changeset))
    return TRUE;

  gboolean has_no_effect = dconf_changeset_all (changeset,
                                                dconf_engine_path_has_value_predicate,
                                                engine);

  if (!dconf_engine_changeset_changes_only_writable_keys (engine, changeset, error))
    return FALSE;

  dconf_changeset_seal (changeset);

  dconf_engine_lock_queue (engine);

  /* The pending changeset is kept unsealed so that it can be modified
   * by later calls to this functions. It wouldn't be a good idea to
   * repurpose the incoming changeset for this role, so create a new
   * one if necessary. */
  if (engine->pending == NULL)
    engine->pending = dconf_changeset_new ();

  dconf_changeset_change (engine->pending, changeset);

  /* There might be no in-flight request yet, so we try to manage the
   * queue right away in order to try to promote pending changes there
   * (which causes the D-Bus message to actually be sent). */
  dconf_engine_manage_queue (engine);

  dconf_engine_unlock_queue (engine);

  /* Emit the signal after dropping the lock to avoid deadlock on re-entry. */
  if (!has_no_effect)
    dconf_engine_emit_changes (engine, changeset, origin_tag);

  return TRUE;
}

gboolean
dconf_engine_change_sync (DConfEngine     *engine,
                          DConfChangeset  *changeset,
                          gchar          **tag,
                          GError         **error)
{
  GVariant *reply;
  g_debug ("change_sync");

  if (dconf_changeset_is_empty (changeset))
    {
      if (tag)
        *tag = g_strdup ("");

      return TRUE;
    }

  if (!dconf_engine_changeset_changes_only_writable_keys (engine, changeset, error))
    return FALSE;

  dconf_changeset_seal (changeset);

  /* we know that we have at least one source because we checked writability */
  reply = dconf_engine_dbus_call_sync_func (engine->sources[0]->bus_type,
                                            engine->sources[0]->bus_name,
                                            engine->sources[0]->object_path,
                                            "ca.desrt.dconf.Writer", "Change",
                                            dconf_engine_prepare_change (engine, changeset),
                                            G_VARIANT_TYPE ("(s)"), error);

  if (reply == NULL)
    return FALSE;

  /* g_variant_get() is okay with NULL tag */
  g_variant_get (reply, "(s)", tag);
  g_variant_unref (reply);

  return TRUE;
}

static gboolean
dconf_engine_is_interested_in_signal (DConfEngine *engine,
                                      GBusType     bus_type,
                                      const gchar *sender,
                                      const gchar *path)
{
  gint i;

  for (i = 0; i < engine->n_sources; i++)
    {
      DConfEngineSource *source = engine->sources[i];

      if (source->bus_type == bus_type && g_str_equal (source->object_path, path))
        return TRUE;
    }

  return FALSE;
}

void
dconf_engine_handle_dbus_signal (GBusType     type,
                                 const gchar *sender,
                                 const gchar *object_path,
                                 const gchar *member,
                                 GVariant    *body)
{
  if (g_str_equal (member, "Notify"))
    {
      const gchar *prefix;
      const gchar **changes;
      const gchar *tag;
      GSList *engines;

      if (!g_variant_is_of_type (body, G_VARIANT_TYPE ("(sass)")))
        return;

      g_variant_get (body, "(&s^a&s&s)", &prefix, &changes, &tag);

      /* Reject junk */
      if (changes[0] == NULL)
        /* No changes?  Do nothing. */
        goto junk;

      if (dconf_is_key (prefix, NULL))
        {
          /* If the prefix is a key then the changes must be ['']. */
          if (changes[0][0] || changes[1])
            goto junk;
        }
      else if (dconf_is_dir (prefix, NULL))
        {
          /* If the prefix is a dir then we can have changes within that
           * dir, but they must be rel paths.
           *
           *   ie:
           *
           *  ('/a/', ['b', 'c/']) == ['/a/b', '/a/c/']
           */
          gint i;

          for (i = 0; changes[i]; i++)
            if (!dconf_is_rel_path (changes[i], NULL))
              goto junk;
        }
      else
        /* Not a key or a dir? */
        goto junk;

      g_mutex_lock (&dconf_engine_global_lock);
      engines = g_slist_copy_deep (dconf_engine_global_list, (GCopyFunc) dconf_engine_ref, NULL);
      g_mutex_unlock (&dconf_engine_global_lock);

      while (engines)
        {
          DConfEngine *engine = engines->data;

          /* It's possible that this incoming change notify is for a
           * change that we already announced to the client when we
           * placed it in the queue.
           *
           * Check last_handled to determine if we should ignore it.
           */
          if (!engine->last_handled || !g_str_equal (engine->last_handled, tag))
            if (dconf_engine_is_interested_in_signal (engine, type, sender, object_path))
              dconf_engine_change_notify (engine, prefix, changes, tag, FALSE, NULL, engine->user_data);

          engines = g_slist_delete_link (engines, engines);

          dconf_engine_unref (engine);
        }

junk:
      g_free (changes);
    }

  else if (g_str_equal (member, "WritabilityNotify"))
    {
      const gchar *empty_str_list[] = { "", NULL };
      const gchar *path;
      GSList *engines;

      if (!g_variant_is_of_type (body, G_VARIANT_TYPE ("(s)")))
        return;

      g_variant_get (body, "(&s)", &path);

      /* Rejecting junk here is relatively straightforward */
      if (!dconf_is_path (path, NULL))
        return;

      g_mutex_lock (&dconf_engine_global_lock);
      engines = g_slist_copy_deep (dconf_engine_global_list, (GCopyFunc) dconf_engine_ref, NULL);
      g_mutex_unlock (&dconf_engine_global_lock);

      while (engines)
        {
          DConfEngine *engine = engines->data;

          if (dconf_engine_is_interested_in_signal (engine, type, sender, object_path))
            dconf_engine_change_notify (engine, path, empty_str_list, "", TRUE, NULL, engine->user_data);

          engines = g_slist_delete_link (engines, engines);

          dconf_engine_unref (engine);
        }
    }
}

gboolean
dconf_engine_has_outstanding (DConfEngine *engine)
{
  gboolean has;

  /* The in-flight will never be empty unless the pending is
   * also empty, so we only really need to check one of them...
   */
  dconf_engine_lock_queue (engine);
  has = engine->in_flight != NULL;
  dconf_engine_unlock_queue (engine);

  return has;
}

void
dconf_engine_sync (DConfEngine *engine)
{
  g_debug ("sync");
  dconf_engine_lock_queue (engine);
  while (engine->in_flight != NULL)
    g_cond_wait (&engine->queue_cond, &engine->queue_lock);
  dconf_engine_unlock_queue (engine);
}
