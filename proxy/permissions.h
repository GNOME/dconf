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

#pragma once

#include <glib.h>

typedef struct {
  GHashTable *hash_table;
} PermissionList;

typedef struct {
  gchar          *app_id;
  gchar          *ipc_dir;
  PermissionList  readable;
  PermissionList  writable;
} Permissions;

gboolean
permission_list_merge (PermissionList *self,
                       PermissionList *to_merge);

gboolean
permission_list_unmerge (PermissionList *self,
                         PermissionList *to_unmerge);

gboolean
permission_list_contains (PermissionList *self,
                          const gchar    *path);

const gchar **
permission_list_get_strv (PermissionList *self);

void
permission_list_init (PermissionList  *self,
                      gchar          **contents);

void
permission_list_clear (PermissionList *self);


void
permissions_init (Permissions *permissions);

void
permissions_clear (Permissions *permissions);

gboolean
permissions_merge (Permissions *permissions,
                   Permissions *to_merge);

gboolean
permissions_unmerge (Permissions *permissions,
                     Permissions *to_unmerge);
