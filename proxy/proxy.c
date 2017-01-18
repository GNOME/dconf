/*
 * Copyright © 2016 Canonical Limited
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
 * Author: Allison Lortie <desrt@desrt.ca>
 */

#include "../gvdb/gvdb-builder.h"
#include "../client/dconf.h"
#include "permissions.h"
#include "confinement.h"

#include <sys/stat.h>

#include <stdio.h>
#include <gio/gio.h>
#include <glib-unix.h>


typedef struct _DConfProxy DConfProxy;

typedef struct {
  Permissions     permissions;
  gint            ref_count;
  gchar          *node;

  GHashTable     *locks_table;
  DConfChangeset *db0;
  DConfChangeset *db1;

  DConfProxy     *proxy; /* backref */
} Application;

typedef struct {
  gchar        *unique_name;
  Permissions   permissions;
  guint         watch_id;
  Application  *application;
} ConfinedSender;

struct _DConfProxy
{
  GDBusConnection *connection;
  guint owner_id;
  guint subtree_id;
  guint object_id;
  guint sigterm_handler;
  guint sigint_handler;
  gboolean exit_requested;

  GHashTable *applications_by_id;
  GHashTable *applications_by_node;
  GHashTable *confined_senders_by_name;

  DConfClient *client;
  gchar **locks;
};

static gboolean
contains (const gchar *a, const gchar *b)
{
  return g_str_equal (a, b) || (g_str_has_prefix (b, a) && g_str_has_suffix (a, "/"));
}

static gboolean
list_contains (const gchar * const *list,
               const gchar         *item)
{
  gint i;

  for (i = 0; list[i]; i++)
    if (contains (list[i], item))
      return TRUE;

  return FALSE;
}

static GHashTable *
make_locks_table (const gchar * const *writable,
                  const gchar * const *lockdown)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  gint i;

  /* Mark the writable paths as unlocked, but only if they are not
   * completely contained within an admin lockdown.
   */
  for (i = 0; writable[i]; i++)
    if (!list_contains (lockdown, writable[i]))
      g_hash_table_insert (table, g_strdup (writable[i]), GUINT_TO_POINTER (FALSE));

  /* For admin lockdown on paths that are inside of the writable areas,
   * add them in as more-specific locks.
   */
  for (i = 0; lockdown[i]; i++)
    if (list_contains (writable, lockdown[i]))
      g_hash_table_insert (table, g_strdup (lockdown[i]), GUINT_TO_POINTER (TRUE));

  /* Finally, if we don't have '/' explicitly unlocked, lock it. */
  if (!g_hash_table_contains (table, "/"))
    g_hash_table_insert (table, g_strdup ("/"), GUINT_TO_POINTER (TRUE));

  return table;
}

static void
dump_table (GHashTable *table)
{
  GHashTableIter iter;
  gpointer key, value;

  g_print ("Table has %d items:\n", g_hash_table_size (table));

  g_hash_table_iter_init (&iter, table);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_print ("  %s -> %s\n", (gchar *) key, value == NULL ? "false" : "true");
  g_print("\n");
}

static void
fill_table (DConfClient    *client,
            const gchar    *path,
            gboolean        is_locked,
            DConfChangeset *db0,
            DConfChangeset *db1,
            GHashTable     *locks_table)
{
  gpointer this_lock;

  if (g_hash_table_lookup_extended (locks_table, path, NULL, &this_lock))
    is_locked = GPOINTER_TO_UINT (this_lock);

  if (g_str_has_suffix (path, "/"))
    {
      g_auto(GStrv) rels;
      gint i;

      rels = dconf_client_list (client, path, NULL);

      for (i = 0; rels[i]; i++)
        {
          g_autofree gchar *full = g_strconcat (path, rels[i], NULL);
          fill_table (client, full, is_locked, db0, db1, locks_table);
        }
    }
  else
    {
      if (is_locked)
        {
          g_autoptr(GVariant) value;

          value = dconf_client_read (client, path);
          if (value)
            dconf_changeset_set (db1, path, value);
        }
      else
        {
          g_autoptr(GVariant) v0, v1;

          v0 = dconf_client_read_full (client, path, DCONF_READ_USER_VALUE, NULL);
          v1 = dconf_client_read_full (client, path, DCONF_READ_DEFAULT_VALUE, NULL);

          if (v0)
            dconf_changeset_set (db0, path, v0);

          if (v1)
            dconf_changeset_set (db1, path, v1);
        }
    }
}

