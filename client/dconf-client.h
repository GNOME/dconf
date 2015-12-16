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

#ifndef __dconf_client_h__
#define __dconf_client_h__

#include <gio/gio.h>
#include "../common/dconf-changeset.h"
#include "../common/dconf-enums.h"

G_BEGIN_DECLS

#define DCONF_TYPE_CLIENT       (dconf_client_get_type ())
G_DECLARE_FINAL_TYPE(DConfClient, dconf_client, DCONF, CLIENT, GObject)

DConfClient *           dconf_client_new                                (void);

GVariant *              dconf_client_read                               (DConfClient          *client,
                                                                         const gchar          *key);

GVariant *              dconf_client_read_full                          (DConfClient          *client,
                                                                         const gchar          *key,
                                                                         DConfReadFlags        flags,
                                                                         const GQueue         *read_through);

gchar **                dconf_client_list                               (DConfClient          *client,
                                                                         const gchar          *dir,
                                                                         gint                 *length);

gchar **                dconf_client_list_locks                         (DConfClient          *client,
                                                                         const gchar          *dir,
                                                                         gint                 *length);

gboolean                dconf_client_is_writable                        (DConfClient          *client,
                                                                         const gchar          *key);

gboolean                dconf_client_write_fast                         (DConfClient          *client,
                                                                         const gchar          *key,
                                                                         GVariant             *value,
                                                                         GError              **error);
gboolean                dconf_client_write_sync                         (DConfClient          *client,
                                                                         const gchar          *key,
                                                                         GVariant             *value,
                                                                         gchar               **tag,
                                                                         GCancellable         *cancellable,
                                                                         GError              **error);

gboolean                dconf_client_change_fast                        (DConfClient          *client,
                                                                         DConfChangeset       *changeset,
                                                                         GError              **error);
gboolean                dconf_client_change_sync                        (DConfClient          *client,
                                                                         DConfChangeset       *changeset,
                                                                         gchar               **tag,
                                                                         GCancellable         *cancellable,
                                                                         GError              **error);

void                    dconf_client_watch_fast                         (DConfClient          *client,
                                                                         const gchar          *path);
void                    dconf_client_watch_sync                         (DConfClient          *client,
                                                                         const gchar          *path);

void                    dconf_client_unwatch_fast                       (DConfClient          *client,
                                                                         const gchar          *path);
void                    dconf_client_unwatch_sync                       (DConfClient          *client,
                                                                         const gchar          *path);

void                    dconf_client_sync                               (DConfClient          *client);


G_END_DECLS

#endif /* __dconf_client_h__ */
