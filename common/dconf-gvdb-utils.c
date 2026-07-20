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

#include "dconf-gvdb-utils.h"

#include "./dconf-paths.h"
#include "gvdb/gvdb-builder.h"
#include "gvdb/gvdb-reader.h"

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

DConfChangeset *
dconf_gvdb_utils_changeset_from_table (GvdbTable *table)
{
  DConfChangeset *database = dconf_changeset_new_database (NULL);
  gchar **names;
  gsize n_names;
  gsize i;

  names = gvdb_table_get_names (table, &n_names);
  for (i = 0; i < n_names; i++)
    {
      if (dconf_is_key (names[i], NULL))
        {
          GVariant *value;

          value = gvdb_table_get_value (table, names[i]);

          if (value != NULL)
            {
              dconf_changeset_set (database, names[i], value);
              g_variant_unref (value);
            }
        }

      g_free (names[i]);
    }

  g_free (names);
  return database;
}

#include <fcntl.h>
#include <unistd.h>

#define DCONF_GVDB_UTILS_ESTALE_RETRY_TIMEOUT_USEC (G_USEC_PER_SEC)
#define DCONF_GVDB_UTILS_ESTALE_INITIAL_DELAY_USEC 5000
#define DCONF_GVDB_UTILS_ESTALE_MAX_DELAY_USEC     100000


static gboolean
dconf_gvdb_utils_set_file_error (const gchar  *filename,
                                 gint          saved_errno,
                                 GError      **error)
{
  g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
               "%s: %s", filename, g_strerror (saved_errno));

  return FALSE;
}

static gboolean
dconf_gvdb_utils_read_file_once (const gchar  *filename,
                                 gchar       **contents,
                                 gsize        *size,
                                 gint         *saved_errno,
                                 GError      **error)
{
  gint fd;

  *contents = NULL;
  *size = 0;
  *saved_errno = 0;

  do
    fd = g_open (filename, O_RDONLY | O_CLOEXEC, 0);
  while (fd == -1 && errno == EINTR);

  if (fd == -1)
    {
      *saved_errno = errno;

      return dconf_gvdb_utils_set_file_error (filename, *saved_errno, error);
    }
  else
    {
      GString *data;
      guint8   buffer[4096];

      data = g_string_new (NULL);

      for (;;)
        {
          gssize bytes_read;

          bytes_read = read (fd, buffer, sizeof buffer);
          if (bytes_read > 0)
            {
              g_string_append_len (data, (const gchar *) buffer, bytes_read);
            }
          else if (bytes_read == 0)
            {
              break;
            }
          else if (errno != EINTR)
            {
              *saved_errno = errno;
              close (fd);
              g_string_free (data, TRUE);

              return dconf_gvdb_utils_set_file_error (filename, *saved_errno, error);
            }
        }

      close (fd);

      *size = data->len;
      *contents = g_string_free_and_steal (data);

      return TRUE;
    }
}

static gboolean
dconf_gvdb_utils_read_file (const gchar  *filename,
                            gchar       **contents,
                            gsize        *size,
                            GError      **error)
{
  GError *local_error = NULL;
  gint saved_errno = 0;
  gint64 estale_retry_deadline = 0;
  guint estale_retry_delay = DCONF_GVDB_UTILS_ESTALE_INITIAL_DELAY_USEC;

  while (!dconf_gvdb_utils_read_file_once (filename, contents, size,
                                           &saved_errno, &local_error))
    {
#ifdef ESTALE
      if (saved_errno == ESTALE)
        {
          gint64 now = g_get_monotonic_time ();

          if (estale_retry_deadline == 0)
            estale_retry_deadline = now + DCONF_GVDB_UTILS_ESTALE_RETRY_TIMEOUT_USEC;

          if (now < estale_retry_deadline)
            {
              gint64 remaining_usec = estale_retry_deadline - now;
              guint delay_usec = MIN (estale_retry_delay, remaining_usec);

              g_clear_error (&local_error);
              g_usleep (delay_usec);

              estale_retry_delay = MIN (estale_retry_delay * 2,
                                        DCONF_GVDB_UTILS_ESTALE_MAX_DELAY_USEC);
              continue;
            }
        }
#endif

      g_propagate_error (error, local_error);

      return FALSE;
    }

  return TRUE;
}

