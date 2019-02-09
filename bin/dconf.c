/*
 * Copyright © 2010, 2011 Codethink Limited
 * Copyright © 2011 Canonical Limited
 * Copyright © 2018 Tomasz Miąsko
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
 *         Tomasz Miąsko
 */

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "client/dconf-client.h"
#include "common/dconf-enums.h"
#include "common/dconf-paths.h"
#include "gvdb/gvdb-builder.h"
#include "gvdb/gvdb-reader.h"

static gboolean dconf_help (const gchar **argv, GError **error);

static gboolean
option_error_propagate (GError **dst, GError **src)
{
  g_assert (src != NULL && *src != NULL);

  (*src)->domain = G_OPTION_ERROR;
  (*src)->code = G_OPTION_ERROR_FAILED;
  g_propagate_error (dst, g_steal_pointer (src));

  return FALSE;
}

static gboolean
option_error_set (GError **error, const char *message)
{
  g_assert (error != NULL);
  g_assert (message != NULL);

  g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, message);

  return FALSE;
}

static gboolean
dconf_read (const gchar **argv,
            GError      **error)
{
  gint index = 0;
  const gchar *key;
  DConfReadFlags flags = DCONF_READ_FLAGS_NONE;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(DConfClient) client = NULL;
  g_autoptr(GVariant) result = NULL;

  if (argv[index] != NULL && strcmp (argv[index], "-d") == 0)
    {
      flags = DCONF_READ_DEFAULT_VALUE;
      index += 1;
    }

  key = argv[index];
  if (!dconf_is_key (key, &local_error))
    return option_error_propagate (error, &local_error);

  index += 1;

  if (argv[index] != NULL)
    return option_error_set (error, "too many arguments");

  client = dconf_client_new ();
  result = dconf_client_read_full (client, key, flags, NULL);

  if (result != NULL)
    {
      g_autofree gchar *s = g_variant_print (result, TRUE);
      g_printf ("%s\n", s);
    }

  return TRUE;
}

static gint
string_compare (const void *a,
                const void *b)
{
  return strcmp (*(const gchar **)a, *(const gchar **)b);
}

static gint
string_rcompare (const void *a,
                 const void *b)
{
  return -strcmp (*(const gchar **)a, *(const gchar **)b);
}

static gboolean
dconf_list (const gchar **argv,
            GError      **error)
{
  const char *dir;
  gint length;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(DConfClient) client = NULL;
  g_auto(GStrv) items = NULL;

  dir = argv[0];
  if (!dconf_is_dir (dir, &local_error))
    return option_error_propagate (error, &local_error);

  client = dconf_client_new ();
  items = dconf_client_list (client, dir, &length);
  qsort (items, length, sizeof (items[0]), string_compare);

  for (char **item = items; *item; ++item)
    g_printf ("%s\n", *item);

  return TRUE;
}

static gboolean
dconf_list_locks (const gchar **argv,
                  GError      **error)
{
  const char *dir;
  gint length;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(DConfClient) client = NULL;
  g_auto(GStrv) items = NULL;

  dir = argv[0];
  if (!dconf_is_dir (dir, &local_error))
    return option_error_propagate (error, &local_error);

  if (argv[1] != NULL)
    return option_error_set (error, "too many arguments");

  client = dconf_client_new ();
  items = dconf_client_list_locks (client, dir, &length);
  qsort (items, length, sizeof (items[0]), string_compare);

  for (char **item = items; *item; ++item)
    g_printf ("%s\n", *item);

  return TRUE;
}

