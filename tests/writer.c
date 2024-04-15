/*
 * Copyright © 2018 Endless Mobile, Inc
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
 * Author: Philip Withnall <withnall@endlessm.com>
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <locale.h>

#include "service/dconf-generated.h"
#include "service/dconf-writer.h"

static guint n_warnings = 0;

static GLogWriterOutput
log_writer_cb (GLogLevelFlags   log_level,
               const GLogField *fields,
               gsize            n_fields,
               gpointer         user_data)
{
  if (log_level & G_LOG_LEVEL_WARNING)
    n_warnings++;

  return G_LOG_WRITER_HANDLED;
}

static void
assert_n_warnings (guint expected_n_warnings)
{
  g_assert_cmpuint (n_warnings, ==, expected_n_warnings);
  n_warnings = 0;
}

static guint64
get_file_mtime_us (char *filename)
{
  GFile *file = g_file_new_for_path (filename);
  GError *error = NULL;
  GFileInfo *info = g_file_query_info (
    file,
    "time::*",
    G_FILE_QUERY_INFO_NONE,
    NULL,
    &error);
  if (!info)
    {
      printf ("failed with error %i: %s\n", error->code, error->message);
      exit (1);
    }

  guint64 mtime_us =
    g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED) * 1000000 +
    g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
  return mtime_us;
}

typedef struct
{
  gchar *dconf_dir;  /* (owned) */
} Fixture;

gchar *config_dir = NULL;

static void
set_up (Fixture       *fixture,
        gconstpointer  test_data)
{
  fixture->dconf_dir = g_build_filename (config_dir, "dconf", NULL);
  g_assert_cmpint (g_mkdir (fixture->dconf_dir, 0755), ==, 0);

  g_test_message ("Using dconf directory: %s", fixture->dconf_dir);
}

static void
tear_down (Fixture       *fixture,
           gconstpointer  test_data)
{
  g_assert_cmpint (g_rmdir (fixture->dconf_dir), ==, 0);
  g_clear_pointer (&fixture->dconf_dir, g_free);

  assert_n_warnings (0);
}

/* Test basic initialisation of a #DConfWriter. This is essentially a smoketest. */
static void
test_writer_basic (Fixture       *fixture,
                   gconstpointer  test_data)
{
  g_autoptr(DConfWriter) writer = NULL;

  writer = DCONF_WRITER (dconf_writer_new (DCONF_TYPE_WRITER, "some-name"));
  g_assert_nonnull (writer);

  g_assert_cmpstr (dconf_writer_get_name (writer), ==, "some-name");
}

/* Test that beginning a write operation when no database exists succeeds. Note
 * that the database will not actually be created until some changes are made
 * and the write is committed. */
static void
test_writer_begin_missing (Fixture       *fixture,
                           gconstpointer  test_data)
{
  g_autoptr(DConfWriter) writer = NULL;
  DConfWriterClass *writer_class;
  gboolean retval;
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *db_filename = g_build_filename (fixture->dconf_dir, "missing", NULL);

  /* Check the database doesn’t exist. */
  g_assert_false (g_file_test (db_filename, G_FILE_TEST_EXISTS));

  /* Create a writer. */
  writer = DCONF_WRITER (dconf_writer_new (DCONF_TYPE_WRITER, "missing"));
  g_assert_nonnull (writer);

  writer_class = DCONF_WRITER_GET_CLASS (writer);
  retval = writer_class->begin (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);
}

/* Test that beginning a write operation when a corrupt or empty database exists
 * will take a backup of the database and then succeed. Note that a new empty
 * database will not actually be created until some changes are made and the
 * write is committed. */
typedef struct
{
  const gchar *corrupt_db_contents;
  guint n_existing_backups;
} BeginCorruptFileData;

