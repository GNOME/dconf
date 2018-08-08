/*
 * Copyright © 2010 Codethink Limited
 * Copyright © 2012 Canonical Limited
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

#include "dconf-client.h"

#include "../engine/dconf-engine.h"
#include "../common/dconf-paths.h"
#include <glib-object.h>

/**
 * SECTION:client
 * @title: DConfClient
 * @short_description: Direct read and write access to dconf, based on GDBus
 *
 * This is the primary client interface to dconf.
 *
 * It allows applications to directly read from and write to the dconf
 * database.  Applications can subscribe to change notifications.
 *
 * Most applications probably don't want to access dconf directly and
 * would be better off using something like #GSettings.
 *
 * Please note that the API of libdconf is not stable in any way.  It
 * has changed in incompatible ways in the past and there will be
 * further changes in the future.
 **/

/**
 * DConfClient:
 *
 * The main object for interacting with dconf.  This is a #GObject, so
 * you should manage it with g_object_ref() and g_object_unref().
 **/
struct _DConfClient
{
  GObject parent_instance;

  DConfEngine  *engine;
  GMainContext *context;
};

G_DEFINE_TYPE (DConfClient, dconf_client, G_TYPE_OBJECT)

enum
{
  SIGNAL_CHANGED,
  SIGNAL_WRITABILITY_CHANGED,
  N_SIGNALS
};
static guint dconf_client_signals[N_SIGNALS];

static void
dconf_client_finalize (GObject *object)
{
  DConfClient *client = DCONF_CLIENT (object);

  dconf_engine_unref (client->engine);
  g_main_context_unref (client->context);

  G_OBJECT_CLASS (dconf_client_parent_class)
    ->finalize (object);
}

static void
dconf_client_init (DConfClient *client)
{
}

static void
dconf_client_class_init (DConfClientClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = dconf_client_finalize;

  /**
   * DConfClient::changed:
   * @client: the #DConfClient reporting the change
   * @prefix: the prefix under which the changes happened
   * @changes: the list of paths that were changed, relative to @prefix
   * @tag: the tag for the change, if it originated from the service
   *
   * This signal is emitted when the #DConfClient has a possible change
   * to report.  The signal is an indication that a change may have
   * occurred; it's possible that the keys will still have the same value
   * as before.
   *
   * To ensure that you receive notification about changes to paths that
   * you are interested in you must call dconf_client_watch_fast() or
   * dconf_client_watch_sync().  You may still receive notifications for
   * paths that you did not explicitly watch.
   *
   * @prefix will be an absolute dconf path; see dconf_is_path().
   * @changes is a %NULL-terminated array of dconf rel paths; see
   * dconf_is_rel_path().
   *
   * @tag is an opaque tag string, or %NULL.  The only thing you should
   * do with @tag is to compare it to tag values returned by
   * dconf_client_write_sync() or dconf_client_change_sync().
   *
   * The number of changes being reported is equal to the length of
   * @changes.  Appending each item in @changes to @prefix will give the
   * absolute path of each changed item.
   *
   * If a single key has changed then @prefix will be equal to the key
   * and @changes will contain a single item: the empty string.
   *
   * If a single dir has changed (indicating that any key under the dir
   * may have changed) then @prefix will be equal to the dir and
   * @changes will contain a single empty string.
   *
   * If more than one change is being reported then @changes will have
   * more than one item.
   **/
  dconf_client_signals[SIGNAL_CHANGED] = g_signal_new ("changed", DCONF_TYPE_CLIENT, G_SIGNAL_RUN_LAST,
                                                       0, NULL, NULL, NULL, G_TYPE_NONE, 3,
                                                       G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE,
                                                       G_TYPE_STRV | G_SIGNAL_TYPE_STATIC_SCOPE,
                                                       G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE);

  /**
   * DConfClient::writability-changed:
   * @client: the #DConfClient reporting the change
   * @path: the dir or key that changed
   *
   * Signal emitted when writability for a key (or all keys in a dir) changes.
   * It will be immediately followed by #DConfClient::changed signal for
   * the path.
   */
  dconf_client_signals[SIGNAL_WRITABILITY_CHANGED] = g_signal_new ("writability-changed", DCONF_TYPE_CLIENT,
                                                                   G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                                                   G_TYPE_NONE, 1,
                                                                   G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE);
}