static gboolean
dconf_write (const gchar  **argv,
             GError       **error)
{
  const char *key;
  const char *value_str;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GVariant) value = NULL;
  g_autoptr(DConfClient) client = NULL;

  key = argv[0];
  if (!dconf_is_key (key, &local_error))
    return option_error_propagate (error, &local_error);

  value_str = argv[1];
  if (value_str == NULL)
    return option_error_set (error, "value not specified");

  value = g_variant_parse (NULL, value_str, NULL, NULL, &local_error);
  if (value == NULL)
    return option_error_propagate (error, &local_error);

  if (argv[2] != NULL)
    return option_error_set (error, "too many arguments");

  client = dconf_client_new ();
  return dconf_client_write_sync (client, key, value, NULL, NULL, error);
}

static gboolean
dconf_reset (const gchar  **argv,
             GError       **error)
{
  gboolean force = FALSE;
  gint index = 0;
  const gchar *path;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(DConfClient) client = NULL;

  if (argv[index] != NULL && strcmp (argv[index], "-f") == 0)
    {
      index += 1;
      force = TRUE;
    }

  path = argv[index];
  if (!dconf_is_path (path, &local_error))
    return option_error_propagate (error, &local_error);

  index += 1;

  if (dconf_is_dir (path, NULL) && !force)
    return option_error_set (error, "-f must be given to (recursively) reset entire directories");

  if (argv[index] != NULL)
    return option_error_set (error, "too many arguments");

  client = dconf_client_new ();
  return dconf_client_write_sync (client, path, NULL, NULL, NULL, error);
}

static void
show_path (DConfClient *client, const gchar *path)
{
  if (dconf_is_key (path, NULL))
    {
      g_autoptr(GVariant) value = NULL;
      g_autofree gchar *value_str = NULL;

      value = dconf_client_read (client, path);

      if (value != NULL)
        value_str = g_variant_print (value, TRUE);

      g_printf ("  %s\n", value_str != NULL ? value_str : "unset");
    }
}

static void
watch_function (DConfClient  *client,
                const gchar  *path,
                const gchar **items,
                const gchar  *tag,
                gpointer      user_data)
{
  for (const gchar **item = items; *item; ++item)
    {
      g_autofree gchar *full = NULL;

      full = g_strconcat (path, *item, NULL);
      g_printf ("%s\n", full);
      show_path (client, full);
    }

  g_printf ("\n");
  fflush (stdout);
}

static gboolean
dconf_watch (const char **argv,
             GError     **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(DConfClient) client = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  const gchar *path;

  path = argv[0];
  if (!dconf_is_path (path, &local_error))
    return option_error_propagate (error, &local_error);

  if (argv[1] != NULL)
    return option_error_set (error, "too many arguments");

  client = dconf_client_new ();
  g_signal_connect (client, "changed", G_CALLBACK (watch_function), NULL);
  dconf_client_watch_sync (client, path);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  return TRUE;
}

static gboolean
dconf_blame (const char **argv,
             GError     **error)
{
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GVariant) child = NULL;
  g_autoptr(GDBusConnection) connection = NULL;

  if (argv[0] != NULL)
    return option_error_set (error, "too many arguments");

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
  if (connection == NULL)
    return FALSE;

  reply = g_dbus_connection_call_sync (connection, "ca.desrt.dconf", 
                                       "/ca/desrt/dconf", 
                                       "ca.desrt.dconf.ServiceInfo",
                                       "Blame", NULL, G_VARIANT_TYPE ("(s)"),
                                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);
  if (reply == NULL)
    return FALSE;

  child = g_variant_get_child_value (reply, 0);
  g_printf ("%s", g_variant_get_string (child, NULL));

  return TRUE;
}

/**
 * Returns a parent dir that contains given path.
 */
static gchar *
path_get_parent (const char *path)
{
  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (strcmp (path, "/") != 0, NULL);

  gsize last = 0;

  /* Find the position of the last slash, other than the trailing one. */
  for (gsize i = 0; path[i + 1] != '\0'; ++i)
    if (path[i] == '/')
      last = i;

  return strndup (path, last + 1);
}

