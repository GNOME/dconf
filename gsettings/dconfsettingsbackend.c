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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#define G_SETTINGS_ENABLE_BACKEND
#include <gio/gsettingsbackend.h>
#include "../engine/dconf-engine.h"
#include <gio/gio.h>

#include <string.h>

typedef GSettingsBackendClass DConfSettingsBackendClass;
typedef struct _DConfSettingsBackend DConfSettingsBackend;

/* The DConfSettingsBackend can be in one of two major states:
 *
 *  1) directly connected to the engine
 *
 *    In this case the 'changeset' and 'parent' fields are NULL.
 *
 *  2) acting as a delayed backend
 *
 *    In this case the 'changeset' field is non-NULL and the 'parent'
 *    field points at the DConfSettingsBackend that is parent to this
 *    backend.  This is a strong reference.
 *
 * In either case the 'children' list contains the list of delayed
 * settings objects that have this object as their parent.  These are
 * weak references (since the strong reference is in the child->parent
 * direction).  We use GWeakRef here to make sure we don't try to send
 * signals to an object that's already half-disposed in another thread.
 *
 * We use code locking.
 *
 * The average GSettings-using program will only ever have a single
 * backend.  Even those that use the "delayed" functionality will only
 * have one or two and, even in that case, most interactions will
 * require locking of multiple backends anyway.  The logic is much
 * easier if there is only a single lock.
 *
 * An interesting possibility might be to share a lock per-engine (or
 * use the engine's lock itself).
 */
struct _DConfSettingsBackend
{
  GSettingsBackend      parent_instance;

  DConfEngine          *engine;         /* always set */
  DConfSettingsBackend *parent;         /* set only for delayed backends */

  DConfChangeset       *changeset;      /* will be non-NULL only for delayed backends */
  GSList               *children;       /* list of delayed backends under us (for change notification) */
};

static GMutex dconf_settings_backend_lock;

static GType dconf_settings_backend_get_type (void);
G_DEFINE_TYPE (DConfSettingsBackend, dconf_settings_backend, G_TYPE_SETTINGS_BACKEND)

/* The following three functions are the only functions that ever touch
 * the 'children' list.
 *
 * All three functions should be called unlocked (and each of them will
 * acquire the lock).
 *
 * dconf_settings_backend_add_child: add a backend to the child list of
 *   its parent (when creating a delayed settings backend object)
 * dconf_settings_backend_prune_dead_child: cleanup one dead child from
 *   the parent's list (called from finalize of a delayed backend)
 * dconf_settings_backend_get_child_list: get a GSList of strong
 *   references to child objects of this backend (used during the
 *   propagation of change signals).  Caller must deep-free the list.
 */
static void
dconf_settings_backend_add_child (DConfSettingsBackend *dcsb,
                                  DConfSettingsBackend *child)
{
  /* Abuse the GWeakRef API a bit.
   *
   * We use the ->data pointer here, knowing that GWeakRef is the same
   * size as a pointer.
   *
   * On finalize of the child, the weak ref will have been set to NULL,
   * so we can just remove NULL from the list.
   */
  g_mutex_lock (&dconf_settings_backend_lock);
  dcsb->children = g_slist_prepend (dcsb->children, NULL);
  g_weak_ref_init ((GWeakRef *) &dcsb->children->data, child);
  g_mutex_unlock (&dconf_settings_backend_lock);
}

static void
dconf_settings_backend_prune_dead_child (DConfSettingsBackend *dcsb)
{
  /* Since we're storing weakrefs in our 'children' list, they will be
   * set back to NULL automatically when the child is freed.
   *
   * All that is left is to prune those values from the list so that it
   * doesn't grow unboundedly as we add and remove children.
   *
   * This is called each time we remove a child, so we only really need
   * to remove one NULL each time.  Even if there is a race and we
   * remove the "wrong" NULL (ie: the one that used to belong to the
   * child being finalised in the other thread), the other thread will
   * remove the one that used to belong to us...
   *
   * N.B. It _should_ be safe to access the GWeakRef directly, although
   * it's definitely 'evil'...
   */
  g_mutex_lock (&dconf_settings_backend_lock);
  dcsb->children = g_slist_remove (dcsb->children, NULL);
  g_mutex_unlock (&dconf_settings_backend_lock);
}