typedef struct
{
  DConfClient  *client;
  gchar        *prefix;
  gchar       **changes;
  gchar        *tag;
  gboolean      is_writability;
} DConfClientChange;

static gboolean
dconf_client_dispatch_change_signal (gpointer user_data)
{
  DConfClientChange *change = user_data;

  if (change->is_writability)
    {
      /* We know that the engine does it this way... */
      g_assert (change->changes[0][0] == '\0' && change->changes[1] == NULL);

      g_signal_emit (change->client,
                     dconf_client_signals[SIGNAL_WRITABILITY_CHANGED], 0,
                     change->prefix);
    }

  g_signal_emit (change->client, dconf_client_signals[SIGNAL_CHANGED], 0,
                 change->prefix, change->changes, change->tag);

  g_object_unref (change->client);
  g_free (change->prefix);
  g_strfreev (change->changes);
  g_free (change->tag);
  g_slice_free (DConfClientChange, change);

  return G_SOURCE_REMOVE;
}

void
dconf_engine_change_notify (DConfEngine         *engine,
                            const gchar         *prefix,
                            const gchar * const *changes,
                            const gchar *        tag,
                            gboolean             is_writability,
                            gpointer             origin_tag,
                            gpointer             user_data)
{
  GWeakRef *weak_ref = user_data;
  DConfClientChange *change;
  DConfClient *client;

  client = g_weak_ref_get (weak_ref);

  if (client == NULL)
    return;

  g_return_if_fail (DCONF_IS_CLIENT (client));

  change = g_slice_new (DConfClientChange);
  change->client = client;
  change->prefix = g_strdup (prefix);
  change->changes = g_strdupv ((gchar **) changes);
  change->tag = g_strdup (tag);
  change->is_writability = is_writability;

  g_main_context_invoke (client->context, dconf_client_dispatch_change_signal, change);
}

static void
dconf_client_free_weak_ref (gpointer data)
{
  GWeakRef *weak_ref = data;

  g_weak_ref_clear (weak_ref);
  g_slice_free (GWeakRef, weak_ref);
}

/**
 * dconf_client_new:
 *
 * Creates a new #DConfClient.
 *
 * Returns: (transfer full): a new #DConfClient
 **/
DConfClient *
dconf_client_new (void)
{
  DConfClient *client;
  GWeakRef *weak_ref;

  client = g_object_new (DCONF_TYPE_CLIENT, NULL);
  weak_ref = g_slice_new (GWeakRef);
  g_weak_ref_init (weak_ref, client);
  client->engine = dconf_engine_new (NULL, weak_ref, dconf_client_free_weak_ref);
  client->context = g_main_context_ref_thread_default ();

  return client;
}

/**
 * dconf_client_read:
 * @client: a #DConfClient
 * @key: the key to read the value of
 *
 * Reads the current value of @key.
 *
 * If @key exists, its value is returned.  Otherwise, %NULL is returned.
 *
 * If there are outstanding "fast" changes in progress they may affect
 * the result of this call.
 *
 * Returns: (transfer full) (nullable): a #GVariant, or %NULL
 **/
GVariant *
dconf_client_read (DConfClient *client,
                   const gchar *key)
{
  g_return_val_if_fail (DCONF_IS_CLIENT (client), NULL);

  return dconf_engine_read (client->engine, DCONF_READ_FLAGS_NONE, NULL, key);
}

/**
 * DConfReadFlags:
 * @DCONF_READ_FLAGS_NONE: no flags
 * @DCONF_READ_DEFAULT_VALUE: read the default value, ignoring any
 *   values in writable databases or any queued changes.  This is
 *   effectively equivalent to asking what value would be read after a
 *   reset was written for the key in question.
 * @DCONF_READ_USER_VALUE: read the user value, ignoring any system
 *   databases, including ignoring locks.  It is even possible to read
 *   "invisible" values in the user database in this way, which would
 *   have normally been ignored because of locks.
 *
 * Since: 0.26
 */