static gboolean
dconf_complete (const gchar **argv,
                GError      **error)
{
  const gchar *suffix;
  const gchar *path;

  suffix = argv[0];
  if (suffix == NULL)
    return option_error_set (error, "suffix not specified");

  path = argv[1];
  if (path == NULL)
    return option_error_set (error, "path not specified");

  if (argv[2] != NULL)
    return option_error_set (error, "too many arguments");

  if (g_str_equal (path, ""))
    {
      g_printf ("/\n");
      return TRUE;
    }

  if (path[0] == '/')
    {
      gint length;
      g_autoptr(DConfClient) client = NULL;
      g_autofree gchar *dir = NULL;
      g_auto(GStrv) items = NULL;

      if (g_str_has_suffix (path, "/"))
        dir = g_strdup (path);
      else
        dir = path_get_parent (path);

      client = dconf_client_new ();
      items = dconf_client_list (client, dir, &length);
      qsort (items, length, sizeof (items[0]), string_compare);

      for (gchar **item = items; *item; ++item)
        {
          g_autofree gchar *full_item = NULL;

          full_item = g_strconcat (dir, *item, NULL);
          if (g_str_has_prefix (full_item, path) &&
              g_str_has_suffix (*item, suffix))
            {
              g_printf ("%s%s\n", full_item,
                        g_str_has_suffix (full_item, "/") ? "" : " ");
            }
        }
    }

  return TRUE;
}

/**
 * Comparison function for paths that orders keys before dirs.
 */
static gint
path_compare (const void *a,
              const void *b)
{
  const gchar *as = *(const gchar **)a;
  const gchar *bs = *(const gchar **)b;

  const gboolean a_is_dir = !!g_str_has_suffix (as, "/");
  const gboolean b_is_dir = !!g_str_has_suffix (bs, "/");

  if (a_is_dir != b_is_dir)
    return a_is_dir - b_is_dir;
  else
    return strcmp (as, bs);
}

/**
 * add_to_keyfile:
 * @dir_src: a dconf source dir
 * @dir_dst: a key-file destination dir
 *
 * Copy directory contents from dconf to key-file.
 **/
static void 
add_to_keyfile (GKeyFile    *kf,
                DConfClient *client,
                const gchar *dir_src,
                const gchar *dir_dst)
{
  g_autofree gchar *group = NULL;
  g_auto(GStrv) items = NULL;
  gint length;
  gsize n;

  /* Key-file group names are formed by removing initial and trailing slash
   * from dir name, with the singular exception of root dir whose group name
   * is just "/". */

  n = strlen (dir_dst);
  g_assert (n >= 1 && dir_dst[n - 1] == '/');

  if (g_str_equal (dir_dst, "/"))
    group = g_strdup ("/");
  else
    group = g_strndup (dir_dst + 1, n - 2);

  items = dconf_client_list (client, dir_src, &length);
  qsort (items, length, sizeof (items[0]), path_compare);

  for (gchar **item = items; *item; ++item)
    {
      g_autofree gchar *path = g_strconcat (dir_src, *item, NULL);

      if (g_str_has_suffix (*item, "/"))
        {
          g_autofree gchar *subdir = g_strconcat (dir_dst, *item, NULL);
          add_to_keyfile (kf, client, path, subdir);
        }
      else
        {
          g_autoptr(GVariant) value = dconf_client_read (client, path);
          if (value != NULL)
            {
              g_autofree gchar *value_str = g_variant_print (value, TRUE);
              g_key_file_set_value (kf, group, *item, value_str);
            }
        }
    }
}

static gboolean
dconf_dump (const gchar **argv,
            GError      **error)
{
  const gchar *dir;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GKeyFile) kf = NULL;
  g_autoptr(DConfClient) client = NULL;
  g_autofree gchar *data = NULL;

  dir = argv[0];
  if (!dconf_is_dir (dir, &local_error))
    return option_error_propagate (error, &local_error);

  if (argv[1] != NULL)
    return option_error_set (error, "too many arguments");

  kf = g_key_file_new ();
  client = dconf_client_new ();

  add_to_keyfile (kf, client, dir, "/");

  data = g_key_file_to_data (kf, NULL, NULL);
  g_printf ("%s", data);

  return TRUE;
}

