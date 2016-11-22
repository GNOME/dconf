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

void
permission_list_add (PermissionList *self,
                     const gchar    *string)
{
  gsize current;

  current = (gsize) g_hash_table_lookup (self->hash_table, string);
  g_hash_table_insert (self->hash_table, g_strdup (string), (gpointer) (current + 1));
}

void
permission_list_remove (PermissionList *self,
                        const gchar    *string)
{
  gsize current;

  current = (gsize) g_hash_table_lookup (self->hash_table, string);
  g_assert (current != 0);

  if (current > 1)
    g_hash_table_insert (self->hash_table, g_strdup (string), (gpointer) (current - 1));
  else
    g_hash_table_remove (self->hash_table, string);
}

void
permission_list_merge (PermissionList *self,
                       PermissionList *to_merge)
{
  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, to_merge->hash_table);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    permission_list_add (self, key);
}

void
permission_list_unmerge (PermissionList *self,
                         PermissionList *to_unmerge)
{
  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, to_unmerge->hash_table);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    permission_list_remove (self, key);
}

void
permission_list_init (PermissionList  *self,
                      gchar          **contents)
{
  self->hash_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  if (contents != NULL)
    {
      gint i;

      for (i = 0; contents[i]; i++)
        g_hash_table_insert (self->hash_table, contents[i], (gpointer) 1u);

      g_free (contents);
    }
}

void
permission_list_clear (PermissionList *self)
{
  g_hash_table_unref (self->hash_table);
  self->hash_table = NULL;
}

void
permissions_init (Permissions *permissions)
{
  permission_list_init (&permissions->readable, NULL);
  permission_list_init (&permissions->writable, NULL);
}

void
permissions_clear (Permissions *permissions)
{
  permission_list_clear (&permissions->readable);
  permission_list_clear (&permissions->writable);
}

static void
merge_string (gchar       **dest,
              const gchar  *src)
{
  if (*dest == NULL)
    *dest = g_strdup (src);

  g_assert_cmpstr (*dest, ==, src);
}

void
permissions_merge (Permissions *permissions,
                   Permissions *to_merge)
{
  merge_string (&permissions->app_id, to_merge->app_id);
  merge_string (&permissions->ipc_dir, to_merge->ipc_dir);

  permission_list_merge (&permissions->readable, &to_merge->readable);
  permission_list_merge (&permissions->writable, &to_merge->writable);
}

void
permissions_unmerge (Permissions *permissions,
                     Permissions *to_unmerge)
{
  permission_list_unmerge (&permissions->readable, &to_unmerge->readable);
  permission_list_unmerge (&permissions->writable, &to_unmerge->writable);
}