static gboolean
add_key (const gchar *path,
         GVariant    *value,
         gpointer     user_data)
{
  GHashTable *gvdb = user_data;

  gvdb_item_set_value (gvdb_hash_table_insert_path (gvdb, path, '/'), value);

  return TRUE; /* continue */
}

void
dconf_gvdb_utils_write_file (const gchar     *filename,
                             DConfChangeset  *database)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GHashTable) gvdb;

  gvdb = gvdb_hash_table_new (NULL, NULL);
  dconf_changeset_all (database, add_key, gvdb);

  if (!gvdb_table_write_contents (gvdb, filename, FALSE, &error))
    g_warning ("failed to write %s: %s", filename, error->message);
}

static void
application_update_permissions (Application *self)
{
  const gchar **writable;
  const gchar **readable;
  gchar *fn;

  writable = permission_list_get_strv (&self->permissions.writable);
  readable = permission_list_get_strv (&self->permissions.readable);

  g_print ("W %s\nR %s\n", g_strjoinv(",", (gchar **)writable), g_strjoinv(",", (gchar **)readable));

  g_clear_pointer (&self->locks_table, g_hash_table_unref);
  self->locks_table = make_locks_table (writable, (const gchar **) self->proxy->locks);
  dump_table (self->locks_table);

  g_clear_pointer (&self->db0, dconf_changeset_unref);
  g_clear_pointer (&self->db1, dconf_changeset_unref);

  self->db0 = dconf_changeset_new_database (NULL);
  self->db1 = dconf_changeset_new_database (NULL);

  g_print ("W %s\nR %s\n", g_strjoinv(",", (gchar **)writable), g_strjoinv(",", (gchar **)readable));

  if(readable[0])
  fill_table (self->proxy->client, readable[0], TRUE, self->db0, self->db1, self->locks_table);

  g_print ("db0: %s\n", g_variant_print (dconf_changeset_serialise (self->db0), FALSE));
  g_print ("db1: %s\n", g_variant_print (dconf_changeset_serialise (self->db1), FALSE));

  mkdir (self->permissions.ipc_dir, 0777);

  if (!readable[0])
    return;

  fn = g_strconcat (self->permissions.ipc_dir, "/0", NULL);
  dconf_gvdb_utils_write_file (fn, self->db0);
  g_free (fn);
  fn = g_strconcat (self->permissions.ipc_dir, "/1", NULL);
  dconf_gvdb_utils_write_file (fn, self->db1);
  g_free (fn);

  g_free (writable);
  g_free (readable);
}

static void
application_unref (Application *self)
{
  self->ref_count--;

  if (self->ref_count == 0)
    {
      g_print ("Freeing app %s\n", self->permissions.app_id);

      g_hash_table_remove (self->proxy->applications_by_id, self->permissions.app_id);
      g_hash_table_remove (self->proxy->applications_by_node, self->node);

      g_clear_pointer (&self->locks_table, g_hash_table_unref);
      g_clear_pointer (&self->db0, dconf_changeset_unref);
      g_clear_pointer (&self->db1, dconf_changeset_unref);

      permissions_clear (&self->permissions);
      g_free (self->node);

      g_slice_free (Application, self);
    }
}

static void
confined_sender_vanished (GDBusConnection *connection,
                          const gchar     *name,
                          gpointer         user_data)
{
  ConfinedSender *self = user_data;

  g_assert_cmpstr (name, ==, self->unique_name);

  g_hash_table_remove (self->application->proxy->confined_senders_by_name, self->unique_name);

  if (permissions_unmerge (&self->application->permissions, &self->permissions))
    application_update_permissions (self->application);

  application_unref (self->application);

  permissions_clear (&self->permissions);
  g_bus_unwatch_name (self->watch_id);
  g_free (self->unique_name);

  g_slice_free (ConfinedSender, self);
}