static GKeyFile *
keyfile_from_stdin (GError **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  char buffer[1024];
  g_autoptr(GString) s = NULL;
  g_autoptr(GKeyFile) kf = NULL;

  s = g_string_new (NULL);
  while (fgets (buffer, sizeof (buffer), stdin) != NULL)
    g_string_append (s, buffer);

  kf = g_key_file_new ();
  if (!g_key_file_load_from_data (kf, s->str, s->len, G_KEY_FILE_NONE, error))
    return FALSE;

  return g_steal_pointer (&kf);
}

typedef void (*KeyFileForeachFunc) (const gchar *path,
                                    GVariant    *value,
                                    gpointer     user_data);

static gboolean
keyfile_foreach (GKeyFile           *kf,
                 const gchar        *dir,
                 KeyFileForeachFunc  func,
                 gpointer            user_data,
                 GError            **error)
{
  g_auto(GStrv) groups = NULL;

  groups = g_key_file_get_groups (kf, NULL);

  for (gchar **group = groups; *group; ++group)
    {
      g_auto(GStrv) keys = NULL;

      keys = g_key_file_get_keys (kf, *group, NULL, NULL);

      for (gchar **key = keys; *key; ++key)
        {
          g_autoptr(GString) s = NULL;
          g_autofree gchar *value_str = NULL;
          g_autoptr(GVariant) value = NULL;

          /* Reconstruct dconf key path from the current dir,
           * key-file group name and key-file key. */
          s = g_string_new (dir);
          if (strcmp (*group, "/") != 0)
            {
              g_string_append (s, *group);
              g_string_append (s, "/");
            }
          g_string_append (s, *key);

          if (!dconf_is_key (s->str, error))
            {
              g_prefix_error (error, "[%s]: %s: invalid path: ",
                              *group, *key);
              return FALSE;
            }

          value_str = g_key_file_get_value (kf, *group, *key, NULL);
          g_assert (value_str != NULL);

          value = g_variant_parse (NULL, value_str, NULL, NULL, error);
          if (value == NULL)
            {
              g_prefix_error (error, "[%s]: %s: invalid value: %s: ",
                              *group, *key, value_str);
              return FALSE;
            }

          func (s->str, value, user_data);
        }
    }

  return TRUE;
}

typedef struct {
  DConfClient    *client;
  DConfChangeset *changeset;
  gboolean        force;
} LoadContext;

static void
changeset_set (const gchar *path,
               GVariant    *value,
               gpointer     user_data)
{
  LoadContext *ctx = user_data;

  /* When force option is used, ignore changes made to non-writeable keys to
   * avoid rejecting the whole changeset.
   */
  if (ctx->force && !dconf_client_is_writable (ctx->client, path))
    {
      g_fprintf (stderr, "warning: ignored non-writable key '%s'\n", path);
      return;
    }

  dconf_changeset_set (ctx->changeset, path, value);
}

static gboolean
dconf_load (const gchar **argv,
            GError      **error)
{
  const gchar *dir;
  gint index = 0;
  gboolean force = FALSE;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GKeyFile) kf = NULL;
  g_autoptr(DConfChangeset) changeset = NULL;
  g_autoptr (DConfClient) client = NULL;

  if (argv[index] != NULL && strcmp (argv[index], "-f") == 0)
    {
      force = TRUE;
      index += 1;
    }

  dir = argv[index];
  if (!dconf_is_dir (dir, &local_error))
    return option_error_propagate (error, &local_error);

  index += 1;

  if (argv[index] != NULL)
    return option_error_set (error, "too many arguments");

  kf = keyfile_from_stdin (error);
  if (kf == NULL)
    return FALSE;

  client = dconf_client_new ();
  changeset = dconf_changeset_new ();

  LoadContext ctx = { client, changeset, force };
  if (!keyfile_foreach (kf, dir, changeset_set, &ctx, error))
    return FALSE;

  return dconf_client_change_sync (client, changeset, NULL, NULL, error);
}

