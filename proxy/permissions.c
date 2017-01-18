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

static gboolean
permission_list_add (PermissionList *self,
                     const gchar    *string)
{
  gsize ref_count;

  ref_count = (gsize) g_hash_table_lookup (self->hash_table, string);

  ref_count++;

  g_hash_table_insert (self->hash_table, g_strdup (string), (gpointer) ref_count);

  return ref_count == 1;
}

static gboolean
permission_list_remove (PermissionList *self,
                        const gchar    *string)
{
  gsize ref_count;

  ref_count = (gsize) g_hash_table_lookup (self->hash_table, string);
  g_assert (ref_count != 0);

  ref_count--;

  if (ref_count > 0)
    g_hash_table_insert (self->hash_table, g_strdup (string), (gpointer) ref_count);
  else
    g_hash_table_remove (self->hash_table, string);

  return ref_count == 0;
}

gboolean
permission_list_merge (PermissionList *self,
                       PermissionList *to_merge)
{
  gboolean any_changes = FALSE;
  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, to_merge->hash_table);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    any_changes |= permission_list_add (self, key);

  return any_changes;
}

gboolean
permission_list_unmerge (PermissionList *self,
                         PermissionList *to_unmerge)
{
  gboolean any_changes = FALSE;
  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, to_unmerge->hash_table);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    any_changes |= permission_list_remove (self, key);

  return any_changes;
}

static gboolean
path_contains (const gchar *a,
               const gchar *b)
{
  gint i;

  for (i = 0; b[i]; i++)
    if (a[i] != b[i])
      {
        if (a[i] == '/')
          return TRUE;

        return FALSE;
      }

  return a[i] == '\0';
}

gboolean
permission_list_contains (PermissionList *self,
                          const gchar    *path)
{
  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, self->hash_table);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    if (path_contains (key, path))
      return TRUE;

  return FALSE;
}

const gchar **
permission_list_get_strv (PermissionList *self)
{
  return (const gchar **) g_hash_table_get_keys_as_array (self->hash_table, NULL);
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
  g_clear_pointer (&self->hash_table, g_hash_table_unref);
}

void
permissions_init (Permissions *permissions)
{
  permission_list_init (&permissions->readable, NULL);
  permission_list_init (&permissions->writable, NULL);
}

void
permissions_clear (Permissions *self)
{
  g_clear_pointer (&self->app_id, g_free);
  g_clear_pointer (&self->ipc_dir, g_free);

  permission_list_clear (&self->readable);
  permission_list_clear (&self->writable);
}

static void
merge_string (gchar       **dest,
              const gchar  *src)
{
  if (*dest == NULL)
    *dest = g_strdup (src);

  g_assert_cmpstr (*dest, ==, src);
}

gboolean
permissions_merge (Permissions *permissions,
                   Permissions *to_merge)
{
  merge_string (&permissions->app_id, to_merge->app_id);
  merge_string (&permissions->ipc_dir, to_merge->ipc_dir);

  return permission_list_merge (&permissions->readable, &to_merge->readable) |
         permission_list_merge (&permissions->writable, &to_merge->writable);
}

gboolean
permissions_unmerge (Permissions *permissions,
                     Permissions *to_unmerge)
{
  return permission_list_unmerge (&permissions->readable, &to_unmerge->readable) |
         permission_list_unmerge (&permissions->writable, &to_unmerge->writable);
}