/**
 * dconf_client_read_full:
 * @client: a #DConfClient
 * @key: the key to read the default value of
 * @flags: #DConfReadFlags
 * @read_through: a #GQueue of #DConfChangeset
 *
 * Reads the current value of @key.
 *
 * If @flags contains %DCONF_READ_USER_VALUE then only the user value
 * will be read.  Locks are ignored, which means that it is possible to
 * use this API to read "invisible" user values which are hidden by
 * system locks.
 *
 * If @flags contains %DCONF_READ_DEFAULT_VALUE then only non-user
 * values will be read.  The result will be exactly equivalent to the
 * value that would be read if the current value of the key were to be
 * reset.
 *
 * Flags may not contain both %DCONF_READ_USER_VALUE and
 * %DCONF_READ_DEFAULT_VALUE.
 *
 * If @read_through is non-%NULL, %DCONF_READ_DEFAULT_VALUE is not
 * given then @read_through is checked for the key in question, subject
 * to the restriction that the key in question is writable.  This
 * effectively answers the question of "what would happen if these
 * changes were committed".
 *
 * If there are outstanding "fast" changes in progress they may affect
 * the result of this call.
 *
 * If @flags is %DCONF_READ_FLAGS_NONE and @read_through is %NULL then
 * this call is exactly equivalent to dconf_client_read().
 *
 * Returns: (transfer full) (nullable): a #GVariant, or %NULL
 *
 * Since: 0.26
 */
GVariant *
dconf_client_read_full (DConfClient    *client,
                        const gchar    *key,
                        DConfReadFlags  flags,
                        const GQueue   *read_through)
{
  g_return_val_if_fail (DCONF_IS_CLIENT (client), NULL);

  return dconf_engine_read (client->engine, flags, read_through, key);
}

/**
 * dconf_client_list:
 * @client: a #DConfClient
 * @dir: the dir to list the contents of
 * @length: the length of the returned list
 *
 * Gets the list of all dirs and keys immediately under @dir.
 *
 * If @length is non-%NULL then it will be set to the length of the
 * returned array.  In any case, the array is %NULL-terminated.
 *
 * IF there are outstanding "fast" changes in progress then this call
 * may return inaccurate results with respect to those outstanding
 * changes.
 *
 * Returns: (transfer full) (not nullable): an array of strings, never %NULL.
 **/
gchar **
dconf_client_list (DConfClient *client,
                   const gchar *dir,
                   gint        *length)
{
  g_return_val_if_fail (DCONF_IS_CLIENT (client), NULL);

  return dconf_engine_list (client->engine, dir, length);
}

/**
 * dconf_client_list_locks:
 * @client: a #DConfClient
 * @dir: the dir to limit results to
 * @length: the length of the returned list.
 *
 * Lists all locks under @dir in effect for @client.
 *
 * If no locks are in effect, an empty list is returned.  If no keys are
 * writable at all then a list containing @dir is returned.
 *
 * The returned list will be %NULL-terminated.
 *
 * Returns: (transfer full) (not nullable): an array of strings, never %NULL.
 *
 * Since: 0.26
 */
gchar **
dconf_client_list_locks (DConfClient *client,
                         const gchar *dir,
                         gint        *length)
{
  g_return_val_if_fail (DCONF_IS_CLIENT (client), NULL);
  g_return_val_if_fail (dconf_is_dir (dir, NULL), NULL);

  return dconf_engine_list_locks (client->engine, dir, length);
}

/**
 * dconf_client_is_writable:
 * @client: a #DConfClient
 * @key: the key to check for writability
 *
 * Checks if @key is writable (ie: the key has no locks).
 *
 * This call does not verify that writing to the key will actually be
 * successful.  It only checks that the database is writable and that
 * there are no locks affecting @key.  Other issues (such as a full disk
 * or an inability to connect to the bus and start the service) may
 * cause the write to fail.
 *
 * Returns: %TRUE if @key is writable
 **/
gboolean
dconf_client_is_writable (DConfClient *client,
                          const gchar *key)
{
  g_return_val_if_fail (DCONF_IS_CLIENT (client), FALSE);

  return dconf_engine_is_writable (client->engine, key);
}