static GPtrArray *
list_directory (const gchar *dirname,
                mode_t       ftype,
                GError     **error)
{
  const gchar *name;
  g_autoptr(GDir) dir = NULL;
  g_autoptr(GPtrArray) files = NULL;

  dir = g_dir_open (dirname, 0, error);
  if (dir == NULL)
    return NULL;

  files = g_ptr_array_new_full (0, g_free);

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      GStatBuf buf;
      g_autofree gchar *filename = NULL;

      /* Ignore swap files like .swp etc. */
      if (g_str_has_prefix (name, "."))
        continue;

      filename = g_build_filename (dirname, name, NULL);

      if (g_stat (filename, &buf) < 0)
        {
          gint saved_errno = errno;
          g_debug ("ignoring file %s: %s",
                   filename, g_strerror (saved_errno));
          continue;
        }

      if ((buf.st_mode & S_IFMT) != ftype)
        continue;

      g_ptr_array_add (files, g_steal_pointer (&filename));
    }

  return g_steal_pointer (&files);
}

static GHashTable *
read_locks_directory (const gchar  *dirname,
                      GError      **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GPtrArray) files = NULL;
  g_autoptr(GHashTable) table = NULL;

  files = list_directory (dirname, S_IFREG, &local_error);
  if (files == NULL)
    {
      /* If locks directory is missing, there are just no locks... */
      if (!g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_propagate_error (error, g_steal_pointer (&local_error));
      return NULL;
    }

  table = gvdb_hash_table_new (NULL, NULL);

  for (guint i = 0; i != files->len; ++i)
    {
      const gchar *filename;
      g_autofree gchar *contents = NULL;
      g_auto(GStrv) lines = NULL;
      gsize length;

      filename = g_ptr_array_index (files, i);

      if (!g_file_get_contents (filename, &contents, &length, error))
        return NULL;

      lines = g_strsplit (contents, "\n", 0);
      for (gchar **line = lines; *line; ++line)
        {
          if (g_str_has_prefix (*line, "/"))
            gvdb_hash_table_insert_string (table, *line, "");
        }
    }

  return g_steal_pointer (&table);
}

static GvdbItem *
table_get_parent (GHashTable  *table,
                  const gchar *name)
{
  GvdbItem *parent = NULL;
  g_autofree gchar *dir = NULL;

  dir = path_get_parent (name);
  parent = g_hash_table_lookup (table, dir);

  if (parent == NULL)
    {
      parent = gvdb_hash_table_insert (table, dir);
      gvdb_item_set_parent (parent, table_get_parent (table, dir));
    }

 return parent;
}


static void
table_insert (const gchar *path,
              GVariant    *value,
              gpointer     user_data)
{
  GHashTable *table = user_data;
  GvdbItem *item;

  /* See FILES-PRECEDENCE 2 */
  if (g_hash_table_lookup (table, path) != NULL)
    return;

  item = gvdb_hash_table_insert (table, path);
  gvdb_item_set_parent (item, table_get_parent (table, path));
  gvdb_item_set_value (item, value);
}

