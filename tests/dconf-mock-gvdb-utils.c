/*
 * Copyright © 2018 Daniel Playfair Cal
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
 * Author: Daniel Playfair Cal <daniel.playfair.cal@gmail.com>
 */

#include "../service/dconf-gvdb-utils.h"

#include "../common/dconf-changeset.h"

#include <glib.h>
#include <unistd.h>

static GHashTable *files = NULL;

static void
dconf_gvdb_utils_files_ensure_init (void)
{
  if (files == NULL)
    files = g_hash_table_new_full (g_str_hash,
                                   g_str_equal,
                                   g_free,
                                   (void (*)(void *)) dconf_changeset_unref);
}

DConfChangeset *
dconf_gvdb_utils_read_file (const gchar  *filename,
                            gboolean     *file_missing,
                            GError      **error)
{
  DConfChangeset *contents;
  dconf_gvdb_utils_files_ensure_init ();
  contents = g_hash_table_lookup (files, filename);
  if (contents == NULL)
  {
    *file_missing = TRUE;
    contents = dconf_changeset_new_database (NULL);
  }
  return contents;
}

gboolean
dconf_gvdb_utils_write_file (const gchar     *filename,
                             DConfChangeset  *database,
                             GError         **error)
{
  dconf_gvdb_utils_files_ensure_init ();
  DConfChangeset *copy = dconf_changeset_new_database (database);
  g_hash_table_replace (files,
                        g_strdup (filename),
                        (gpointer *) copy);
  return TRUE;
}

int
dconf_gvdb_utils_open (const char *name, int flags)
{
  return 42;
}

int
dconf_gvdb_utils_close (int fd)
{
  return 0;
}

ssize_t
dconf_gvdb_utils_write (int fd, const void *buf, size_t n)
{
  return n;
}