DConfChangeset *
dconf_gvdb_utils_read_and_back_up_file (const gchar  *filename,
                                        gboolean     *file_missing,
                                        GError      **error)
{
  DConfChangeset *database;
  GError *my_error = NULL;
  GvdbTable *table = NULL;
  gchar *contents;
  gsize size;

  if (dconf_gvdb_utils_read_file (filename, &contents, &size, &my_error))
    {
      GBytes *bytes;

      bytes = g_bytes_new_take (contents, size);
      table = gvdb_table_new_from_bytes (bytes, FALSE, &my_error);
      g_bytes_unref (bytes);
    }

  /* It is perfectly fine if the file does not exist -- then it's
   * just empty.
   */
  if (g_error_matches (my_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
    g_clear_error (&my_error);

  /* Otherwise, we should report errors to prevent ourselves from
   * overwriting the database in other situations...
   */
  if (g_error_matches (my_error, G_FILE_ERROR, G_FILE_ERROR_INVAL))
    {
      /* Move the database to a backup file, warn and continue with a new
       * database. The alternative is erroring out and exiting the daemon,
       * which leaves the user’s session essentially unusable.
       *
       * The code to find an unused backup filename is racy, but this is an
       * error handling path. Who cares. */
      g_autofree gchar *backup_filename = NULL;
      guint i;

      for (i = 0;
           i < G_MAXUINT &&
           (backup_filename == NULL || g_file_test (backup_filename, G_FILE_TEST_EXISTS));
           i++)
        {
          g_free (backup_filename);
          backup_filename = g_strdup_printf ("%s~%u", filename, i);
        }

      if (g_rename (filename, backup_filename) != 0)
        g_warning ("Error renaming corrupt database from ‘%s’ to ‘%s’: %s",
                   filename, backup_filename, g_strerror (errno));
      else
        g_warning ("Database ‘%s’ was corrupt: moved it to ‘%s’ and created an empty replacement",
                   filename, backup_filename);

      g_clear_error (&my_error);
    }
  else if (my_error)
    {
      g_propagate_prefixed_error (error, my_error, "Cannot open dconf database: ");
      return NULL;
    }

  /* Fill the table up with the initial state */
  if (table != NULL)
    {
      database = dconf_gvdb_utils_changeset_from_table (table);
      gvdb_table_free (table);
    }
  else
    database = dconf_changeset_new_database (NULL);

  if (file_missing)
    *file_missing = (table == NULL);

  return database;
}

static GvdbItem *
dconf_gvdb_utils_get_parent (GHashTable  *table,
                             const gchar *key)
{
  GvdbItem *grandparent, *parent;
  gchar *parent_name;
  gint len;

  if (g_str_equal (key, "/"))
    return NULL;

  len = strlen (key);
  if (key[len - 1] == '/')
    len--;

  while (key[len - 1] != '/')
    len--;

  parent_name = g_strndup (key, len);
  parent = g_hash_table_lookup (table, parent_name);

  if (parent == NULL)
    {
      parent = gvdb_hash_table_insert (table, parent_name);

      grandparent = dconf_gvdb_utils_get_parent (table, parent_name);

      if (grandparent != NULL)
        gvdb_item_set_parent (parent, grandparent);
    }

  g_free (parent_name);

  return parent;
}

static gboolean
dconf_gvdb_utils_add_key (const gchar *path,
                          GVariant    *value,
                          gpointer     user_data)
{
  GHashTable *gvdb = user_data;
  GvdbItem *item;

  g_assert (g_hash_table_lookup (gvdb, path) == NULL);
  item = gvdb_hash_table_insert (gvdb, path);
  gvdb_item_set_parent (item, dconf_gvdb_utils_get_parent (gvdb, path));
  gvdb_item_set_value (item, value);

  return TRUE;
}

GHashTable *
dconf_gvdb_utils_table_from_changeset (DConfChangeset *database)
{
  GHashTable *table;

  table = gvdb_hash_table_new (NULL, NULL);
  dconf_changeset_all (database, dconf_gvdb_utils_add_key, table);
  return table;
}

gboolean
dconf_gvdb_utils_write_file (const gchar     *filename,
                             DConfChangeset  *database,
                             GError         **error)
{
  GHashTable *gvdb;
  gboolean success;

  gvdb = dconf_gvdb_utils_table_from_changeset (database);
  success = gvdb_table_write_contents (gvdb, filename, FALSE, error);

  if (!success)
    {
      gchar *dirname;

      /* Maybe it failed because the directory doesn't exist.  Try
       * again, after mkdir().
       */
      dirname = g_path_get_dirname (filename);
      g_mkdir_with_parents (dirname, 0700);
      g_free (dirname);

      g_clear_error (error);
      success = gvdb_table_write_contents (gvdb, filename, FALSE, error);
    }

  g_hash_table_unref (gvdb);

  return success;
}