static GHashTable *
read_directory (const gchar  *dir,
                GError      **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GHashTable) table = NULL;
  g_autoptr(GPtrArray) files = NULL;
  g_autofree gchar *locks_dir = NULL;
  GHashTable *locks_table = NULL;

  table = gvdb_hash_table_new (NULL, NULL);
  gvdb_hash_table_insert (table, "/");

  files = list_directory (dir, S_IFREG, error);
  if (files == NULL)
    return NULL;

  /* FILES-PRECEDENCE: When a path is found in multiple files, value from the
   * file lexicographically latest takes precedence.  This is achieved by 1)
   * processing files in reversed lexicographical order, 2) not overwriting
   * existing paths.
   */
  g_ptr_array_sort (files, string_rcompare);

  for (guint i = 0; i != files->len; ++i)
    {
      const gchar *filename;
      g_autoptr(GKeyFile) kf = NULL;

      filename = g_ptr_array_index (files, i);
      kf = g_key_file_new ();

      g_debug ("loading key-file: %s", filename);

      if (!g_key_file_load_from_file (kf, filename, G_KEY_FILE_NONE, error))
        {
          g_autofree gchar *display_name = g_filename_display_basename (filename);
          g_prefix_error (error, "%s: ", display_name);
          return FALSE;
        }

      if (!keyfile_foreach (kf, "/", table_insert, table, error))
        {
          g_autofree gchar *display_name = g_filename_display_basename (filename);
          g_prefix_error (error, "%s: ", display_name);
          return FALSE;
        }
    }

  locks_dir = g_build_filename (dir, "locks", NULL);
  locks_table = read_locks_directory (locks_dir, &local_error);
  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  if (locks_table != NULL)
    {
      GvdbItem *item;

      item = gvdb_hash_table_insert (table, ".locks");
      gvdb_item_set_hash_table (item, locks_table);
    }

  return g_steal_pointer (&table);
}

static gboolean
update_directory (const gchar *dir,
                  GError     **error)
{
  gint fd = -1;
  g_autofree gchar *filename = NULL;
  g_autoptr(GHashTable) table = NULL;
  g_autoptr(GDBusConnection) bus = NULL;

  g_assert (g_str_has_suffix (dir, ".d"));
  filename = strndup (dir, strlen (dir) - 2);

  table = read_directory (dir, error);
  if (table == NULL)
    return FALSE;

  fd = open (filename, O_WRONLY);
  if (fd < 0 && errno != ENOENT)
    {
      gint saved_errno = errno;
      g_autofree gchar *display_name = g_filename_display_name (filename);

      g_fprintf (stderr, "warning: Failed to open '%s': for replacement: %s\n",
                 display_name, g_strerror (saved_errno));
    }

  if (!gvdb_table_write_contents (table, filename, FALSE, error))
    {
      if (fd >= 0)
        close (fd);
      return FALSE;
    }

  if (fd >= 0)
    {
      /* Mark previous database as invalid. */
      write (fd, "\0\0\0\0\0\0\0\0", 8);
      close (fd);
    }

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);

  if (bus != NULL)
    {
      g_autofree gchar *object_name = NULL;
      g_autofree gchar *object_path = NULL;

      object_name = g_path_get_basename (filename);
      object_path = g_strconcat ("/ca/desrt/dconf/Writer/", object_name, NULL);

      /* Ignore all D-Bus errors. */
      g_dbus_connection_emit_signal (bus, NULL, object_path,
                                     "ca.desrt.dconf.Writer",
                                     "WritabilityNotify",
                                      g_variant_new ("(s)", "/"),
                                      NULL);
      g_dbus_connection_flush_sync (bus, NULL, NULL);
    }

  return TRUE;
}

static gboolean
update_all (const gchar *dirname,
            GError     **error)
{
  gboolean failed = FALSE;
  g_autoptr(GPtrArray) files = NULL;

  files = list_directory (dirname, S_IFDIR, error);
  if (files == NULL)
    return FALSE;

  for (guint i = 0; i != files->len; ++i)
    {
      const gchar *name;
      g_autoptr(GError) local_error = NULL;

      name = g_ptr_array_index (files, i);
      if (!g_str_has_suffix (name, ".d"))
        continue;

      if (!update_directory (name, &local_error))
        {
          g_autofree gchar *display_name = g_filename_display_name (name);
          g_fprintf (stderr, "%s: %s\n",
                     display_name, local_error->message);
          failed = TRUE;
        }
    }

  if (failed)
    {
      g_set_error_literal (error, DCONF_ERROR, DCONF_ERROR_FAILED,
                           "failed to update at least one of the databases");
      return FALSE;
    }

  return TRUE;
}