/**
 * dconf_client_write_fast:
 * @client: a #DConfClient
 * @key: the key to write to
 * @value: a #GVariant, the value to write. If it has a floating reference it's
 *  consumed.
 * @error: a pointer to a %NULL #GError, or %NULL
 *
 * Writes @value to the given @key, or reset @key to its default value.
 *
 * If @value is %NULL then @key is reset to its default value (which may
 * be completely unset), otherwise @value becomes the new value.
 *
 * This call merely queues up the write and returns immediately, without
 * blocking.  The only errors that can be detected or reported at this
 * point are attempts to write to read-only keys.  If the application
 * exits immediately after this function returns then the queued call
 * may never be sent; see dconf_client_sync().
 *
 * A local copy of the written value is kept so that calls to
 * dconf_client_read() that occur before the service actually makes the
 * change will return the new value.
 *
 * If the write is queued then a change signal will be directly emitted.
 * If this function is being called from the main context of @client
 * then the signal is emitted before this function returns; otherwise it
 * is scheduled on the main context.
 *
 * Returns: %TRUE if the write was queued
 **/
gboolean
dconf_client_write_fast (DConfClient  *client,
                         const gchar  *key,
                         GVariant     *value,
                         GError      **error)
{
  DConfChangeset *changeset;
  gboolean success;

  g_return_val_if_fail (DCONF_IS_CLIENT (client), FALSE);

  changeset = dconf_changeset_new_write (key, value);
  success = dconf_engine_change_fast (client->engine, changeset, NULL, error);
  dconf_changeset_unref (changeset);

  return success;
}

/**
 * dconf_client_write_sync:
 * @client: a #DConfClient
 * @key: the key to write to
 * @value: a #GVariant, the value to write. If it has a floating reference it's
 *  consumed.
 * @tag: (out) (optional) (not nullable) (transfer full): the tag from this write
 * @cancellable: a #GCancellable, or %NULL
 * @error: a pointer to a %NULL #GError, or %NULL
 *
 * Write @value to the given @key, or reset @key to its default value.
 *
 * If @value is %NULL then @key is reset to its default value (which may
 * be completely unset), otherwise @value becomes the new value.
 *
 * This call blocks until the write is complete.  This call will
 * therefore detect and report all cases of failure.  If the modified
 * key is currently being watched then a signal will be emitted from the
 * main context of @client (once the signal arrives from the service).
 *
 * If @tag is non-%NULL then it is set to the unique tag associated with
 * this write.  This is the same tag that will appear in the following
 * change signal.
 *
 * Returns: %TRUE on success, else %FALSE with @error set
 **/
gboolean
dconf_client_write_sync (DConfClient   *client,
                         const gchar   *key,
                         GVariant      *value,
                         gchar        **tag,
                         GCancellable  *cancellable,
                         GError       **error)
{
  DConfChangeset *changeset;
  gboolean success;

  g_return_val_if_fail (DCONF_IS_CLIENT (client), FALSE);

  changeset = dconf_changeset_new_write (key, value);
  success = dconf_engine_change_sync (client->engine, changeset, tag, error);
  dconf_changeset_unref (changeset);

  return success;
}

/**
 * dconf_client_change_fast:
 * @client: a #DConfClient
 * @changeset: the changeset describing the requested change
 * @error: a pointer to a %NULL #GError, or %NULL
 *
 * Performs the change operation described by @changeset.
 *
 * Once @changeset is passed to this call it can no longer be modified.
 *
 * This call merely queues up the write and returns immediately, without
 * blocking.  The only errors that can be detected or reported at this
 * point are attempts to write to read-only keys.  If the application
 * exits immediately after this function returns then the queued call
 * may never be sent; see dconf_client_sync().
 *
 * A local copy of the written value is kept so that calls to
 * dconf_client_read() that occur before the service actually makes the
 * change will return the new value.
 *
 * If the write is queued then a change signal will be directly emitted.
 * If this function is being called from the main context of @client
 * then the signal is emitted before this function returns; otherwise it
 * is scheduled on the main context.
 *
 * Returns: %TRUE if the requested changed was queued
 **/
gboolean
dconf_client_change_fast (DConfClient     *client,
                          DConfChangeset  *changeset,
                          GError         **error)
{
  g_return_val_if_fail (DCONF_IS_CLIENT (client), FALSE);

  return dconf_engine_change_fast (client->engine, changeset, NULL, error);
}