static GSList *
dconf_settings_backend_get_child_list (DConfSettingsBackend *dcsb)
{
  GSList *children = NULL;
  GSList *node;

  /* Turn the instance variable list of weak reference to child objects
   * into a local copy: a list of strong references.  This ensures that
   * nobody is freeing objects in another thread as we're trying to
   * report changes to them.
   */
  g_mutex_lock (&dconf_settings_backend_lock);
  for (node = dcsb->children; node; node = node->next)
    {
      DConfSettingsBackend *child;

      child = g_weak_ref_get ((GWeakRef *) &node->data);

      if (child)
        children = g_slist_prepend (children, child);
    }
  g_mutex_unlock (&dconf_settings_backend_lock);

  return children;
}

static GVariant *
dconf_settings_backend_read (GSettingsBackend   *backend,
                             const gchar        *key,
                             const GVariantType *expected_type)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) backend;
  GVariant *value;

  if (dcsb->changeset)
    /* The "delayed" case -- need to provide the read_through list */
    {
      GQueue read_through = G_QUEUE_INIT;
      DConfSettingsBackend *node;

      /* We hold the lock for the entire duration of the read in order
       * to ensure that no other threads are modifying the changesets
       * while _read() may be iterating over the queue.
       *
       * It might be possible to avoid this if we had copy-on-write
       * changesets, but it's probably not worth the fuss...
       */
      g_mutex_lock (&dconf_settings_backend_lock);

      /* Collect the changeset from each backend up to the toplevel one.
       *
       * The queue will be iterated from tail to head so we need to make
       * sure that the "most delayed" changset is the one at the tail.
       * We do this by prepending parents to the head.
       */
      for (node = dcsb; node->changeset; node = node->parent)
        g_queue_push_head (&read_through, node->changeset);

      /* Actually do the read */
      value = dconf_engine_read (dcsb->engine, &read_through, key);

      /* Free the queue */
      g_queue_clear (&read_through);

      /* Drop the lock */
      g_mutex_unlock (&dconf_settings_backend_lock);
    }

  else
    /* Normal read case. */
    value = dconf_engine_read (dcsb->engine, NULL, key);

  return value;
}

static gboolean
dconf_settings_backend_write (GSettingsBackend *backend,
                              const gchar      *key,
                              GVariant         *value)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) backend;
  gboolean success;

  if (dcsb->changeset)
    {
      /* We check for writability while holding the lock in order to
       * ensure that we don't get an interleaved writability change
       * event in another thread after we check but before we set.
       *
       * If the writability change event does come _after_ the set
       * then it will remove the change from the changeset.
       */
      g_mutex_lock (&dconf_settings_backend_lock);

      success = dconf_engine_is_writable (dcsb->engine, key);

      if (success)
        {
          dconf_changeset_set (dcsb->changeset, key, value);
          /* emit changes... */
        }

      g_mutex_unlock (&dconf_settings_backend_lock);
    }
  else
    {
      DConfChangeset *changeset;

      changeset = dconf_changeset_new ();
      dconf_changeset_set (changeset, key, value);

      success = dconf_engine_change_fast (dcsb->engine, changeset, NULL, NULL);
      dconf_changeset_unref (changeset);
    }

  return success;
}

static void
dconf_settings_backend_reset (GSettingsBackend *backend,
                              const gchar      *key)
{
  dconf_settings_backend_write (backend, key, NULL);
}

static gboolean
dconf_settings_backend_is_set (GSettingsBackend *backend,
                               const gchar      *name)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) backend;

  return dconf_engine_is_set (dcsb->engine, name);
}