static void
test_writer_begin_corrupt_file (Fixture       *fixture,
                                gconstpointer  test_data)
{
  const BeginCorruptFileData *data = test_data;
  g_autoptr(DConfWriter) writer = NULL;
  DConfWriterClass *writer_class;
  gboolean retval;
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *db_filename = g_build_filename (fixture->dconf_dir, "corrupt", NULL);
  g_autofree gchar *new_db_filename_backup = NULL;
  g_autofree gchar *backup_file_contents = NULL;
  gsize backup_file_contents_len = 0;
  guint i;

  /* Create a corrupt database. */
  g_file_set_contents (db_filename, data->corrupt_db_contents, -1, &local_error);
  g_assert_no_error (local_error);

  /* Create any existing backups, to test we don’t overwrite them. */
  for (i = 0; i < data->n_existing_backups; i++)
    {
      g_autofree gchar *db_filename_backup = g_strdup_printf ("%s~%u", db_filename, i);
      g_file_set_contents (db_filename_backup, "backup", -1, &local_error);
      g_assert_no_error (local_error);
    }

  new_db_filename_backup = g_strdup_printf ("%s~%u", db_filename, data->n_existing_backups);

  /* Create a writer. */
  writer = DCONF_WRITER (dconf_writer_new (DCONF_TYPE_WRITER, "corrupt"));
  g_assert_nonnull (writer);

  writer_class = DCONF_WRITER_GET_CLASS (writer);
  retval = writer_class->begin (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* The writer should have printed a warning about the corrupt database. */
  assert_n_warnings (1);

  /* Check a backup file has been created and has the right content. */
  g_file_get_contents (new_db_filename_backup, &backup_file_contents,
                       &backup_file_contents_len, &local_error);
  g_assert_no_error (local_error);
  g_assert_cmpstr (backup_file_contents, ==, data->corrupt_db_contents);
  g_assert_cmpuint (backup_file_contents_len, ==, strlen (data->corrupt_db_contents));

  /* Clean up. */
  g_assert_cmpint (g_unlink (new_db_filename_backup), ==, 0);

  for (i = 0; i < data->n_existing_backups; i++)
    {
      g_autofree gchar *db_filename_backup = g_strdup_printf ("%s~%u", db_filename, i);
      g_assert_cmpint (g_unlink (db_filename_backup), ==, 0);
    }
}

/**
 * Test that committing a write operation when no writes have been queued
 * does not result in a database write.
 */
static void test_writer_commit_no_change (Fixture       *fixture,
                                          gconstpointer  test_data)
{
  const char *db_name = "nonexistent";
  g_autoptr(DConfWriter) writer = NULL;
  DConfWriterClass *writer_class;
  gboolean retval;
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *db_filename = g_build_filename (fixture->dconf_dir, db_name, NULL);

  /* Create a writer. */
  writer = DCONF_WRITER (dconf_writer_new (DCONF_TYPE_WRITER, db_name));
  g_assert_nonnull (writer);
  writer_class = DCONF_WRITER_GET_CLASS (writer);

  /* Check the database doesn’t exist. */
  g_assert_false (g_file_test (db_filename, G_FILE_TEST_EXISTS));

  /* Begin transaction */
  retval = writer_class->begin (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Commit transaction */
  retval = writer_class->commit (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Check the database still doesn’t exist. */
  g_assert_false (g_file_test (db_filename, G_FILE_TEST_EXISTS));

  /* End transaction */
  writer_class->end (writer);
}

/**
 * Test that committing a write operation when writes that would not change
 * the database have been queued does not result in a database write.
 */
static void test_writer_commit_empty_changes (Fixture       *fixture,
                                              gconstpointer  test_data)
{
  const char *db_name = "nonexistent";
  g_autoptr(DConfWriter) writer = NULL;
  DConfWriterClass *writer_class;
  gboolean retval;
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *db_filename = g_build_filename (fixture->dconf_dir, db_name, NULL);

  /* Create a writer. */
  writer = DCONF_WRITER (dconf_writer_new (DCONF_TYPE_WRITER, db_name));
  g_assert_nonnull (writer);
  writer_class = DCONF_WRITER_GET_CLASS (writer);

  /* Check the database doesn’t exist. */
  g_assert_false (g_file_test (db_filename, G_FILE_TEST_EXISTS));

  /* Begin transaction */
  retval = writer_class->begin (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Make a redundant/empty change to the database */
  DConfChangeset *changes = dconf_changeset_new();
  writer_class->change (writer, changes, NULL);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Commit transaction */
  retval = writer_class->commit (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Check the database still doesn't exist */
  g_assert_false (g_file_test (db_filename, G_FILE_TEST_EXISTS));

  /* End transaction */
  writer_class->end (writer);
}

/**
 * Test that committing a write operation when writes that would change
 * the database have been queued does result in a database write.
 */
static void test_writer_commit_real_changes (Fixture       *fixture,
                                             gconstpointer  test_data)
{
  const char *db_name = "nonexistent";
  g_autoptr(DConfWriter) writer = NULL;
  DConfWriterClass *writer_class;
  DConfChangeset *changes;
  gboolean retval;
  g_autoptr(GError) local_error = NULL;
  guint64 db_mtime_us;
  g_autofree gchar *db_filename = g_build_filename (fixture->dconf_dir, db_name, NULL);

  /* Create a writer. */
  writer = DCONF_WRITER (dconf_writer_new (DCONF_TYPE_WRITER, db_name));
  g_assert_nonnull (writer);
  writer_class = DCONF_WRITER_GET_CLASS (writer);

  /* Check the database doesn’t exist. */
  g_assert_false (g_file_test (db_filename, G_FILE_TEST_EXISTS));

  /* Begin transaction */
  retval = writer_class->begin (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Make a real change to the database */
  changes = dconf_changeset_new();
  dconf_changeset_set(changes, "/key", g_variant_new ("(s)", "value"));
  writer_class->change (writer, changes, NULL);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Commit transaction */
  retval = writer_class->commit (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Check the database now exists */
  g_assert_true (g_file_test (db_filename, G_FILE_TEST_EXISTS));
  db_mtime_us = get_file_mtime_us (db_filename);

  /* End transaction */
  writer_class->end (writer);

  /* Begin a second transaction */
  retval = writer_class->begin (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Make a redundant/empty change to the database */
  changes = dconf_changeset_new();
  writer_class->change (writer, changes, NULL);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Commit transaction */
  retval = writer_class->commit (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* End transaction */
  writer_class->end (writer);

  /* Check that no extra write was done (even after committing a real change) */
  g_assert_cmpuint (db_mtime_us, ==, get_file_mtime_us (db_filename));
  db_mtime_us = get_file_mtime_us (db_filename);

  /* Begin a third transaction */
  retval = writer_class->begin (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Commit transaction (with no changes at all) */
  retval = writer_class->commit (writer, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (retval);

  /* Check that no extra write was done (even after committing a real change) */
  g_assert_cmpuint (db_mtime_us, ==, get_file_mtime_us (db_filename));
  db_mtime_us = get_file_mtime_us (db_filename);

  /* End transaction */
  writer_class->end (writer);

  /* Clean up. */
  g_assert_cmpint (g_unlink (db_filename), ==, 0);
}

int
main (int argc, char **argv)
{
  g_autoptr(GError) local_error = NULL;
  int retval;
  const BeginCorruptFileData empty_data = { "", 0 };
  const BeginCorruptFileData corrupt_file_data0 = {
    "secretly not a valid GVDB database 😧", 0
  };
  const BeginCorruptFileData corrupt_file_data1 = {
    "secretly not a valid GVDB database 😧", 1
  };
  const BeginCorruptFileData corrupt_file_data2 = {
    "secretly not a valid GVDB database 😧", 2
  };

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  /* Set up a fake $XDG_CONFIG_HOME. We can’t do this in the fixture, as
   * g_get_user_config_dir() caches its return value. */
  config_dir = g_dir_make_tmp ("dconf-test-writer_XXXXXX", &local_error);
  g_assert_no_error (local_error);
  g_assert_true (g_setenv ("XDG_CONFIG_HOME", config_dir, TRUE));
  g_test_message ("Using config directory: %s", config_dir);

  /* Log handling so we don’t abort on the first g_warning(). */
  g_log_set_writer_func (log_writer_cb, NULL, NULL);

  g_test_add ("/writer/basic", Fixture, NULL, set_up,
              test_writer_basic, tear_down);
  g_test_add ("/writer/begin/missing", Fixture, NULL, set_up,
              test_writer_begin_missing, tear_down);
  g_test_add ("/writer/begin/empty", Fixture, &empty_data, set_up,
              test_writer_begin_corrupt_file, tear_down);
  g_test_add ("/writer/begin/corrupt-file/0", Fixture, &corrupt_file_data0, set_up,
              test_writer_begin_corrupt_file, tear_down);
  g_test_add ("/writer/begin/corrupt-file/1", Fixture, &corrupt_file_data1, set_up,
              test_writer_begin_corrupt_file, tear_down);
  g_test_add ("/writer/begin/corrupt-file/2", Fixture, &corrupt_file_data2, set_up,
              test_writer_begin_corrupt_file, tear_down);
  g_test_add ("/writer/commit/redundant_change/0", Fixture, NULL, set_up,
              test_writer_commit_no_change, tear_down);
  g_test_add ("/writer/commit/redundant_change/1", Fixture, NULL, set_up,
              test_writer_commit_empty_changes, tear_down);
  g_test_add ("/writer/commit/redundant_change/2", Fixture, NULL, set_up,
              test_writer_commit_real_changes, tear_down);

  retval = g_test_run ();

  /* Clean up the config dir. */
  g_unsetenv ("XDG_CONFIG_HOME");
  g_assert_cmpint (g_rmdir (config_dir), ==, 0);
  g_clear_pointer (&config_dir, g_free);

  return retval;
}