static gboolean
application_can_write (const gchar *path,
                       GVariant    *value,
                       gpointer     user_data)
{
  Application *application = user_data;

  /* TODO: In dconf, resets are never supposed to fail.
   *
   * We should respond to attempts to reset paths (for example "/") by
   * resetting the list of all writable keys under that path.  Even an
   * attempt to explicitly reset a non-writable key should succeed, by
   * doing nothing.
   *
   * For now, reject these cases completely, to prevent applications
   * from resetting the user's data in other applications.
   */

  return permission_list_contains (&application->permissions.writable, path);
}

static void
dconf_proxy_endpoint_method_call (GDBusConnection       *connection,
                                  const gchar           *sender,
                                  const gchar           *object_path,
                                  const gchar           *interface_name,
                                  const gchar           *method_name,
                                  GVariant              *parameters,
                                  GDBusMethodInvocation *invocation,
                                  gpointer               user_data)
{
  Application *application = user_data;

  g_assert_cmpstr (interface_name, ==, "ca.desrt.dconf.Proxy");

  if (g_str_equal (method_name, "Start"))
    {
    }

  else if (g_str_equal (method_name, "Change"))
    {
      g_autoptr(GError) error = NULL;
      g_autoptr(GVariant) serialised = g_variant_new_from_bytes (G_VARIANT_TYPE ("a{smv}"),
                                                                 g_variant_get_data_as_bytes (parameters),
                                                                 FALSE);
      g_variant_ref_sink (serialised);

      g_autoptr(DConfChangeset) changeset = dconf_changeset_deserialise (serialised);

      /* Enforce the writability constraint */
      if (!dconf_changeset_all (changeset, application_can_write, application))
        {
          g_dbus_method_invocation_return_error_literal (invocation, DCONF_ERROR, DCONF_ERROR_NOT_WRITABLE,
                                                         "Attempt to write to keys blocked by confinement policy");
          return;
        }

      /* The write is legitimate.  Send it to dconf. */
      if (!dconf_client_change_sync (application->proxy->client, changeset, NULL, NULL, &error))
        {
          g_dbus_method_invocation_return_gerror (invocation, error);
          return;
        }

      /* Success! */
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
}

static GVariant *
dconf_proxy_endpoint_get_property (GDBusConnection  *connection,
                                   const gchar      *sender,
                                   const gchar      *object_path,
                                   const gchar      *interface_name,
                                   const gchar      *property_name,
                                   GError          **error,
                                   gpointer          user_data)
{
  Application *application = user_data;

  g_assert_cmpstr (interface_name, ==, "ca.desrt.dconf.Proxy");
  g_assert_cmpstr (property_name, ==, "Directory");

  return g_variant_new_string (application->permissions.ipc_dir);
}

static void
dconf_proxy_method_call (GDBusConnection       *connection,
                         const gchar           *sender,
                         const gchar           *object_path,
                         const gchar           *interface_name,
                         const gchar           *method_name,
                         GVariant              *parameters,
                         GDBusMethodInvocation *invocation,
                         gpointer               user_data)
{
}

static gchar *
dconf_proxy_create_node_name (const gchar *id)
{
  static int x;

  return g_strdup_printf ("%d", x++);
}

static Application *
dconf_proxy_get_application (DConfProxy  *self,
                             const gchar *id)
{
  Application *application;

  application = g_hash_table_lookup (self->applications_by_id, id);

  if (application == NULL)
    {
      application = g_slice_new0 (Application);

      permissions_init (&application->permissions);
      application->permissions.app_id = g_strdup (id);
      application->node = dconf_proxy_create_node_name (id);
      application->proxy = self;

      g_hash_table_insert (self->applications_by_id, application->permissions.app_id, application);
      g_hash_table_insert (self->applications_by_node, application->node, application);
    }

  application->ref_count++;

  return application;
}



static gboolean
dconf_proxy_get_confined_sender (DConfProxy       *self,
                                 GDBusConnection  *connection,
                                 const gchar      *sender,
                                 ConfinedSender  **out_confined_sender)
{
  g_autoptr(GVariant) reply, credentials;
  ConfinedSender *confined_sender;
  Permissions permissions;
  gboolean is_confined;

  g_assert (g_dbus_is_unique_name (sender));

  reply = g_dbus_connection_call_sync (connection,
                                       "org.freedesktop.DBus", "/",
                                       "org.freedesktop.DBus", "GetConnectionCredentials",
                                       g_variant_new ("(s)", sender),
                                       G_VARIANT_TYPE ("(a{sv})"),
                                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);

  if (reply == NULL)
    return FALSE;

  credentials = g_variant_get_child_value (reply, 0);

  if (!confinement_check (credentials, &is_confined, &permissions))
    return FALSE;

  if (!is_confined)
    {
      *out_confined_sender = NULL;
      return TRUE;
    }

  confined_sender = g_slice_new (ConfinedSender);
  confined_sender->unique_name = g_strdup (sender);
  confined_sender->permissions = permissions;
  confined_sender->application = dconf_proxy_get_application (self, permissions.app_id);
  confined_sender->watch_id = g_bus_watch_name_on_connection (connection, sender, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                              NULL, confined_sender_vanished,confined_sender, NULL);

  if (permissions_merge (&confined_sender->application->permissions, &confined_sender->permissions))
    application_update_permissions (confined_sender->application);

  g_hash_table_insert (self->confined_senders_by_name, confined_sender->unique_name, confined_sender);

  *out_confined_sender = confined_sender;

  return TRUE;
}

static gboolean
dconf_proxy_check_permissions (DConfProxy       *self,
                               GDBusConnection  *connection,
                               const gchar      *sender,
                               const gchar      *node,
                               Application     **out_application)
{
  ConfinedSender *confined_sender;
  Application *application;

  /* Find out if we have a confined sender. */
  if (!dconf_proxy_get_confined_sender (self, connection, sender, &confined_sender))
    return FALSE;

  if (confined_sender)
    {
      /* The only thing we are allowed to return here is the application
       * that belongs to this confined sender, but in case the node was
       * specified, we need to verify that it was the correct one, too.
       *
       * We can skip the hash table lookup here because we already have
       * the node string accessible directly.
       */
      if (node && !g_str_equal (node, confined_sender->application->node))
        return FALSE;

      application = confined_sender->application;
    }
  else
    {
      /* Unconfined sender.  Lookup the application by the node ID, if
       * we have it, otherwise return NULL.
       */
      if (node != NULL)
        application = g_hash_table_lookup (self->applications_by_node, node);
      else
        application = NULL;
    }

  *out_application = application;

  return TRUE;
}

static gchar **
dconf_proxy_subtree_enumerate (GDBusConnection *connection,
                               const gchar     *sender,
                               const gchar     *object_path,
                               gpointer         user_data)
{
  DConfProxy *proxy = user_data;
  Application *application;
  gchar **result;

  g_debug ("subtree enumerate: %s %s", sender, object_path);

  g_assert_cmpstr (object_path, ==, "/ca/desrt/dconf/Proxy");

  /* Security check */
  if (!dconf_proxy_check_permissions (proxy, connection, sender, NULL, &application))
    return NULL;

  if (application != NULL)
    {
      /* Specific confined application making the request */
      result = g_new (gchar *, 1 + 1);
      result[0] = g_strdup (application->node);
      result[1] = NULL;
    }
  else
    {
      /* Unconfined caller: list all existing nodes (ie: debugging) */
      gint i;

      result = (gchar **) g_hash_table_get_keys_as_array (proxy->applications_by_node, NULL);
      for (i = 0; result[i]; i++)
        result[i] = g_strdup (result[i]);
    }

  return result;
}

static GDBusInterfaceInfo *
dconf_proxy_get_proxy_interface (void)
{
  static GDBusInterfaceInfo *interface;

  if (g_once_init_enter (&interface))
    {
      g_autoptr(GDBusNodeInfo) node_info;
      GError *error = NULL;

     g_assert_no_error (error);

      g_once_init_leave (&interface, g_dbus_interface_info_ref (node_info->interfaces[0]));
    }

  return interface;
}

static GDBusInterfaceInfo *
dconf_proxy_get_endpoint_interface (void)
{
  static GDBusInterfaceInfo *interface;

  if (g_once_init_enter (&interface))
    {
      g_autoptr(GDBusNodeInfo) node_info;
      GError *error = NULL;

      node_info = g_dbus_node_info_new_for_xml ("<node>"
                                                 "<interface name='ca.desrt.dconf.Proxy.Endpoint'>"
                                                  "<property name='Directory' type='s' access='read'/>"
                                                  "<method name='Change'>"
                                                   "<arg direction='in' type='ay'/>"
                                                  "</method>"
                                                 "</interface>"
                                                "</node>", &error);
      g_assert_no_error (error);

      g_once_init_leave (&interface, g_dbus_interface_info_ref (node_info->interfaces[0]));
    }

  return interface;
}

static GDBusInterfaceInfo **
dconf_proxy_subtree_introspect (GDBusConnection *connection,
                                const gchar     *sender,
                                const gchar     *object_path,
                                const gchar     *node,
                                gpointer         user_data)
{
  DConfProxy *proxy = user_data;
  GDBusInterfaceInfo *result[2];
  Application *application;

  g_debug ("subtree introspect: %s %s %s", sender, object_path, node);

  /* GDBus bug: g_assert (g_str_equal (object_path, "/ca/desrt/dconf/Proxy")); */

  if (node != NULL)
    {
      /* They are attempting to introspect a specific endpoint.  Make
       * sure they have a right to do so.
       */
      if (!dconf_proxy_check_permissions (proxy, connection, sender, node, &application))
        return NULL;

      /* If we didn't find an application, we act as if there is no object */
      if (application == NULL)
        return NULL;

      result[0] = dconf_proxy_get_endpoint_interface ();
    }
  else
    result[0] = dconf_proxy_get_proxy_interface ();

  g_dbus_interface_info_ref (result[0]);
  result[1] = NULL;

  return g_memdup (result, sizeof result);
}

static const GDBusInterfaceVTable *
dconf_proxy_subtree_dispatch (GDBusConnection *connection,
                              const gchar     *sender,
                              const gchar     *object_path,
                              const gchar     *interface_name,
                              const gchar     *node,
                              gpointer        *out_user_data,
                              gpointer         user_data)
{
  static const GDBusInterfaceVTable vtable = {
    .method_call = dconf_proxy_endpoint_method_call,
    .get_property = dconf_proxy_endpoint_get_property
  };
  DConfProxy *proxy = user_data;
  Application *application;

  g_debug ("subtree dispatch: %s %s %s", sender, object_path, node);
  g_assert_cmpstr (object_path, ==, "/ca/desrt/dconf/Proxy");

  if (!dconf_proxy_check_permissions (proxy, connection, sender, node, &application))
    return NULL;

  if (application == NULL)
    return NULL;

  *out_user_data = application;

  return &vtable;
}

static void
dconf_proxy_bus_acquired_handler (GDBusConnection *connection,
                                  const gchar     *name,
                                  gpointer         user_data)
{
  const GDBusSubtreeVTable subtree_vtable = {
    .enumerate = dconf_proxy_subtree_enumerate,
    .introspect = dconf_proxy_subtree_introspect,
    .dispatch = dconf_proxy_subtree_dispatch
  };
  const GDBusInterfaceVTable proxy_vtable = {
    .method_call = dconf_proxy_method_call
  };
  g_autoptr(GDBusNodeInfo) node_info;
  DConfProxy *proxy = user_data;
  GError *error = NULL;

  g_debug ("acquired bus connection, unique %s", g_dbus_connection_get_unique_name (connection));

  proxy->connection = g_object_ref (connection);

  node_info = g_dbus_node_info_new_for_xml ("<node>"
                                             "<interface name='ca.desrt.dconf.Proxy'>"
                                              "<method name='Start'>"
                                               "<arg name='Endpoint' direction='out' type='o'/>"
                                               "<arg name='IpcDirectory' direction='out' type='s'/>"
                                              "</method>"
                                             "</interface>"
                                            "</node>", &error);
  g_assert_no_error (error);

  proxy->subtree_id = g_dbus_connection_register_subtree (connection, "/ca/desrt/dconf/Proxy", &subtree_vtable,
                                                          G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,
                                                          proxy, NULL, &error);
  g_assert_no_error (error);

  /*proxy->object_id = g_dbus_connection_register_object (connection, "/ca/desrt/dconf/Proxy", node_info->interfaces[0],
                                                        &proxy_vtable, proxy, NULL, &error); */
  g_assert_no_error (error);

  g_debug ("all objects successfully registered");
}

static void
dconf_proxy_name_lost_handler (GDBusConnection *connection,
                               const gchar     *name,
                               gpointer         user_data)
{
  DConfProxy *self = user_data;

  g_warning ("Unable to acquire bus name: %s.  Exiting.", name);
  self->exit_requested = TRUE;
}

static void
dconf_proxy_free (DConfProxy *self)
{
  g_debug ("freeing proxy object");

  if (g_hash_table_size (self->confined_senders_by_name) != 0)
    {
      GHashTableIter iter;
      gpointer value;

      g_warning ("Exiting proxy with the following applications connected.  Expect problems:");
      g_hash_table_iter_init (&iter, self->confined_senders_by_name);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          ConfinedSender *confined_sender = value;

          g_warning ("  %s (%s)", confined_sender->unique_name, confined_sender->permissions.app_id);
          g_hash_table_iter_remove (&iter);

          confined_sender_vanished (NULL, confined_sender->unique_name, confined_sender);
        }
    }

  if (self->object_id > 0)
    g_dbus_connection_unregister_object (self->connection, self->object_id);

  if (self->subtree_id > 0)
    g_dbus_connection_unregister_subtree (self->connection, self->subtree_id);

  g_source_remove (self->sigterm_handler);
  g_source_remove (self->sigint_handler);
  g_bus_unown_name (self->owner_id);

  g_assert_cmpint (g_hash_table_size (self->applications_by_node), ==, 0);
  g_assert_cmpint (g_hash_table_size (self->applications_by_id), ==, 0);
  g_assert_cmpint (g_hash_table_size (self->confined_senders_by_name), ==, 0);

  g_hash_table_unref (self->applications_by_node);
  g_hash_table_unref (self->applications_by_id);
  g_hash_table_unref (self->confined_senders_by_name);

  if (self->connection)
    g_object_unref (self->connection);

  g_slice_free (DConfProxy, self);
}