/**
 * dconf_client_change_sync:
 * @client: a #DConfClient
 * @changeset: the changeset describing the requested change
 * @tag: (out) (optional) (not nullable) (transfer full): the tag from this write
 * @cancellable: a #GCancellable, or %NULL
 * @error: a pointer to a %NULL #GError, or %NULL
 *
 * Performs the change operation described by @changeset.
 *
 * Once @changeset is passed to this call it can no longer be modified.
 *
 * This call blocks until the change is complete.  This call will
 * therefore detect and report all cases of failure.  If any of the
 * modified keys are currently being watched then a signal will be
 * emitted from the main context of @client (once the signal arrives
 * from the service).
 *
 * If @tag is non-%NULL then it is set to the unique tag associated with
 * this change.  This is the same tag that will appear in the following
 * change signal.  If @changeset makes no changes then @tag may be
 * non-unique (eg: the empty string may be used for empty changesets).
 *
 * Returns: %TRUE on success, else %FALSE with @error set
 **/
gboolean
dconf_client_change_sync (DConfClient     *client,
                          DConfChangeset  *changeset,
                          gchar          **tag,
                          GCancellable    *cancellable,
                          GError         **error)
{
  g_return_val_if_fail (DCONF_IS_CLIENT (client), FALSE);

  return dconf_engine_change_sync (client->engine, changeset, tag, error);
}

/**
 * dconf_client_watch_fast:
 * @client: a #DConfClient
 * @path: a path to watch
 *
 * Requests change notifications for @path.
 *
 * If @path is a key then the single key is monitored.  If @path is a
 * dir then all keys under the dir are monitored.
 *
 * This function queues the watch request with D-Bus and returns
 * immediately.  There is a very slim chance that the dconf database
 * could change before the watch is actually established.  If that is
 * the case then a synthetic change signal will be emitted.
 *
 * Errors are silently ignored.
 **/
void
dconf_client_watch_fast (DConfClient *client,
                         const gchar *path)
{
  g_return_if_fail (DCONF_IS_CLIENT (client));

  dconf_engine_watch_fast (client->engine, path);
}

/**
 * dconf_client_watch_sync:
 * @client: a #DConfClient
 * @path: a path to watch
 *
 * Requests change notifications for @path.
 *
 * If @path is a key then the single key is monitored.  If @path is a
 * dir then all keys under the dir are monitored.
 *
 * This function submits each of the various watch requests that are
 * required to monitor a key and waits until each of them returns.  By
 * the time this function returns, the watch has been established.
 *
 * Errors are silently ignored.
 **/
void
dconf_client_watch_sync (DConfClient *client,
                         const gchar *path)
{
  g_return_if_fail (DCONF_IS_CLIENT (client));

  dconf_engine_watch_sync (client->engine, path);
}

/**
 * dconf_client_unwatch_fast:
 * @client: a #DConfClient
 * @path: a path previously watched
 *
 * Cancels the effect of a previous call to dconf_client_watch_fast().
 *
 * This call returns immediately.
 *
 * It is still possible that change signals are received after this call
 * had returned (watching guarantees notification of changes, but
 * unwatching does not guarantee no notifications).
 **/
void
dconf_client_unwatch_fast (DConfClient *client,
                           const gchar *path)
{
  g_return_if_fail (DCONF_IS_CLIENT (client));

  dconf_engine_unwatch_fast (client->engine, path);
}

/**
 * dconf_client_unwatch_sync:
 * @client: a #DConfClient
 * @path: a path previously watched
 *
 * Cancels the effect of a previous call to dconf_client_watch_sync().
 *
 * This function submits each of the various unwatch requests and waits
 * until each of them returns.  It is still possible that change signals
 * are received after this call has returned (watching guarantees
 * notification of changes, but unwatching does not guarantee no
 * notifications).
 **/
void
dconf_client_unwatch_sync (DConfClient *client,
                           const gchar *path)
{
  g_return_if_fail (DCONF_IS_CLIENT (client));

  dconf_engine_unwatch_sync (client->engine, path);
}

/**
 * dconf_client_sync:
 * @client: a #DConfClient
 *
 * Blocks until all outstanding "fast" change or write operations have
 * been submitted to the service.
 *
 * Applications should generally call this before exiting on any
 * #DConfClient that they wrote to.
 **/
void
dconf_client_sync (DConfClient *client)
{
  g_return_if_fail (DCONF_IS_CLIENT (client));

  dconf_engine_sync (client->engine);
}