static gboolean
dconf_settings_backend_get_writable (GSettingsBackend *backend,
                                     const gchar      *name)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) backend;

  return dconf_engine_is_writable (dcsb->engine, name);
}

static void
dconf_settings_backend_subscribe (GSettingsBackend *backend,
                                  const gchar      *name)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) backend;

  dconf_engine_watch_fast (dcsb->engine, name);
}

static void
dconf_settings_backend_unsubscribe (GSettingsBackend *backend,
                                    const gchar      *name)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) backend;

  dconf_engine_unwatch_fast (dcsb->engine, name);
}

static void
dconf_settings_backend_sync (GSettingsBackend *backend)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) backend;

  dconf_engine_sync (dcsb->engine);
}

static GSettingsBackend *
dconf_settings_backend_delay (GSettingsBackend *backend)
{
  return g_object_new (dconf_settings_backend_get_type (), "parent", backend, NULL);
}

static void
dconf_settings_backend_apply (GSettingsBackend *backend)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) backend;
  DConfChangeset *failed_changes = NULL;

  if (dcsb->changeset == NULL)
    return;

  g_mutex_lock (&dconf_settings_backend_lock);

  if (dcsb->parent->changeset)
    dconf_changeset_apply (dcsb->parent->changeset, dcsb->changeset);

  else
    if (!dconf_engine_change_fast (dcsb->engine, dcsb->changeset, dcsb, NULL))
      /* The engine rejected the write.  Signal the issue by emitting
       * a change signal after unlocking.
       */
      failed_changes = dconf_changeset_ref (dcsb->changeset);

  dconf_changeset_unref (dcsb->changeset);
  dcsb->changeset = dconf_changeset_new ();

  g_mutex_unlock (&dconf_settings_backend_lock);

  g_assert (failed_changes == NULL); /* TODO: implement this */
}

static void
dconf_settings_backend_revert (GSettingsBackend *backend)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) backend;
  DConfChangeset *reverted_changes = NULL;

  if (dcsb->changeset == NULL)
    return;

  g_mutex_lock (&dconf_settings_backend_lock);
  reverted_changes = dcsb->changeset;
  dcsb->changeset = dconf_changeset_new ();
  g_mutex_unlock (&dconf_settings_backend_lock);

  g_assert (reverted_changes == NULL); /* TODO: implement this */
}

static void
dconf_settings_backend_free_weak_ref (gpointer data)
{
  GWeakRef *weak_ref = data;

  g_weak_ref_clear (weak_ref);
  g_slice_free (GWeakRef, weak_ref);
}

static void
dconf_settings_backend_set_property (GObject *object, guint prop_id,
                                     const GValue *value, GParamSpec *pspec)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) object;

  g_assert_cmpint (prop_id, ==, 1);

  /* "parent" is marked as a construct property which means we're
   * guaranteed to hit here during construction, even if "parent" is
   * NULL (or omitted).
   */

  dcsb->parent = g_value_dup_object (value);

  if (dcsb->parent)
    {
      dcsb->engine = dconf_engine_ref (dcsb->parent->engine);
      dconf_settings_backend_add_child (dcsb->parent, dcsb);
      dcsb->changeset = dconf_changeset_new ();
    }
  else
    {
      GWeakRef *weak_ref;

      weak_ref = g_slice_new (GWeakRef);
      g_weak_ref_init (weak_ref, dcsb);

      dcsb->engine = dconf_engine_new (weak_ref, dconf_settings_backend_free_weak_ref);
    }
}

static void
dconf_settings_backend_finalize (GObject *object)
{
  DConfSettingsBackend *dcsb = (DConfSettingsBackend *) object;

  if (dcsb->parent)
    {
      dconf_settings_backend_prune_dead_child (dcsb->parent);
      g_object_unref (dcsb->parent);
    }

  if (dcsb->changeset)
    dconf_changeset_unref (dcsb->changeset);

  g_assert (dcsb->children == NULL);

  dconf_engine_unref (dcsb->engine);

  G_OBJECT_CLASS (dconf_settings_backend_parent_class)
    ->finalize (object);
}

