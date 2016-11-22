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

#include "permissions.h"
#include "confinement.h"

#include <stdio.h>
#include <gio/gio.h>

typedef struct {
  Permissions  permissions;
  gint         ref_count;
  gchar       *node;
} Application;

typedef struct {
  gchar        *unique_name;
  Permissions   permissions;
  guint         watch_id;
  Application  *application;
} ConfinedSender;

typedef struct
{
  GHashTable *applications_by_id;
  GHashTable *applications_by_node;
  GHashTable *confined_senders_by_name;
} DConfProxy;

static DConfProxy *
dconf_proxy_get (void)
{
  static DConfProxy *the_proxy;

  if (the_proxy == NULL)
    {
      the_proxy = g_slice_new (DConfProxy);
      the_proxy->applications_by_id = g_hash_table_new (g_str_hash, g_str_equal);
      the_proxy->applications_by_node = g_hash_table_new (g_str_hash, g_str_equal);
      the_proxy->confined_senders_by_name = g_hash_table_new (g_str_hash, g_str_equal);
    }

  return the_proxy;
}

static void
confined_sender_vanished (GDBusConnection *connection,
                          const gchar     *name,
                          gpointer         user_data)
{
  ConfinedSender *self = user_data;

  g_assert_cmpstr (name, ==, self->unique_name);

  /* TODO: lookup the application and unmerge/unref */

  permissions_clear (&self->permissions);
  g_bus_unwatch_name (self->watch_id);
  g_free (self->unique_name);

  g_slice_free (ConfinedSender, self);
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

static GVariant *
dconf_proxy_get_property (GDBusConnection  *connection,
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
      application = g_slice_new (Application);

      permissions_init (&application->permissions);
      application->permissions.app_id = g_strdup (id);
      application->node = dconf_proxy_create_node_name (id);
      application->ref_count = 0;

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

  permissions_merge (&confined_sender->application->permissions, &confined_sender->permissions);

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

static GDBusInterfaceInfo **
dconf_proxy_subtree_introspect (GDBusConnection *connection,
                                const gchar     *sender,
                                const gchar     *object_path,
                                const gchar     *node,
                                gpointer         user_data)
{
  static GDBusInterfaceInfo *proxy_interface;
  DConfProxy *proxy = user_data;
  GDBusInterfaceInfo **result;
  Application *application;

  /* GDBus bug: g_assert (g_str_equal (object_path, "/ca/desrt/dconf/Proxy")); */

  /* The root node has nothing on it */
  if (node == NULL)
    return NULL;

  /* Do the permissions check */
  if (!dconf_proxy_check_permissions (proxy, connection, sender, node, &application))
    return NULL;

  /* If we didn't find an application, we act as if there is no object */
  if (application == NULL)
    return NULL;

  /* Prepare the blob */
  if (proxy_interface == NULL)
    {
      GDBusNodeInfo *node_info;
      GError *error = NULL;

      node_info = g_dbus_node_info_new_for_xml ("<node>"
                                                 "<interface name='ca.desrt.dconf.Proxy'>"
                                                  "<property name='Directory' type='s' access='read'/>"
                                                  "<method name='Start'/>"
                                                  "<method name='Write'>"
                                                   "<arg direction='in' type='ay'/>"
                                                  "</method>"
                                                 "</interface>"
                                                "</node>", &error);
      g_assert_no_error (error);

      proxy_interface = g_dbus_interface_info_ref (node_info->interfaces[0]);
      g_dbus_node_info_unref (node_info);
    }

  result = g_new (GDBusInterfaceInfo *, 1 + 1);
  result[0] = g_dbus_interface_info_ref (proxy_interface);
  result[1] = NULL;

  return result;
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
    .method_call = dconf_proxy_method_call,
    .get_property = dconf_proxy_get_property
  };
  DConfProxy *proxy = user_data;
  Application *application;

  g_assert (g_str_equal (object_path, "/ca/desrt/dconf/Proxy"));

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
  DConfProxy *proxy = user_data;
  GError *error = NULL;

  g_dbus_connection_register_subtree (connection, "/ca/desrt/dconf/Proxy", &subtree_vtable,
                                      G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,
                                      proxy, NULL, &error);
  g_assert_no_error (error);
}

static void
dconf_proxy_name_lost_handler (GDBusConnection *connection,
                               const gchar     *name,
                               gpointer         user_data)
{
  g_error ("Unable to acquire bus name: %s.  Exiting.", name);
}

int
main (int    argc,
      char **argv)
{
  DConfProxy *proxy;

  proxy = dconf_proxy_get ();

  g_bus_own_name (G_BUS_TYPE_SESSION, "ca.desrt.dconf.Proxy", G_BUS_NAME_OWNER_FLAGS_NONE,
                  dconf_proxy_bus_acquired_handler, NULL, dconf_proxy_name_lost_handler,
                  proxy, NULL);

  while (TRUE)
    g_main_context_iteration (NULL, TRUE);
}