static gboolean
dconf_compile (const gchar **argv,
               GError      **error)
{
  gboolean byteswap;
  const gchar *output;
  const gchar *dir;
  g_autoptr(GHashTable) table = NULL;

  output = argv[0];
  if (output == NULL)
    return option_error_set (error, "output file not specified");

  dir = argv[1];
  if (dir == NULL)
    return option_error_set (error, "keyfile .d directory not specified");

  if (argv[2] != NULL)
    return option_error_set (error, "too many arguments");

  table = read_directory (dir, error);
  if (table == NULL)
    return FALSE;

  /* We always write the result of "dconf compile" as little endian so
   * that it can be installed in /usr/share */
  byteswap = (G_BYTE_ORDER == G_BIG_ENDIAN);
  return gvdb_table_write_contents (table, output, byteswap, error);
}

static gchar *
get_system_db_path ()
{
    return g_build_filename (SYSCONFDIR, "dconf", "db", NULL);
}

static gboolean
dconf_update (const gchar **argv,
              GError      **error)
{
  gint index = 0;
  g_autofree gchar *dir = NULL;

  if (argv[index] != NULL)
    {
      dir = g_strdup (argv[0]);
      index += 1;
    }
  else
    dir = get_system_db_path ();

  if (argv[index] != NULL)
    return option_error_set (error, "too many arguments");

  return update_all (dir, error);
}

typedef struct {
  const char  *name;
  gboolean   (*func)(const gchar **, GError **);
  const char  *description;
  const char  *synopsis;
} Command;

static const Command commands[] = {
  {
    "help", dconf_help,
    "Print help", " COMMAND "
  },
  {
    "read", dconf_read,
    "Read the value of a key.  -d to read default values.",
    " [-d] KEY "
  },
  {
    "list", dconf_list, 
    "List the sub-keys and sub-dirs of a dir",
    " DIR "
  },
  {
    "list-locks", dconf_list_locks,
    "List the locks under a dir",
    " DIR "
  },
  {
    "write", dconf_write,
    "Write a new value to a key",
    " KEY VALUE "
  },
  {
    "reset", dconf_reset,
    "Reset a key or dir.  -f is required for dirs.",
    " [-f] PATH "
  },
  {
    "compile", dconf_compile,
    "Compile a binary database from keyfiles",
    " OUTPUT KEYFILEDIR "
  },
  {
    "update", dconf_update,
    "Update the system dconf databases",
    " [DBDIR] "
  },
  {
    "watch", dconf_watch,
    "Watch a path for key changes",
    " PATH "
  },
  {
    "dump", dconf_dump,
    "Dump an entire subpath to stdout",
    " DIR "
  },
  {
    "load", dconf_load, 
    "Populate a subpath from stdin.  -f ignore locked keys.",
    " [-f] DIR "
  },
  {
    "blame", dconf_blame,
    "",
    ""
  },
  {
    "_complete", dconf_complete,
    "",
    " SUFFIX PATH "
  },
  {},
};

static const gchar usage[] = 
  "Usage:\n"
  "  dconf COMMAND [ARGS...]\n"
  "\n"
  "Commands:\n"
  "  help              Show this information\n"
  "  read              Read the value of a key\n"
  "  list              List the contents of a dir\n"
  "  write             Change the value of a key\n"
  "  reset             Reset the value of a key or dir\n"
  "  compile           Compile a binary database from keyfiles\n"
  "  update            Update the system databases\n"
  "  watch             Watch a path for changes\n"
  "  dump              Dump an entire subpath to stdout\n"
  "  load              Populate a subpath from stdin\n"
  "\n"
  "Use 'dconf help COMMAND' to get detailed help.\n"
  "\n";