static void
dconf_settings_backend_init (DConfSettingsBackend *dcsb)
{
}

static void
dconf_settings_backend_class_init (GSettingsBackendClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->set_property = dconf_settings_backend_set_property;
  object_class->finalize = dconf_settings_backend_finalize;

  class->read = dconf_settings_backend_read;
  class->write = dconf_settings_backend_write;
  class->reset = dconf_settings_backend_reset;
  class->get_writable = dconf_settings_backend_get_writable;
  class->subscribe = dconf_settings_backend_subscribe;
  class->unsubscribe = dconf_settings_backend_unsubscribe;
  class->sync = dconf_settings_backend_sync;
  class->delay = dconf_settings_backend_delay;

  g_object_class_install_property (object_class, 1,
                                   g_param_spec_object ("parent", "parent backend",
                                                        "the parent backend for delayed backends",
                                                        dconf_settings_backend_get_type (), G_PARAM_STATIC_STRINGS |
                                                        G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

void
g_io_module_load (GIOModule *module)
{
  g_type_module_use (G_TYPE_MODULE (module));
  g_io_extension_point_implement (G_SETTINGS_BACKEND_EXTENSION_POINT_NAME,
                                  dconf_settings_backend_get_type (),
                                  "dconf", 100);
}

void
g_io_module_unload (GIOModule *module)
{
  g_assert_not_reached ();
}

gchar **
g_io_module_query (void)
{
  return g_strsplit (G_SETTINGS_BACKEND_EXTENSION_POINT_NAME, "!", 0);
}

static void
dconf_settings_backend_change (DConfSettingsBackend *dcsb,
                               const gchar          *prefix,
                               const gchar * const  *changes,
                               const gchar          *tag,
                               gpointer              origin_tag)
{
  GSList *children;

  /* Avoid reporting changes into delayed DConfSettingsBackend objects
   * when the changes were caused by apply() being called on that same
   * object.
   */
  if (dcsb == origin_tag)
    return;

  /* Make a local list of strong references to our children. */
  children = dconf_settings_backend_get_child_list (dcsb);

  /* Iterate our local list, reporting changes and dropping our
   * references on each and deconstructing our local list as we go...
   */
  while (children)
    {
      DConfSettingsBackend *child = children->data;

      dconf_settings_backend_change (child, prefix, changes, tag, origin_tag);
      children = g_slist_delete_link (children, children);
      g_object_unref (child);
    }

  /* Actually cause the change signals to be emitted on this backend. */
  if (changes[1] == NULL)
    {
      if (g_str_has_suffix (prefix, "/"))
        g_settings_backend_path_changed (G_SETTINGS_BACKEND (dcsb), prefix);
      else
        g_settings_backend_changed (G_SETTINGS_BACKEND (dcsb), prefix);
    }
  else
    g_settings_backend_keys_changed (G_SETTINGS_BACKEND (dcsb), prefix, changes);
}

void
dconf_engine_change_notify (DConfEngine         *engine,
                            const gchar         *prefix,
                            const gchar * const *changes,
                            const gchar         *tag,
                            gpointer             origin_tag,
                            gpointer             user_data)
{
  GWeakRef *weak_ref = user_data;
  DConfSettingsBackend *dcsb;
  GSList *children = NULL;
  GSList *node;

  /* Notifies are sent on either
   *
   *   1) the thread on which a fast write was done; or
   *
   *   2) the dconf worker thread for changes reported by the service
   *
   * In either of those cases it's possible that a thread other than
   * this one is currently calling unref() on the backend.  We use a
   * weakref to make sure that we're not reporting changes to
   * partially-dead objects.
   */
  dcsb = g_weak_ref_get (weak_ref);

  if (dcsb == NULL)
    return;

  if (changes[0] == NULL)
    return;

  dconf_settings_backend_change (dcsb, prefix, changes, tag, origin_tag);

  g_object_unref (dcsb);
}