static gboolean
dconf_proxy_request_exit (gpointer user_data)
{
  DConfProxy *self = user_data;

  g_debug ("requested exit on signal");

  self->exit_requested = TRUE;

  return TRUE;
}

static DConfProxy *
dconf_proxy_new (void)
{
  DConfProxy *self;

  g_debug ("creating proxy object");

  self = g_slice_new0 (DConfProxy);
  self->applications_by_id = g_hash_table_new (g_str_hash, g_str_equal);
  self->applications_by_node = g_hash_table_new (g_str_hash, g_str_equal);
  self->confined_senders_by_name = g_hash_table_new (g_str_hash, g_str_equal);

  self->sigterm_handler = g_unix_signal_add (SIGTERM, dconf_proxy_request_exit, self);
  self->sigint_handler = g_unix_signal_add (SIGINT, dconf_proxy_request_exit, self);

  self->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION, "ca.desrt.dconf.Proxy", G_BUS_NAME_OWNER_FLAGS_NONE,
                                   dconf_proxy_bus_acquired_handler, NULL, dconf_proxy_name_lost_handler,
                                   self, NULL);

  self->client = dconf_client_new ();
  self->locks = dconf_client_list_locks (self->client, "/", NULL);

  return self;
}

static gboolean
dconf_proxy_wants_to_run (DConfProxy *self)
{
  return !self->exit_requested;
}

int
main (int    argc,
      char **argv)
{
  DConfProxy *proxy;

  proxy = dconf_proxy_new ();

  while (dconf_proxy_wants_to_run (proxy))
    g_main_context_iteration (NULL, TRUE);

  dconf_proxy_free (proxy);

  return 0;
}