static const Command *
command_with_name (const gchar *name)
{
  const Command *cmd;

  for (cmd = commands; cmd->name != NULL; ++cmd)
    if (g_strcmp0 (cmd->name, name) == 0)
      return cmd;

  return NULL;
}

static void
command_show_help (const Command *cmd,
                   FILE          *file)
{
  g_autoptr(GString) s = g_string_sized_new (1024);

  if (cmd == NULL)
    {
      g_string_append (s, usage);
    }
  else
    {
      /* Generate command specific usage help text. */

      g_string_append (s, "Usage:\n");
      g_string_append_printf (s, "  dconf %s%s\n\n", cmd->name, cmd->synopsis);

      if (!g_str_equal (cmd->description, ""))
        g_string_append_printf (s, "%s\n\n", cmd->description);

      if (!g_str_equal (cmd->synopsis, ""))
        {
          g_string_append (s, "Arguments:\n");

          if (strstr (cmd->synopsis, " COMMAND ") != NULL)
            g_string_append (s, "  COMMAND     "
                                "The (optional) command to explain\n");

          if (strstr (cmd->synopsis, " PATH ") != NULL)
            g_string_append (s, "  PATH        Either a KEY or DIR\n");

          if (strstr (cmd->synopsis, " PATH ") != NULL ||
              strstr (cmd->synopsis, " KEY ") != NULL)
            g_string_append (s, "  KEY         A key path (starting, but not ending with '/')\n");

          if (strstr (cmd->synopsis, " PATH ") != NULL ||
              strstr (cmd->synopsis, " DIR ") != NULL)
            g_string_append (s, "  DIR         A directory path (starting and ending with '/')\n");

          if (strstr (cmd->synopsis, " VALUE ") != NULL)
            g_string_append (s, "  VALUE       The value to write (in GVariant format)\n");

          if (strstr (cmd->synopsis, " OUTPUT ") != NULL)
            g_string_append (s, "  OUTPUT      The filename of the (binary) output\n");

          if (strstr (cmd->synopsis, " KEYFILEDIR ") != NULL)
            g_string_append (s, "  KEYFILEDIR  The path to the .d directory containing keyfiles\n");

          if (strstr (cmd->synopsis, " SUFFIX ") != NULL)
            g_string_append (s, "  SUFFIX      An empty string '' or '/'.\n");

          if (strstr (cmd->synopsis, " [DBDIR] ") != NULL)
            {
              g_autofree gchar *path = get_system_db_path ();
              g_string_append_printf (s, "  DBDIR       The databases directory. Default: %s\n", path);
            }

          g_string_append (s, "\n");
        }
    }

  g_fprintf (file, "%s", s->str);
}

static gboolean
dconf_help (const gchar **argv, GError **error)
{
  const gchar *name = *argv;
  command_show_help (command_with_name (name), stdout);
  return TRUE;
}

int
main (int argc, const char **argv)
{
  const Command *cmd;
  g_autoptr(GError) error = NULL;

  setlocale (LC_ALL, "");
  g_set_prgname (argv[0]);

  if (argc <= 1)
    {
      g_fprintf (stderr, "error: no command specified\n\n");
      command_show_help (NULL, stderr);
      return 2;
    }

  cmd = command_with_name (argv[1]);
  if (cmd == NULL)
    {
      g_fprintf (stderr, "error: unknown command %s\n\n", argv[1]);
      command_show_help (cmd, stderr);
      return 2;
    }

  if (cmd->func (argv + 2, &error))
    return 0;

  g_assert (error != NULL);

  /* Invalid arguments passed, show usage on stderr. */
  if (error->domain == G_OPTION_ERROR)
    {
      g_fprintf (stderr, "error: %s\n\n", error->message);
      command_show_help (cmd, stderr);
      return 2;
    }
  else
    {
      g_fprintf (stderr, "error: %s\n", error->message);
      return 1;
    }
}
