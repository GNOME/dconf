#define _GNU_SOURCE

#include "../engine/dconf-engine.h"
#include "../engine/dconf-engine-profile.h"
#include "../engine/dconf-engine-mockable.h"
#include "../common/dconf-enums.h"
#include "dconf-mock.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include <math.h>

/* Interpose to catch fopen(SYSCONFDIR "/dconf/profile/user") */
static const gchar *filename_to_replace;
static const gchar *filename_to_replace_it_with;

FILE *
dconf_engine_fopen (const char *filename,
       const char *mode)
{

  if (filename_to_replace && g_str_equal (filename, filename_to_replace))
    {
      /* Crash if this file was unexpectedly opened */
      g_assert_nonnull (filename_to_replace_it_with);
      filename = filename_to_replace_it_with;
    }

  return fopen (filename, mode);
}

static void assert_no_messages (void);
static void assert_pop_message (const gchar    *expected_domain,
                                GLogLevelFlags  expected_log_level,
                                const gchar    *expected_message_fragment);
static void assert_maybe_pop_message (const gchar    *expected_domain,
                                      GLogLevelFlags  expected_log_level,
                                      const gchar    *expected_message_fragment);

static GThread *main_thread;
static GString *change_log;

void
dconf_engine_change_notify (DConfEngine         *engine,
                            const gchar         *prefix,
                            const gchar * const *changes,
                            const gchar         *tag,
                            gboolean             is_writability,
                            gpointer             origin_tag,
                            gpointer             user_data)
{
  gchar *joined;

  if (!change_log)
    return;

  if (is_writability)
    g_string_append (change_log, "w:");

  joined = g_strjoinv (",", (gchar **) changes);
  g_string_append_printf (change_log, "%s:%d:%s:%s;",
                          prefix, g_strv_length ((gchar **) changes), joined,
                          tag ? tag : "nil");
  g_free (joined);
}

static void
verify_and_free (DConfEngineSource  **sources,
                 gint                 n_sources,
                 const gchar * const *expected_names,
                 gint                 n_expected)
{
  gint i;

  g_assert_cmpint (n_sources, ==, n_expected);

  g_assert_true ((sources == NULL) == (n_sources == 0));

  for (i = 0; i < n_sources; i++)
    {
      g_assert_cmpstr (sources[i]->name, ==, expected_names[i]);
      dconf_engine_source_free (sources[i]);
    }

  g_free (sources);
}

static void
test_five_times (const gchar *filename,
                 gint         n_expected,
                 ...)
{
  const gchar **expected_names;
  DConfEngineSource **sources;
  gint n_sources;
  va_list ap;
  gint i;

  expected_names = g_new (const gchar *, n_expected);
  va_start (ap, n_expected);
  for (i = 0; i < n_expected; i++)
    expected_names[i] = va_arg (ap, const gchar *);
  va_end (ap);

  /* first try by supplying the profile filename via the API */
  g_assert_null (g_getenv ("DCONF_PROFILE"));
  g_assert_null (filename_to_replace);
  sources = dconf_engine_profile_open (filename, &n_sources);
  verify_and_free (sources, n_sources, expected_names, n_expected);

  /* next try supplying it via the environment */
  g_setenv ("DCONF_PROFILE", filename, TRUE);
  g_assert_null (filename_to_replace);
  sources = dconf_engine_profile_open (NULL, &n_sources);
  verify_and_free (sources, n_sources, expected_names, n_expected);
  g_unsetenv ("DCONF_PROFILE");

  /* next try supplying a profile name via API and intercepting fopen */
  filename_to_replace = SYSCONFDIR "/dconf/profile/myprofile";
  filename_to_replace_it_with = filename;
  g_assert_null (g_getenv ("DCONF_PROFILE"));
  sources = dconf_engine_profile_open ("myprofile", &n_sources);
  verify_and_free (sources, n_sources, expected_names, n_expected);
  filename_to_replace = NULL;

  /* next try the same, via the environment */
  g_setenv ("DCONF_PROFILE", "myprofile", TRUE);
  filename_to_replace = SYSCONFDIR "/dconf/profile/myprofile";
  filename_to_replace_it_with = filename;
  sources = dconf_engine_profile_open (NULL, &n_sources);
  verify_and_free (sources, n_sources, expected_names, n_expected);
  g_unsetenv ("DCONF_PROFILE");
  filename_to_replace = NULL;

  /* next try to have dconf pick it up as the default user profile */
  filename_to_replace = SYSCONFDIR "/dconf/profile/user";
  filename_to_replace_it_with = filename;
  g_assert_null (g_getenv ("DCONF_PROFILE"));
  sources = dconf_engine_profile_open (NULL, &n_sources);
  verify_and_free (sources, n_sources, expected_names, n_expected);
  filename_to_replace = NULL;

  filename_to_replace_it_with = NULL;
  g_free (expected_names);
}

typedef struct
{
  const gchar *profile_path;
  const gchar *expected_stderr_pattern;
} ProfileParserOpenData;

static void
test_profile_parser_errors (gconstpointer test_data)
{
  const ProfileParserOpenData *data = test_data;
  DConfEngineSource **sources;
  gint n_sources;

  if (g_test_subprocess ())
    {
      g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

      sources = dconf_engine_profile_open (data->profile_path, &n_sources);
      g_assert_cmpint (n_sources, ==, 0);
      g_assert_null (sources);
      return;
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
  g_test_trap_assert_stderr (data->expected_stderr_pattern);
}

static void
test_profile_parser (void)
{
  DConfEngineSource **sources;
  gint n_sources;

  test_five_times (SRCDIR "/profile/empty-profile", 0);
  test_five_times (SRCDIR "/profile/test-profile", 1, "test");
  test_five_times (SRCDIR "/profile/colourful", 4,
                   "user",
                   "other",
                   "verylongnameverylongnameverylongnameverylongnameverylongnameverylongnameverylongnameverylongnameverylongnameverylongnameverylongnameverylongname",
                   "nonewline");
  test_five_times (SRCDIR "/profile/dos", 2, "user", "site");
  test_five_times (SRCDIR "/profile/no-newline-longline", 0);
  test_five_times (SRCDIR "/profile/many-sources", 10,
                   "user", "local", "room", "floor", "building",
                   "site", "region", "division", "country", "global");

  /* finally, test that we get the default profile if the user profile
   * file cannot be located and we do not specify another profile.
   */
  filename_to_replace = SYSCONFDIR "/dconf/profile/user";
  filename_to_replace_it_with = SRCDIR "/profile/this-file-does-not-exist";
  g_assert_null (g_getenv ("DCONF_PROFILE"));
  sources = dconf_engine_profile_open (NULL, &n_sources);
  filename_to_replace = NULL;
  g_assert_cmpint (n_sources, ==, 1);
  g_assert_cmpstr (sources[0]->name, ==, "user");
  dconf_engine_source_free (sources[0]);
  g_free (sources);

  dconf_mock_shm_reset ();
}

static gpointer
test_signal_threadsafety_worker (gpointer user_data)
{
  gint *finished = user_data;
  gint i;

  for (i = 0; i < 20000; i++)
    {
      DConfEngine *engine;

      engine = dconf_engine_new (NULL, NULL, NULL);
      dconf_engine_unref (engine);
    }

  g_atomic_int_inc (finished);

  return NULL;
}

static void
test_signal_threadsafety (void)
{
#define N_WORKERS 4
  GVariant *parameters;
  gint finished = 0;
  gint i;

  parameters = g_variant_new_parsed ("('/test/key', [''], 'tag')");
  g_variant_ref_sink (parameters);

  for (i = 0; i < N_WORKERS; i++)
    g_thread_unref (g_thread_new ("testcase worker", test_signal_threadsafety_worker, &finished));

  while (g_atomic_int_get (&finished) < N_WORKERS)
    dconf_engine_handle_dbus_signal (G_BUS_TYPE_SESSION,
                                     ":1.2.3",
                                     "/ca/desrt/dconf/Writer/user",
                                     "Notify", parameters);
  g_variant_unref (parameters);

  dconf_mock_shm_reset ();
}

static void
test_user_source (void)
{
  DConfEngineSource *source;
  GvdbTable *table;
  GvdbTable *locks;
  gboolean reopened;

  /* Create the source from a clean slate */
  source = dconf_engine_source_new ("user-db:user");
  g_assert_nonnull (source);
  g_assert_null (source->values);
  g_assert_null (source->locks);

  /* Refresh it the first time.
   * This should cause it to open the shm.
   * FALSE should be returned because there is no database file.
   */
  reopened = dconf_engine_source_refresh (source);
  g_assert_false (reopened);
  dconf_mock_shm_assert_log ("open user;");

  /* Try to refresh it.  There must be no IO at this point. */
  reopened = dconf_engine_source_refresh (source);
  g_assert_false (reopened);
  dconf_mock_shm_assert_log ("");

  /* Add a real database. */
  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_table_insert (table, "/values/int32", g_variant_new_int32 (123456), NULL);
  dconf_mock_gvdb_install ("/HOME/.config/dconf/user", table);

  /* Try to refresh it again.
   * Because we didn't flag the change there must still be no IO.
   */
  reopened = dconf_engine_source_refresh (source);
  g_assert_false (reopened);
  g_assert_null (source->values);
  g_assert_null (source->locks);
  dconf_mock_shm_assert_log ("");

  /* Now flag it and reopen. */
  dconf_mock_shm_flag ("user");
  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);
  g_assert_nonnull (source->values);
  g_assert_null (source->locks);
  g_assert_true (gvdb_table_has_value (source->values, "/values/int32"));
  dconf_mock_shm_assert_log ("close;open user;");

  /* Do it again -- should get the same result, after some IO */
  dconf_mock_shm_flag ("user");
  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);
  g_assert_nonnull (source->values);
  g_assert_null (source->locks);
  dconf_mock_shm_assert_log ("close;open user;");

  /* "Delete" the gvdb and make sure dconf notices after a flag */
  dconf_mock_gvdb_install ("/HOME/.config/dconf/user", NULL);
  dconf_mock_shm_flag ("user");
  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);
  g_assert_null (source->values);
  g_assert_null (source->locks);
  dconf_mock_shm_assert_log ("close;open user;");

  /* Add a gvdb with a lock */
  table = dconf_mock_gvdb_table_new ();
  locks = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_table_insert (table, "/values/int32", g_variant_new_int32 (123456), NULL);
  dconf_mock_gvdb_table_insert (locks, "/values/int32", g_variant_new_boolean (TRUE), NULL);
  dconf_mock_gvdb_table_insert (table, ".locks", NULL, locks);
  dconf_mock_gvdb_install ("/HOME/.config/dconf/user", table);

  /* Reopen and check if we have the lock */
  dconf_mock_shm_flag ("user");
  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);
  g_assert_nonnull (source->values);
  g_assert_nonnull (source->locks);
  g_assert_true (gvdb_table_has_value (source->values, "/values/int32"));
  g_assert_true (gvdb_table_has_value (source->locks, "/values/int32"));
  dconf_mock_shm_assert_log ("close;open user;");

  /* Reopen one last time */
  dconf_mock_shm_flag ("user");
  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);
  g_assert_nonnull (source->values);
  g_assert_nonnull (source->locks);
  dconf_mock_shm_assert_log ("close;open user;");

  dconf_engine_source_free (source);
  dconf_mock_shm_assert_log ("close;");

  dconf_mock_gvdb_install ("/HOME/.config/dconf/user", NULL);
  dconf_mock_shm_reset ();
}

static void
test_file_source (void)
{
  DConfEngineSource *source;
  gboolean reopened;
  GvdbTable *table;
  GVariant *value;

  source = dconf_engine_source_new ("file-db:/path/to/db");
  g_assert_nonnull (source);
  g_assert_null (source->values);
  g_assert_null (source->locks);
  reopened = dconf_engine_source_refresh (source);
  g_assert_null (source->values);
  g_assert_null (source->locks);
  dconf_engine_source_free (source);

  assert_pop_message ("dconf", G_LOG_LEVEL_WARNING, "unable to open file '/path/to/db'");

  source = dconf_engine_source_new ("file-db:/path/to/db");
  g_assert_nonnull (source);
  g_assert_null (source->values);
  g_assert_null (source->locks);

  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_table_insert (table, "/value", g_variant_new_string ("first file"), NULL);
  dconf_mock_gvdb_install ("/path/to/db", table);

  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);
  g_assert_nonnull (source->values);
  g_assert_null (source->locks);
  value = gvdb_table_get_value (source->values, "/value");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "first file");
  g_variant_unref (value);

  /* Of course this should do nothing... */
  reopened = dconf_engine_source_refresh (source);
  g_assert_false (reopened);

  /* Invalidate and replace */
  dconf_mock_gvdb_table_invalidate (table);
  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_table_insert (table, "/value", g_variant_new_string ("second file"), NULL);
  dconf_mock_gvdb_install ("/path/to/db", table);

  /* Even when invalidated, this should still do nothing... */
  reopened = dconf_engine_source_refresh (source);
  g_assert_false (reopened);
  value = gvdb_table_get_value (source->values, "/value");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "first file");
  g_variant_unref (value);

  dconf_mock_gvdb_install ("/path/to/db", NULL);
  dconf_engine_source_free (source);
}


static gboolean service_db_created;
static GvdbTable *service_db_table;

static GVariant *
handle_service_request (GBusType             bus_type,
                        const gchar         *bus_name,
                        const gchar         *object_path,
                        const gchar         *interface_name,
                        const gchar         *method_name,
                        GVariant            *parameters,
                        const GVariantType  *expected_type,
                        GError             **error)
{
  g_assert_cmpstr (bus_name, ==, "ca.desrt.dconf");
  g_assert_cmpstr (interface_name, ==, "ca.desrt.dconf.Writer");
  g_assert_cmpstr (method_name, ==, "Init");
  g_assert_cmpstr (g_variant_get_type_string (parameters), ==, "()");

  if (g_str_equal (object_path, "/ca/desrt/dconf/shm/nil"))
    {
      service_db_table = dconf_mock_gvdb_table_new ();
      dconf_mock_gvdb_table_insert (service_db_table, "/values/int32", g_variant_new_int32 (123456), NULL);
      dconf_mock_gvdb_install ("/RUNTIME/dconf-service/shm/nil", service_db_table);

      /* Make sure this only happens the first time... */
      g_assert_false (service_db_created);
      service_db_created = TRUE;

      return g_variant_new ("()");
    }
  else
    {
      g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "Unknown DB type");
      return NULL;
    }
}

static void
test_service_source (void)
{
  DConfEngineSource *source;
  gboolean reopened;

  /* Make sure we deal with errors from the service sensibly */
  if (g_test_subprocess ())
    {
      g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

      source = dconf_engine_source_new ("service-db:unknown/nil");
      dconf_mock_dbus_sync_call_handler = handle_service_request;
      g_assert_nonnull (source);
      g_assert_null (source->values);
      g_assert_null (source->locks);
      reopened = dconf_engine_source_refresh (source);

      return;
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
  g_test_trap_assert_stderr ("*WARNING*: unable to open file*unknown/nil*expect degraded performance*");

  /* Set up one that will work */
  source = dconf_engine_source_new ("service-db:shm/nil");
  g_assert_nonnull (source);
  g_assert_null (source->values);
  g_assert_null (source->locks);

  /* Refresh it the first time.
   *
   * This should cause the service to be asked to create it.
   *
   * This should return TRUE because we just opened it.
   */
  dconf_mock_dbus_sync_call_handler = handle_service_request;
  reopened = dconf_engine_source_refresh (source);
  dconf_mock_dbus_sync_call_handler = NULL;
  g_assert_true (service_db_created);
  g_assert_true (reopened);

  /* After that, a refresh should be a no-op. */
  reopened = dconf_engine_source_refresh (source);
  g_assert_false (reopened);

  /* Close it and reopen it, ensuring that we don't hit the service
   * again (because the file already exists).
   *
   * Note: dconf_mock_dbus_sync_call_handler = NULL, so D-Bus calls will
   * assert.
   */
  dconf_engine_source_free (source);
  source = dconf_engine_source_new ("service-db:shm/nil");
  g_assert_nonnull (source);
  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);

  /* Make sure it has the content we expect to see */
  g_assert_true (gvdb_table_has_value (source->values, "/values/int32"));

  /* Now invalidate it and replace it with an empty one */
  dconf_mock_gvdb_table_invalidate (service_db_table);
  service_db_table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install ("/RUNTIME/dconf-service/shm/nil", service_db_table);

  /* Now reopening should get the new one */
  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);

  /* ...and we should find it to be empty */
  g_assert_false (gvdb_table_has_value (source->values, "/values/int32"));

  /* We're done. */
  dconf_engine_source_free (source);

  /* This should not have done any shm... */
  dconf_mock_shm_assert_log ("");

  dconf_mock_gvdb_install ("/RUNTIME/dconf-service/shm/nil", NULL);
  service_db_table = NULL;
}

static void
test_system_source (void)
{
  DConfEngineSource *source;
  GvdbTable *first_table;
  GvdbTable *next_table;
  gboolean reopened;

  source = dconf_engine_source_new ("system-db:site");
  g_assert_nonnull (source);

  /* Check to see that we get the warning about the missing file. */
  if (g_test_subprocess ())
    {
      g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

      /* Failing to open should return FALSE from refresh */
      reopened = dconf_engine_source_refresh (source);
      g_assert_false (reopened);
      g_assert_null (source->values);

      /* Attempt the reopen to make sure we don't get two warnings.
       * We should see FALSE again since we go from NULL to NULL.
       */
      reopened = dconf_engine_source_refresh (source);
      g_assert_false (reopened);

      /* Create the file after the fact and make sure it opens properly */
      first_table = dconf_mock_gvdb_table_new ();
      dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", first_table);

      reopened = dconf_engine_source_refresh (source);
      g_assert_true (reopened);
      g_assert_nonnull (source->values);

      dconf_engine_source_free (source);

      return;
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
  /* Check that we only saw the warning, but only one time. */
  g_test_trap_assert_stderr ("*this gvdb does not exist; expect degraded performance*");
  g_test_trap_assert_stderr_unmatched ("*degraded*degraded*");

  /* Create the file before the first refresh attempt */
  first_table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", first_table);
  /* Hang on to a copy for ourselves for below... */
  dconf_mock_gvdb_table_ref (first_table);

  /* See that we get the database. */
  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);
  g_assert_true (source->values == first_table);

  /* Do a refresh, make sure there is no change. */
  reopened = dconf_engine_source_refresh (source);
  g_assert_false (reopened);
  g_assert_true (source->values == first_table);

  /* Replace the table on "disk" but don't invalidate the old one */
  next_table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", next_table);

  /* Make sure the old table remains open (ie: no IO performed) */
  reopened = dconf_engine_source_refresh (source);
  g_assert_false (reopened);
  g_assert_true (source->values == first_table);

  /* Now mark the first table invalid and reopen */
  dconf_mock_gvdb_table_invalidate (first_table);
  gvdb_table_free (first_table);
  reopened = dconf_engine_source_refresh (source);
  g_assert_true (reopened);
  g_assert_true (source->values == next_table);

  /* Remove the file entirely and do the same thing */
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", NULL);
  reopened = dconf_engine_source_refresh (source);
  g_assert_false (reopened);

  dconf_engine_source_free (source);
}

static void
invalidate_state (guint     n_sources,
                  guint     source_types,
                  gpointer *state)
{
  gint i;

  for (i = 0; i < n_sources; i++)
    if (source_types & (1u << i))
      {
        if (state[i])
          {
            dconf_mock_gvdb_table_invalidate (state[i]);
            gvdb_table_free (state[i]);
          }
      }
    else
      {
        dconf_mock_shm_flag (state[i]);
        g_free (state[i]);
      }
}

static void
setup_state (guint     n_sources,
             guint     source_types,
             guint     database_state,
             gpointer *state)
{
  gint i;

  for (i = 0; i < n_sources; i++)
    {
      guint contents = database_state % 7;
      GvdbTable *table = NULL;
      gchar *filename;

      if (contents)
        {
          table = dconf_mock_gvdb_table_new ();

          /* Even numbers get the value setup */
          if ((contents & 1) == 0)
            dconf_mock_gvdb_table_insert (table, "/value", g_variant_new_uint32 (i), NULL);

          /* Numbers above 2 get the locks table */
          if (contents > 2)
            {
              GvdbTable *locks;

              locks = dconf_mock_gvdb_table_new ();

              /* Numbers above 4 get the lock set */
              if (contents > 4)
                dconf_mock_gvdb_table_insert (locks, "/value", g_variant_new_boolean (TRUE), NULL);

              dconf_mock_gvdb_table_insert (table, ".locks", NULL, locks);
            }
        }

      if (source_types & (1u << i))
        {
          if (state)
            {
              if (table)
                state[i] = dconf_mock_gvdb_table_ref (table);
              else
                state[i] = NULL;
            }

          filename = g_strdup_printf (SYSCONFDIR "/dconf/db/db%d", i);
        }
      else
        {
          if (state)
            state[i] = g_strdup_printf ("db%d", i);

          filename = g_strdup_printf ("/HOME/.config/dconf/db%d", i);
        }

      dconf_mock_gvdb_install (filename, table);
      g_free (filename);

      database_state /= 7;
    }
}

static void
create_profile (const gchar *filename,
                guint        n_sources,
                guint        source_types)
{
  GError *error = NULL;
  GString *profile;
  gint i;

  profile = g_string_new (NULL);
  for (i = 0; i < n_sources; i++)
    if (source_types & (1u << i))
      g_string_append_printf (profile, "system-db:db%d\n", i);
    else
      g_string_append_printf (profile, "user-db:db%d\n", i);
  g_file_set_contents (filename, profile->str, profile->len, &error);
  g_assert_no_error (error);
  g_string_free (profile, TRUE);
}

static GQueue read_through_queues[12];

static void
check_read (DConfEngine *engine,
            guint        n_sources,
            guint        source_types,
            guint        database_state)
{
  gboolean any_values = FALSE;
  gboolean any_locks = FALSE;
  guint first_contents;
  gint underlying = -1;
  gint expected = -1;
  gboolean writable;
  GVariant *value;
  gchar **list;
  guint i;
  gint n;

  /* The value we expect to read is number of the first source that has
   * the value set (ie: odd digit in database_state) up to the lowest
   * level lock.
   *
   * We go over each database.  If 'expected' has not yet been set and
   * we find that we should have a value in this database, we set it.
   * If we find that we should have a lock in this database, we unset
   * any previous values (since they should not have been written).
   *
   * We intentionally code this loop in a different way than the one in
   * dconf itself is currently implemented...
   *
   * We also take note of if we saw any locks and cross-check that with
   * dconf_engine_is_writable().  We check if we saw and values at all
   * and cross-check that with dconf_engine_list() (which ignores
   * locks).
   */
  first_contents = database_state % 7;
  for (i = 0; i < n_sources; i++)
    {
      guint contents = database_state % 7;

      /* A lock here should prevent higher reads */
      if (contents > 4)
        {
          /* Locks in the first database don't count... */
          if (i != 0)
            any_locks = TRUE;
          expected = -1;
        }

      /* A value here should be read */
      if (contents && !(contents & 1))
        {
          if (i != 0 && underlying == -1)
            underlying = i;

          if (expected == -1)
            {
              any_values = TRUE;
              expected = i;
            }
        }

      database_state /= 7;
    }

  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value");

  if (expected != -1)
    {
      g_assert_true (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32));
      g_assert_cmpint (g_variant_get_uint32 (value), ==, expected);
      g_variant_unref (value);
    }
  else
    g_assert_null (value);

  /* We are writable if the first database is a user database and we
   * didn't encounter any locks...
   */
  writable = dconf_engine_is_writable (engine, "/value");
  g_assert_cmpint (writable, ==, n_sources && !(source_types & 1) && !any_locks);

  /* Check various read-through scenarios.  Read-through should only be
   * effective if the database is writable.
   */
  for (i = 0; i < G_N_ELEMENTS (read_through_queues); i++)
    {
      gint our_expected = expected;

      if (writable)
        {
          /* If writable, see what our changeset did.
           *
           *   0: nothing
           *   1: reset value (should see underlying value)
           *   2: set value to 123
           */
          if ((i % 3) == 1)
            our_expected = underlying;
          else if ((i % 3) == 2)
            our_expected = 123;
        }

      value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, &read_through_queues[i], "/value");

      if (our_expected != -1)
        {
          g_assert_true (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32));
          g_assert_cmpint (g_variant_get_uint32 (value), ==, our_expected);
          g_variant_unref (value);
        }
      else
        g_assert_null (value);
    }

  /* Check listing */
  g_strfreev (dconf_engine_list (engine, "/", &n));
  list = dconf_engine_list (engine, "/", NULL);
  g_assert_cmpint (g_strv_length (list), ==, n);
  if (any_values)
    {
      g_assert_cmpstr (list[0], ==, "value");
      g_assert_null (list[1]);
    }
  else
    g_assert_null (list[0]);
  g_strfreev (list);

  /* Check the user value.
   *
   * This should be set only in the case that the first database is a
   * user database (ie: writable) and the contents of that database are
   * set (ie: 2, 4 or 6).  See the table in the comment below.
   *
   * Note: we do not consider locks.
   */
  value = dconf_engine_read (engine, DCONF_READ_USER_VALUE, NULL, "/value");
  if (value)
    {
      g_assert_true (first_contents && !(first_contents & 1) && !(source_types & 1));
      g_assert_true (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32));
      g_assert_cmpint (g_variant_get_uint32 (value), ==, 0);
      g_variant_unref (value);
    }
  else
    {
      /* Three possibilities for failure:
       *  - first db did not exist
       *  - value was missing from first db
       *  - first DB was system-db
       */
      g_assert_true (!first_contents || (first_contents & 1) || (source_types & 1));
    }

  /* Check read_through vs. user-value */
  for (i = 0; i < G_N_ELEMENTS (read_through_queues); i++)
    {
      /* It is only possible here to see one of three possibilities:
       *
       *   - NULL
       *   - 0 (value from user's DB)
       *   - 123 (value from queue)
       *
       * We see these values regardless of writability.  We do however
       * ensure that we have a writable database as the first one.
       */
      value = dconf_engine_read (engine, DCONF_READ_USER_VALUE, &read_through_queues[i], "/value");

      /* If we have no first source, or the first source is non-user
       * than we should always do nothing (since we can't queue changes
       * against a system db or one that doesn't exist).
       */
      if (n_sources == 0 || (source_types & 1) || (i % 3) == 0)
        {
          /* Changeset did nothing, so it should be same as above. */
          if (value)
            {
              g_assert_true (first_contents && !(first_contents & 1) && !(source_types & 1));
              g_assert_true (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32));
              g_assert_cmpint (g_variant_get_uint32 (value), ==, 0);
            }
          else
            g_assert_true (!first_contents || (first_contents & 1) || (source_types & 1));
        }
      else if ((i % 3) == 1)
        {
          /* Changeset did a reset, so we should always see NULL */
          g_assert_null (value);
        }
      else if ((i % 3) == 2)
        {
          /* Changeset set a value, so we should see it */
          g_assert_cmpint (g_variant_get_uint32 (value), ==, 123);
        }

      if (value)
        g_variant_unref (value);
    }
}

static void
test_read (void)
{
#define MAX_N_SOURCES 2
  gpointer state[MAX_N_SOURCES];
  gchar *profile_filename;
  GError *error = NULL;
  DConfEngine *engine;
  guint i, j, k;
  guint n;

  /* This test throws a lot of messages about missing databases.
   * Capture and ignore them.
   */

  /* Our test strategy is as follows:
   *
   * We only test a single key name.  It is assumed that gvdb is working
   * properly already so we are only interested in interactions between
   * multiple databases for a given key name.
   *
   * The outermost loop is over 'n'.  This is how many sources are in
   * our test.  We test 0 to 3 (which should be enough to cover all
   * 'interesting' possibilities).  4 takes too long to run (2*7*7 ~=
   * 100 times as long as 3).
   *
   * The next loop is over 'i'.  This goes from 0 to 2^n - 1, with each
   * bit deciding the type of source of the i-th element
   *
   *   0: user
   *
   *   1: system
   *
   * The next loop is over 'j'.  This goes from 0 to 7^n - 1, with each
   * base-7 digit deciding the state of the database file associated
   * with the i-th source:
   *
   *   j      file    has value   has ".locks"   has lock
   *  ----------------------------------------------------
   *   0      0       -           -              -
   *   1      1       0           0              -
   *   2      1       1           0              -
   *   3      1       0           1              0
   *   4      1       1           1              0
   *   5      1       0           1              1
   *   6      1       1           1              1
   *
   * Where 'file' is if the database file exists, 'has value' is if a
   * value exists at '/value' within the file, 'has ".locks"' is if
   * there is a ".locks" subtable and 'has lock' is if there is a lock
   * for '/value' within that table.
   *
   * Finally, we loop over 'k' as a state to transition to ('k' works
   * the same way as 'j').
   *
   * Once we know 'n' and 'i', we can write a profile file.
   *
   * Once we know 'j' we can setup the initial state, create the engine
   * and check that we got the expected value.  Then we transition to
   * state 'k' and make sure everything still works as expected.
   *
   * Since we want to test all j->k transitions, we do the initial setup
   * of the engine (according to j) inside of the 'k' loop, since we
   * need to test all possible transitions from 'j'.
   *
   * We additionally test the effect of read-through queues in 4
   * situations:
   *
   *   - NULL: no queue
   *   - 0: queue with no effect
   *   - 1: queue that resets the value
   *   - 2: queue that sets the value to 123
   *
   * For the cases (0, 1, 2) we can have multiple types of queue that
   * achieve the desired effect.  We can put more than 3 items in
   * read_through_queues -- the expected behaviour is dictated by the
   * value of (i % 3) where i is the array index.
   */
  {
    /* We use a scheme to set up each queue.  Again, we assume that
     * GHashTable is working OK, so we only bother having "/value" as a
     * changeset item (or not).
     *
     * We have an array of strings, each string defining the
     * configuration of one queue.  In each string, each character
     * represents the contents of a changeset within the queue, in
     * order.
     *
     *  ' ' - empty changeset
     *  's' - set value to 123
     *  'r' - reset value
     *  'x' - set value to 321
     */
    const gchar *queue_configs[] = {
      "", "r", "s",
      " ", "rr", "ss",
      "  ", "rs", "sr",
      "  ", "rx", "sx"
    };
    gint i;

    G_STATIC_ASSERT (G_N_ELEMENTS (queue_configs) == G_N_ELEMENTS (read_through_queues));
    for (i = 0; i < G_N_ELEMENTS (read_through_queues); i++)
      {
        const gchar *conf = queue_configs[i];
        gint j;

        for (j = 0; conf[j]; j++)
          {
            DConfChangeset *changeset;

            changeset = dconf_changeset_new ();

            switch (conf[j])
              {
              case ' ':
                break;
              case 'r':
                dconf_changeset_set (changeset, "/value", NULL);
                break;
              case 's':
                dconf_changeset_set (changeset, "/value", g_variant_new_uint32 (123));
                break;
              case 'x':
                dconf_changeset_set (changeset, "/value", g_variant_new_uint32 (321));
                break;
              default:
                g_assert_not_reached ();
              }

            g_queue_push_head (&read_through_queues[i], changeset);
          }
      }
  }

  /* We need a place to put the profile files we use for this test */
  close (g_file_open_tmp ("dconf-testcase.XXXXXX", &profile_filename, &error));
  g_assert_no_error (error);

  for (n = 0; n <= MAX_N_SOURCES; n++)
    for (i = 0; i < pow (2, n); i++)
      {
        gint n_possible_states = pow (7, n);

        /* Step 1: write out the profile file */
        create_profile (profile_filename, n, i);

        for (j = 0; j < n_possible_states; j++)
          for (k = 0; k < n_possible_states; k++)
            {
              guint64 old_state, new_state;

              /* Step 2: setup the state */
              setup_state (n, i, j, (j != k) ? state : NULL);

              /* Step 3: create the engine */
              engine = dconf_engine_new (profile_filename, NULL, NULL);

              /* Step 4: read, and check result */
              check_read (engine, n, i, j);
              old_state = dconf_engine_get_state (engine);

              /* Step 5: change to the new state */
              if (j != k)
                {
                  setup_state (n, i, k, NULL);
                  invalidate_state (n, i, state);
                }

              /* Step 6: read, and check result */
              check_read (engine, n, i, k);
              new_state = dconf_engine_get_state (engine);

              g_assert_true ((j == k) == (new_state == old_state));

              /* Clean up */
              setup_state (n, i, 0, NULL);
              dconf_engine_unref (engine);

              assert_maybe_pop_message ("dconf", G_LOG_LEVEL_WARNING,
                                        "unable to open file '" SYSCONFDIR "/dconf/db");
            }
      }

  /* Clean up the tempfile we were using... */
  g_unlink (profile_filename);
  g_free (profile_filename);
  dconf_mock_shm_reset ();

  assert_no_messages ();
}

static void
test_watch_fast (void)
{
  DConfEngine *engine;
  GvdbTable *table;
  GVariant *triv;
  guint64 a, b, c;

  change_log = g_string_new (NULL);

  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install ("/HOME/.config/dconf/user", table);
  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", table);

  triv = g_variant_ref_sink (g_variant_new ("()"));

  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);

  /* Check that establishing a watch works properly in the normal case.
   */
  a = dconf_engine_get_state (engine);
  dconf_engine_watch_fast (engine, "/a/b/c");
  /* watches do not count as outstanding changes */
  g_assert_false (dconf_engine_has_outstanding (engine));
  dconf_engine_sync (engine);
  b = dconf_engine_get_state (engine);
  g_assert_cmpuint (a, ==, b);
  /* both AddMatch results come back before shm is flagged */
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_assert_no_async ();
  dconf_mock_shm_flag ("user");
  b = dconf_engine_get_state (engine);
  g_assert_cmpuint (a, !=, b);
  g_assert_cmpstr (change_log->str, ==, "");
  dconf_engine_unwatch_fast (engine, "/a/b/c");
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_assert_no_async ();

  /* Establish a watch and fail the race. */
  a = dconf_engine_get_state (engine);
  dconf_engine_watch_fast (engine, "/a/b/c");
  g_assert_false (dconf_engine_has_outstanding (engine));
  dconf_engine_sync (engine);
  b = dconf_engine_get_state (engine);
  g_assert_cmpuint (a, ==, b);
  /* one AddMatch result comes back -after- shm is flagged */
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_shm_flag ("user");
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_assert_no_async ();
  b = dconf_engine_get_state (engine);
  g_assert_cmpuint (a, !=, b);
  g_assert_cmpstr (change_log->str, ==, "/a/b/c:1::nil;");
  /* Try to establish a watch again for the same path */
  dconf_engine_watch_fast (engine, "/a/b/c");
  g_assert_false (dconf_engine_has_outstanding (engine));
  dconf_engine_sync (engine);
  c = dconf_engine_get_state (engine);
  g_assert_cmpuint (b, ==, c);
  /* The watch result was not sent, because the path was already watched */
  dconf_mock_dbus_assert_no_async ();
  c = dconf_engine_get_state (engine);
  g_assert_cmpuint (b, ==, c);
  /* Since the path was already being watched,
   * do not expect a second false change notification */
  g_assert_cmpstr (change_log->str, ==, "/a/b/c:1::nil;");
  dconf_engine_unwatch_fast (engine, "/a/b/c");
  /* nothing was done, because there is still a subscription left */
  dconf_mock_dbus_assert_no_async ();
  dconf_engine_unwatch_fast (engine, "/a/b/c");
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_assert_no_async ();

  dconf_mock_gvdb_install ("/HOME/.config/dconf/user", NULL);
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", NULL);
  dconf_engine_unref (engine);
  g_string_free (change_log, TRUE);
  change_log = NULL;
  g_variant_unref (triv);
}

static void
test_watch_fast_simultaneous_subscriptions (void)
{
  /**
   * Test that creating multiple subscriptions to the same path
   * simultaneously (before receiving replies from D-Bus) only results in
   * a single D-Bus match rule, and that it is removed at the right time.
   */
  DConfEngine *engine;
  GvdbTable *table;
  GVariant *triv;

  /* Set up */
  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install ("/HOME/.config/dconf/user", table);
  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", table);

  triv = g_variant_ref_sink (g_variant_new ("()"));

  change_log = g_string_new (NULL);

  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);


  /* Subscribe to the same path 3 times. Both AddMatch results succeed
   * (one for each source). There is only one for each source*path.
   */
  dconf_engine_watch_fast (engine, "/a/b/c");
  dconf_engine_watch_fast (engine, "/a/b/c");
  dconf_engine_watch_fast (engine, "/a/b/c");

  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_assert_no_async ();

  /* Unsubscribe twice, after the AddMatch succeeds. There is still one
   * active subscription, so no RemoveMatch request is sent. */
  dconf_engine_unwatch_fast (engine, "/a/b/c");
  dconf_engine_unwatch_fast (engine, "/a/b/c");

  dconf_mock_dbus_assert_no_async ();

  /* Unsubscribe once more. The number of active subscriptions falls to 0
   * and the D-Bus match rule is removed */
  dconf_engine_unwatch_fast (engine, "/a/b/c");

  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_assert_no_async ();

  /* The shm was not flagged at any point - so no change notifications
   * should not have been sent */
  g_assert_cmpstr (change_log->str, ==, "");

  /* Clean up */
  dconf_engine_unref (engine);
  g_string_free (change_log, TRUE);
  change_log = NULL;
  g_variant_unref (triv);
}

static void
test_watch_fast_successive_subscriptions (void)
{
  /**
   * Test that subscribing to the same path multiple times successively
   * (after waiting for any expected replies from D-Bus) results in only
   * a single D-Bus match rule being created, and that it is created and
   * destroyed at the right times.
   */
  DConfEngine *engine;
  GvdbTable *table;
  GVariant *triv;

  /* Set up */
  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install ("/HOME/.config/dconf/user", table);
  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", table);

  triv = g_variant_ref_sink (g_variant_new ("()"));

  change_log = g_string_new (NULL);

  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);

  /* Subscribe to a path, and simulate a change to the database while the
   * AddMatch request is in progress */
  dconf_engine_watch_fast (engine, "/a/b/c");
  dconf_mock_shm_flag ("user");
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);

  /* When the AddMatch requests succeeds, expect a change notification
   * for the path */
  dconf_mock_dbus_assert_no_async ();
  g_assert_cmpstr (change_log->str, ==, "/a/b/c:1::nil;");

  /* Subscribe to a path twice again, and simulate a change to the
   * database */
  dconf_engine_watch_fast (engine, "/a/b/c");
  dconf_engine_watch_fast (engine, "/a/b/c");
  dconf_mock_shm_flag ("user");

  /* There was already a match rule in place, so there should be no D-Bus
   * requests and no change notifications */
  dconf_mock_dbus_assert_no_async ();
  g_assert_cmpstr (change_log->str, ==, "/a/b/c:1::nil;");

  /* Unsubscribe */
  dconf_engine_unwatch_fast (engine, "/a/b/c");
  dconf_engine_unwatch_fast (engine, "/a/b/c");
  dconf_mock_dbus_assert_no_async ();
  dconf_engine_unwatch_fast (engine, "/a/b/c");
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_assert_no_async ();


  /* Clean up */
  dconf_engine_unref (engine);
  g_string_free (change_log, TRUE);
  change_log = NULL;
  g_variant_unref (triv);
}

static void
test_watch_fast_short_lived_subscriptions (void)
{
  /**
   * Test that subscribing and then immediately unsubscribing (without
   * waiting for replies from D-Bus) multiple times to the same path
   * correctly triggers D-Bus requests and change notifications in cases
   * where the D-Bus match rule was not in place when the database was
   * changed.
   */
  DConfEngine *engine;
  GvdbTable *table;
  GVariant *triv;

  /* Set up */
  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install ("/HOME/.config/dconf/user", table);
  table = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", table);

  triv = g_variant_ref_sink (g_variant_new ("()"));

  change_log = g_string_new (NULL);

  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);

  /* Subscribe to a path twice, and simulate a change to the database
   * while the AddMatch request is in progress */
  dconf_engine_watch_fast (engine, "/a/b/c");
  dconf_engine_watch_fast (engine, "/a/b/c");
  dconf_mock_shm_flag ("user");
  dconf_engine_unwatch_fast (engine, "/a/b/c");
  dconf_engine_unwatch_fast (engine, "/a/b/c");
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_async_reply (triv, NULL);
  dconf_mock_dbus_assert_no_async ();

  /* When the AddMatch requests succeed, expect a change notification
   * to have been sent for the path, even though the client has since
   * unsubscribed. */
  g_assert_cmpstr (change_log->str, ==, "/a/b/c:1::nil;");


  /* Clean up */
  dconf_engine_unref (engine);
  g_string_free (change_log, TRUE);
  change_log = NULL;
  g_variant_unref (triv);
}

static const gchar *match_request_type;
static gboolean got_match_request[5];

static GVariant *
handle_match_request (GBusType             bus_type,
                      const gchar         *bus_name,
                      const gchar         *object_path,
                      const gchar         *interface_name,
                      const gchar         *method_name,
                      GVariant            *parameters,
                      const GVariantType  *expected_type,
                      GError             **error)
{
  const gchar *match_rule;

  g_assert_cmpstr (bus_name, ==, "org.freedesktop.DBus");
  /* any object path works... */
  g_assert_cmpstr (interface_name, ==, "org.freedesktop.DBus");
  g_assert_cmpstr (method_name, ==, match_request_type);
  g_assert_cmpstr (g_variant_get_type_string (parameters), ==, "(s)");
  g_variant_get (parameters, "(&s)", &match_rule);
  g_assert_nonnull (strstr (match_rule, "arg0path='/a/b/c'"));
  g_assert_false (got_match_request[bus_type]);
  got_match_request[bus_type] = TRUE;

  return g_variant_new ("()");
}

static void
test_watch_sync (void)
{
  DConfEngine *engine;

  dconf_mock_dbus_sync_call_handler = handle_match_request;

  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);

  match_request_type = "AddMatch";

  /* A match rule should be added when the first subscription is established */
  dconf_engine_watch_sync (engine, "/a/b/c");
  g_assert_true (got_match_request[G_BUS_TYPE_SESSION]);
  g_assert_true (got_match_request[G_BUS_TYPE_SYSTEM]);
  got_match_request[G_BUS_TYPE_SESSION] = FALSE;
  got_match_request[G_BUS_TYPE_SYSTEM] = FALSE;

  /* The match rule is now already in place, so more are not needed */
  dconf_engine_watch_sync (engine, "/a/b/c");
  g_assert_false (got_match_request[G_BUS_TYPE_SESSION]);
  g_assert_false (got_match_request[G_BUS_TYPE_SYSTEM]);

  dconf_engine_watch_sync (engine, "/a/b/c");
  g_assert_false (got_match_request[G_BUS_TYPE_SESSION]);
  g_assert_false (got_match_request[G_BUS_TYPE_SYSTEM]);

  match_request_type = "RemoveMatch";

  /* There are 3 subscriptions, so removing 2 should not remove
   * the match rule */
  dconf_engine_unwatch_sync (engine, "/a/b/c");
  g_assert_false (got_match_request[G_BUS_TYPE_SESSION]);
  g_assert_false (got_match_request[G_BUS_TYPE_SYSTEM]);

  dconf_engine_unwatch_sync (engine, "/a/b/c");
  g_assert_false (got_match_request[G_BUS_TYPE_SESSION]);
  g_assert_false (got_match_request[G_BUS_TYPE_SYSTEM]);

  /* The match rule should be removed when the last subscription is
   * removed */
  dconf_engine_unwatch_sync (engine, "/a/b/c");
  g_assert_true (got_match_request[G_BUS_TYPE_SESSION]);
  g_assert_true (got_match_request[G_BUS_TYPE_SYSTEM]);
  got_match_request[G_BUS_TYPE_SESSION] = FALSE;
  got_match_request[G_BUS_TYPE_SYSTEM] = FALSE;

  dconf_engine_unref (engine);

  dconf_mock_dbus_sync_call_handler = NULL;
  match_request_type = NULL;
}

static void
test_change_fast (void)
{
  DConfChangeset *empty, *good_write, *good_write2, *bad_write, *very_good_write, *slightly_bad_write;
  GvdbTable *table, *locks;
  DConfEngine *engine;
  gboolean success;
  GError *error = NULL;
  GVariant *value;

  change_log = g_string_new (NULL);

  table = dconf_mock_gvdb_table_new ();
  locks = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_table_insert (locks, "/locked", g_variant_new_boolean (TRUE), NULL);
  dconf_mock_gvdb_table_insert (table, ".locks", NULL, locks);
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", table);

  empty = dconf_changeset_new ();
  good_write = dconf_changeset_new_write ("/value", g_variant_new_string ("value"));
  good_write2 = dconf_changeset_new_write ("/value2", g_variant_new_string ("value2"));
  bad_write = dconf_changeset_new_write ("/locked", g_variant_new_string ("value"));
  very_good_write = dconf_changeset_new_write ("/value", g_variant_new_string ("value"));
  dconf_changeset_set (very_good_write, "/to-reset", NULL);
  slightly_bad_write = dconf_changeset_new_write ("/locked", g_variant_new_string ("value"));
  dconf_changeset_set (slightly_bad_write, "/to-reset", NULL);

  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);

  success = dconf_engine_change_fast (engine, empty, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  success = dconf_engine_change_fast (engine, empty, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  success = dconf_engine_change_fast (engine, bad_write, NULL, &error);
  g_assert_error (error, DCONF_ERROR, DCONF_ERROR_NOT_WRITABLE);
  g_clear_error (&error);
  g_assert_false (success);

  success = dconf_engine_change_fast (engine, slightly_bad_write, NULL, &error);
  g_assert_error (error, DCONF_ERROR, DCONF_ERROR_NOT_WRITABLE);
  g_clear_error (&error);
  g_assert_false (success);

  /* Up to now, no D-Bus traffic should have been sent at all because we
   * only had trivial and non-writable attempts.
   *
   * Now try some working cases
   */
  dconf_mock_dbus_assert_no_async ();
  g_assert_cmpstr (change_log->str, ==, "");

  /* Verify that value is unset initially */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value");
  g_assert_null (value);

  /* Verify that value2 is unset initially */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value2");
  g_assert_null (value);

  /* change /value */
  success = dconf_engine_change_fast (engine, good_write, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  /* That should have emitted a synthetic change event */
  g_assert_cmpstr (change_log->str, ==, "/value:1::nil;");
  g_string_set_size (change_log, 0);

  /* Verify that the value is set */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "value");
  g_variant_unref (value);

  /* Repeat the same write for /value (which is already in the in_flight queue) */
  success = dconf_engine_change_fast (engine, good_write, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  /* Verify that /value is (still) set */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "value");
  g_variant_unref (value);

  /* That should not have emitted a synthetic change event, since the (local) value did not change */
  g_assert_cmpstr (change_log->str, ==, "");
  g_string_set_size (change_log, 0);

  /* change /value2 */
  success = dconf_engine_change_fast (engine, good_write2, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  /* That should have emitted a synthetic change event */
  g_assert_cmpstr (change_log->str, ==, "/value2:1::nil;");
  g_string_set_size (change_log, 0);

  /* Verify that /value2 is set */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value2");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "value2");
  g_variant_unref (value);

  /* change /value2 a second time */
  success = dconf_engine_change_fast (engine, good_write2, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  /* That should not have emitted a synthetic change event because the (local) value didn't change */
  g_assert_cmpstr (change_log->str, ==, "");

  /* Verify that /value2 is still set */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value2");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "value2");
  g_variant_unref (value);

  /* Fail the first attempted write.  This should cause a warning and a signal. */
  error = g_error_new_literal (G_FILE_ERROR, G_FILE_ERROR_NOENT, "something failed");
  dconf_mock_dbus_async_reply (NULL, error);
  g_clear_error (&error);
  g_assert_cmpstr (change_log->str, ==, "/value:1::nil;");
  g_string_set_size (change_log, 0);

  assert_pop_message ("dconf", G_LOG_LEVEL_WARNING, "failed to commit changes to dconf: something failed");

  /* Verify that /value is still set (because the second write is in progress) */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "value");
  g_variant_unref (value);

  /* Verify that /value2 is still set because the write is in progress */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value2");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "value2");
  g_variant_unref (value);

  /* Now allow the second set of writes to succeed */
  dconf_mock_dbus_async_reply (g_variant_new ("(s)", "tag"), NULL);

  /* Verify that /value became unset due to the in flight queue clearing */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value");
  g_assert_null (value);

  /* Verify that /value2 became unset due to the in flight queue clearing */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value2");
  g_assert_null (value);

  /* Now try a successful write */
  dconf_mock_dbus_assert_no_async ();
  g_assert_cmpstr (change_log->str, ==, "");

  success = dconf_engine_change_fast (engine, good_write, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  /* That should have emitted a synthetic change event */
  g_assert_cmpstr (change_log->str, ==, "/value:1::nil;");
  g_string_set_size (change_log, 0);

  /* Verify that the value is set */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "value");
  g_variant_unref (value);

  /* ACK the write. */
  error = g_error_new_literal (G_FILE_ERROR, G_FILE_ERROR_NOENT, "something failed");
  dconf_mock_dbus_async_reply (g_variant_new ("(s)", "tag"), NULL);
  g_clear_error (&error);
  /* No change this time, since we already did it. */
  g_assert_cmpstr (change_log->str, ==, "");

  /* Verify that the value became unset due to the in-flight queue
   * clearing... */
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "value");
  g_assert_null (value);

  /* Do that all again for a changeset with more than one item */
  dconf_mock_dbus_assert_no_async ();
  g_assert_cmpstr (change_log->str, ==, "");
  success = dconf_engine_change_fast (engine, very_good_write, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);
  g_assert_cmpstr (change_log->str, ==, "/:2:to-reset,value:nil;");
  g_string_set_size (change_log, 0);
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "value");
  g_variant_unref (value);
  error = g_error_new_literal (G_FILE_ERROR, G_FILE_ERROR_NOENT, "something failed");
  dconf_mock_dbus_async_reply (NULL, error);
  g_clear_error (&error);
  g_assert_cmpstr (change_log->str, ==, "/:2:to-reset,value:nil;");
  g_string_set_size (change_log, 0);
  assert_pop_message ("dconf", G_LOG_LEVEL_WARNING, "failed to commit changes to dconf: something failed");
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "value");
  g_assert_null (value);
  dconf_mock_dbus_assert_no_async ();
  g_assert_cmpstr (change_log->str, ==, "");
  success = dconf_engine_change_fast (engine, very_good_write, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);
  g_assert_cmpstr (change_log->str, ==, "/:2:to-reset,value:nil;");
  g_string_set_size (change_log, 0);
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "/value");
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "value");
  g_variant_unref (value);
  error = g_error_new_literal (G_FILE_ERROR, G_FILE_ERROR_NOENT, "something failed");
  dconf_mock_dbus_async_reply (g_variant_new ("(s)", "tag"), NULL);
  g_clear_error (&error);
  g_assert_cmpstr (change_log->str, ==, "");
  value = dconf_engine_read (engine, DCONF_READ_FLAGS_NONE, NULL, "value");
  g_assert_null (value);

  dconf_engine_unref (engine);

  dconf_changeset_unref (empty);
  dconf_changeset_unref (good_write);
  dconf_changeset_unref (very_good_write);
  dconf_changeset_unref (bad_write);
  dconf_changeset_unref (slightly_bad_write);
  g_string_free (change_log, TRUE);
  change_log = NULL;
}

/**
 * Tests that dconf_engine_change_fast() emits local optimistic change
 * notifications in the right circumstances
 */
static void
test_change_fast_redundant (void)
{
  DConfChangeset *change;
  DConfEngine *engine;
  change_log = g_string_new (NULL);

  // Initialise an empty engine
  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);

  // Send an empty changeset, which has no effect
  change = dconf_changeset_new ();
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "");

  // Reset the root directory, which has no effect since the database is empty
  change = dconf_changeset_new_write ("/", NULL);
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "");

  // Set apple to NULL, which has no effect because it was already unset
  change = dconf_changeset_new_write ("/apple", NULL);
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "");

  // Set apple to apple
  change = dconf_changeset_new_write ("/apple", g_variant_new_string ("apple"));
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "/apple:1::nil;");
  g_string_set_size (change_log, 0);

  // Set apple to apple, which has no effect because it is the same as the old value
  change = dconf_changeset_new_write ("/apple", g_variant_new_string ("apple"));
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "");
  g_string_set_size (change_log, 0);

  // Set apple to orange, which has an effect because it is different to the old value
  change = dconf_changeset_new_write ("/apple", g_variant_new_string ("orange"));
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "/apple:1::nil;");
  g_string_set_size (change_log, 0);

  // Set apple to NULL, which has an effect because it was previously set
  change = dconf_changeset_new_write ("/apple", NULL);
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "/apple:1::nil;");
  g_string_set_size (change_log, 0);

  // Set apple to apple
  change = dconf_changeset_new_write ("/apple", g_variant_new_string ("apple"));
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "/apple:1::nil;");
  g_string_set_size (change_log, 0);

  // Reset the root directory, which has an effect since the database is not empty
  change = dconf_changeset_new_write ("/", NULL);
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "/:1::nil;");
  g_string_set_size (change_log, 0);

  // Reset the root directory again, which has no effect since the database is empty
  change = dconf_changeset_new_write ("/", NULL);
  dconf_engine_change_fast (engine, change, NULL, NULL);
  dconf_changeset_unref (change);
  g_assert_cmpstr (change_log->str, ==, "");

  dconf_engine_unref (engine);
  g_string_free (change_log, TRUE);
  change_log = NULL;
}

static GError *change_sync_error;
static GVariant *change_sync_result;

static GVariant *
handle_write_request (GBusType             bus_type,
                      const gchar         *bus_name,
                      const gchar         *object_path,
                      const gchar         *interface_name,
                      const gchar         *method_name,
                      GVariant            *parameters,
                      const GVariantType  *expected_type,
                      GError             **error)
{
  g_assert_cmpstr (bus_name, ==, "ca.desrt.dconf");
  g_assert_cmpstr (interface_name, ==, "ca.desrt.dconf.Writer");

  /* Assume that the engine can format the method call properly, but
   * test that it can properly handle weird replies.
   */

  *error = change_sync_error;
  return change_sync_result;
}


static void
test_change_sync (void)
{
  DConfChangeset *empty, *good_write, *bad_write, *very_good_write, *slightly_bad_write;
  GvdbTable *table, *locks;
  DConfEngine *engine;
  gboolean success;
  GError *error = NULL;
  gchar *tag;

  table = dconf_mock_gvdb_table_new ();
  locks = dconf_mock_gvdb_table_new ();
  dconf_mock_gvdb_table_insert (locks, "/locked", g_variant_new_boolean (TRUE), NULL);
  dconf_mock_gvdb_table_insert (table, ".locks", NULL, locks);
  dconf_mock_gvdb_install (SYSCONFDIR "/dconf/db/site", table);

  empty = dconf_changeset_new ();
  good_write = dconf_changeset_new_write ("/value", g_variant_new_string ("value"));
  bad_write = dconf_changeset_new_write ("/locked", g_variant_new_string ("value"));
  very_good_write = dconf_changeset_new_write ("/value", g_variant_new_string ("value"));
  dconf_changeset_set (very_good_write, "/to-reset", NULL);
  slightly_bad_write = dconf_changeset_new_write ("/locked", g_variant_new_string ("value"));
  dconf_changeset_set (slightly_bad_write, "/to-reset", NULL);

  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);

  success = dconf_engine_change_sync (engine, empty, &tag, &error);
  g_assert_no_error (error);
  g_assert_true (success);
  g_free (tag);

  success = dconf_engine_change_sync (engine, empty, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  success = dconf_engine_change_sync (engine, bad_write, &tag, &error);
  g_assert_error (error, DCONF_ERROR, DCONF_ERROR_NOT_WRITABLE);
  g_clear_error (&error);
  g_assert_false (success);

  success = dconf_engine_change_sync (engine, slightly_bad_write, NULL, &error);
  g_assert_error (error, DCONF_ERROR, DCONF_ERROR_NOT_WRITABLE);
  g_clear_error (&error);
  g_assert_false (success);

  /* Up to now, no D-Bus traffic should have been sent at all because we
   * only had trivial and non-writable attempts.
   *
   * Now try some working cases
   */
  dconf_mock_dbus_sync_call_handler = handle_write_request;
  change_sync_result = g_variant_new ("(s)", "mytag");

  success = dconf_engine_change_sync (engine, good_write, &tag, &error);
  g_assert_no_error (error);
  g_assert_true (success);
  g_assert_cmpstr (tag, ==, "mytag");
  g_free (tag);
  change_sync_result = NULL;

  change_sync_error = g_error_new_literal (G_FILE_ERROR, G_FILE_ERROR_NOENT, "something failed");
  success = dconf_engine_change_sync (engine, very_good_write, &tag, &error);
  g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
  g_assert_false (success);
  g_clear_error (&error);
  change_sync_error = NULL;

  dconf_changeset_unref (empty);
  dconf_changeset_unref (good_write);
  dconf_changeset_unref (very_good_write);
  dconf_changeset_unref (bad_write);
  dconf_changeset_unref (slightly_bad_write);
  dconf_engine_unref (engine);
}

static void
send_signal (GBusType     type,
             const gchar *name,
             const gchar *path,
             const gchar *signame,
             const gchar *args)
{
  GVariant *value;

  value = g_variant_ref_sink (g_variant_new_parsed (args));
  dconf_engine_handle_dbus_signal (type, name, path, signame, value);
  g_variant_unref (value);
}

static void
test_signals (void)
{
  DConfEngine *engine;

  change_log = g_string_new (NULL);

  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);

  /* Throw some non-sense at it to make sure it gets rejected */

  /* Invalid signal name */
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "UnNotify", "('/', [''], 'tag')");
  send_signal (G_BUS_TYPE_SYSTEM, ":1.123", "/ca/desrt/dconf/Writer/site", "UnNotify", "('/', [''], 'tag')");
  g_assert_cmpstr (change_log->str, ==, "");
  /* Bad path */
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/use", "Notify", "('/', [''], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/use", "WritabilityNotify", "('/',)");
  send_signal (G_BUS_TYPE_SYSTEM, ":1.123", "/ca/desrt/dconf/Writer/sit", "Notify", "('/', [''], 'tag')");
  send_signal (G_BUS_TYPE_SYSTEM, ":1.123", "/ca/desrt/dconf/Writer/sit", "WritabilityNotify", "('/',)");
  g_assert_cmpstr (change_log->str, ==, "");
  /* Wrong signature for signal */
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/',)");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "WritabilityNotify", "('/', [''], '')");
  send_signal (G_BUS_TYPE_SYSTEM, ":1.123", "/ca/desrt/dconf/Writer/site", "Notify", "('/',)");
  send_signal (G_BUS_TYPE_SYSTEM, ":1.123", "/ca/desrt/dconf/Writer/site", "WritabilityNotify", "('/', [''], '')");
  g_assert_cmpstr (change_log->str, ==, "");
  /* Signal delivered on wrong bus type */
  send_signal (G_BUS_TYPE_SYSTEM, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/', [''], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/site", "Notify", "('/', [''], 'tag')");
  send_signal (G_BUS_TYPE_SYSTEM, ":1.123", "/ca/desrt/dconf/Writer/user", "WritabilityNotify", "('/',)");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/site", "WritabilityNotify", "('/',)");
  g_assert_cmpstr (change_log->str, ==, "");
  /* Empty changeset */
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/a', @as [], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/a/', @as [], 'tag')");
  send_signal (G_BUS_TYPE_SYSTEM, ":1.123", "/ca/desrt/dconf/Writer/site", "Notify", "('/a', @as [], 'tag')");
  send_signal (G_BUS_TYPE_SYSTEM, ":1.123", "/ca/desrt/dconf/Writer/site", "Notify", "('/a/', @as [], 'tag')");
  /* Try to notify on some invalid paths to make sure they get properly
   * rejected by the engine and not passed onto the user...
   */
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('', [''], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('a', [''], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('a/', [''], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/b//a/', [''], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/b//a', [''], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "WritabilityNotify", "('',)");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "WritabilityNotify", "('a',)");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "WritabilityNotify", "('a/',)");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "WritabilityNotify", "('/b//a/',)");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "WritabilityNotify", "('/b//a',)");
  g_assert_cmpstr (change_log->str, ==, "");
  /* Invalid gluing of segments: '/a' + 'b' != '/ab' */
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/a', ['b'], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/a', ['b', 'c'], 'tag')");
  g_assert_cmpstr (change_log->str, ==, "");
  /* Also: '/a' + '/b' != '/a/b' */
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/a', ['/b'], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/a', ['', '/b'], 'tag')");
  g_assert_cmpstr (change_log->str, ==, "");
  /* Invalid (non-relative) changes */
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/', ['/'], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/', ['/a'], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/', ['a', '/a'], 'tag')");
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify", "('/', ['a', 'a//b'], 'tag')");
  g_assert_cmpstr (change_log->str, ==, "");

  /* Now try some real cases */
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify",
               "('/', [''], 'tag')");
  g_assert_cmpstr (change_log->str, ==, "/:1::tag;");
  g_string_set_size (change_log, 0);
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify",
               "('/one/key', [''], 'tag')");
  g_assert_cmpstr (change_log->str, ==, "/one/key:1::tag;");
  g_string_set_size (change_log, 0);
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify",
               "('/two/', ['keys', 'here'], 'tag')");
  g_assert_cmpstr (change_log->str, ==, "/two/:2:keys,here:tag;");
  g_string_set_size (change_log, 0);
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "Notify",
               "('/some/path/', ['a', 'b/', 'c/d'], 'tag')");
  g_assert_cmpstr (change_log->str, ==, "/some/path/:3:a,b/,c/d:tag;");
  g_string_set_size (change_log, 0);
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "WritabilityNotify", "('/other/key',)");
  g_assert_cmpstr (change_log->str, ==, "w:/other/key:1::;");
  g_string_set_size (change_log, 0);
  send_signal (G_BUS_TYPE_SESSION, ":1.123", "/ca/desrt/dconf/Writer/user", "WritabilityNotify", "('/other/dir/',)");
  g_assert_cmpstr (change_log->str, ==, "w:/other/dir/:1::;");
  g_string_set_size (change_log, 0);

  dconf_engine_unref (engine);
}

static gboolean it_is_good_to_be_done;

static gpointer
waiter_thread (gpointer user_data)
{
  DConfEngine *engine = user_data;

  dconf_engine_sync (engine);

  g_assert_true (g_atomic_int_get (&it_is_good_to_be_done));

  return NULL;
}

static void
test_sync (void)
{
  GThread *waiter_threads[5];
  DConfChangeset *change;
  DConfEngine *engine;
  GError *error = NULL;
  gboolean success;
  gint i;

  engine = dconf_engine_new (SRCDIR "/profile/dos", NULL, NULL);

  /* Make sure a waiter thread returns straight away if nothing is
   * outstanding.
   */
  g_atomic_int_set (&it_is_good_to_be_done, TRUE);
  g_thread_join (g_thread_new ("waiter", waiter_thread, engine));
  g_atomic_int_set (&it_is_good_to_be_done, FALSE);

  /* The write will try to check the system-db for a lock.  That will
   * fail because it doesn't exist...
   */
  change = dconf_changeset_new_write ("/value", g_variant_new_boolean (TRUE));
  success = dconf_engine_change_fast (engine, change, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  assert_pop_message ("dconf", G_LOG_LEVEL_WARNING, "unable to open file");

  /* Spin up some waiters */
  for (i = 0; i < G_N_ELEMENTS (waiter_threads); i++)
    waiter_threads[i] = g_thread_new ("test waiter", waiter_thread, engine);
  g_usleep(100 * G_TIME_SPAN_MILLISECOND);
  /* Release them by completing the pending async call */
  g_atomic_int_set (&it_is_good_to_be_done, TRUE);
  dconf_mock_dbus_async_reply (g_variant_new ("(s)", "tag"), NULL);
  /* Make sure they all quit by joining them */
  for (i = 0; i < G_N_ELEMENTS (waiter_threads); i++)
    g_thread_join (waiter_threads[i]);
  g_atomic_int_set (&it_is_good_to_be_done, FALSE);

  /* Do the same again, but with a failure as a result */
  success = dconf_engine_change_fast (engine, change, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);
  for (i = 0; i < G_N_ELEMENTS (waiter_threads); i++)
    waiter_threads[i] = g_thread_new ("test waiter", waiter_thread, engine);
  g_usleep(100 * G_TIME_SPAN_MILLISECOND);
  error = g_error_new_literal (G_FILE_ERROR, G_FILE_ERROR_NOENT, "some error");
  g_atomic_int_set (&it_is_good_to_be_done, TRUE);
  dconf_mock_dbus_async_reply (NULL, error);
  g_clear_error (&error);

  assert_pop_message ("dconf", G_LOG_LEVEL_WARNING, "failed to commit changes to dconf: some error");

  /* Make sure they all quit by joining them */
  for (i = 0; i < G_N_ELEMENTS (waiter_threads); i++)
    g_thread_join (waiter_threads[i]);
  g_atomic_int_set (&it_is_good_to_be_done, FALSE);

  /* Now put two changes in the queue and make sure we have to reply to
   * both of them before the waiters finish.
   */
  success = dconf_engine_change_fast (engine, change, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);
  success = dconf_engine_change_fast (engine, change, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (success);
  for (i = 0; i < G_N_ELEMENTS (waiter_threads); i++)
    waiter_threads[i] = g_thread_new ("test waiter", waiter_thread, engine);
  g_usleep(100 * G_TIME_SPAN_MILLISECOND);
  dconf_mock_dbus_async_reply (g_variant_new ("(s)", "tag1"), NULL);
  /* Still should not have quit yet... wait a bit to let the waiters try
   * to shoot themselves in their collective feet...
   */
  g_usleep(100 * G_TIME_SPAN_MILLISECOND);
  /* Will be OK after the second reply */
  g_atomic_int_set (&it_is_good_to_be_done, TRUE);
  dconf_mock_dbus_async_reply (g_variant_new ("(s)", "tag2"), NULL);
  /* Make sure they all quit by joining them */
  for (i = 0; i < G_N_ELEMENTS (waiter_threads); i++)
    g_thread_join (waiter_threads[i]);
  g_atomic_int_set (&it_is_good_to_be_done, FALSE);

  dconf_changeset_unref (change);
  dconf_engine_unref (engine);
  dconf_mock_shm_reset ();
}

/* Log handling. */
typedef struct
{
  GLogLevelFlags log_level;
  GLogField *fields;
  gsize n_fields;
} LogMessage;

static void
log_message_clear (LogMessage *message)
{
  gsize i;

  for (i = 0; i < message->n_fields; i++)
    {
      g_free ((gpointer) message->fields[i].key);
      g_free ((gpointer) message->fields[i].value);
    }
}

static GArray *logged_messages = NULL;

static GLogWriterOutput
log_writer_cb (GLogLevelFlags   log_level,
               const GLogField *fields,
               gsize            n_fields,
               gpointer         user_data)
{
  LogMessage *message;
  gsize i;

  /* If we’re running as a subprocess, the parent process is going to be
   * checking our stderr, so just behave normally. */
  if (g_test_subprocess ())
    return g_log_writer_default (log_level, fields, n_fields, user_data);

  /* We only care about dconf messages and non-debug messages. */
  if (log_level == G_LOG_LEVEL_DEBUG)
    return G_LOG_WRITER_HANDLED;

  for (i = 0; i < n_fields; i++)
    {
      if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0 &&
          g_strcmp0 (fields[i].value, "dconf") != 0)
        return G_LOG_WRITER_HANDLED;
    }

  /* Append the message to the queue. */
  g_array_set_size (logged_messages, logged_messages->len + 1);
  message = &g_array_index (logged_messages, LogMessage, logged_messages->len - 1);

  message->log_level = log_level;
  message->fields = g_new0 (GLogField, n_fields);
  message->n_fields = n_fields;

  for (i = 0; i < n_fields; i++)
    {
      gsize length = (fields[i].length < 0) ? strlen (fields[i].value) + 1 : fields[i].length;
      message->fields[i].key = g_strdup (fields[i].key);
      message->fields[i].value = g_malloc0 (length);
      memcpy ((gpointer) message->fields[i].value, fields[i].value, length);
      message->fields[i].length = fields[i].length;
    }

  return G_LOG_WRITER_HANDLED;
}

/* Assert there are no logged messages in the queue. */
static void
assert_no_messages (void)
{
  g_assert_cmpuint (logged_messages->len, ==, 0);
}

/* Assert there is at least one logged message in the queue, and the oldest
 * logged message matches the given expectations. If so, pop it from the queue;
 * if not, abort. */
static void
assert_pop_message (const gchar    *expected_domain,
                    GLogLevelFlags  expected_log_level,
                    const gchar    *expected_message_fragment)
{
  const LogMessage *logged_message;
  gsize i;
  const gchar *message = NULL, *domain = NULL;

  g_assert_cmpuint (logged_messages->len, >, 0);
  logged_message = &g_array_index (logged_messages, LogMessage, 0);

  for (i = 0; i < logged_message->n_fields; i++)
    {
      if (g_strcmp0 (logged_message->fields[i].key, "MESSAGE") == 0)
        message = logged_message->fields[i].value;
      else if (g_strcmp0 (logged_message->fields[i].key, "GLIB_DOMAIN") == 0)
        domain = logged_message->fields[i].value;
    }

  g_assert_cmpstr (domain, ==, expected_domain);
  g_assert_cmpuint (logged_message->log_level, ==, expected_log_level);
  g_assert_cmpstr (strstr (message, expected_message_fragment), !=, NULL);

  g_array_remove_index (logged_messages, 0);
}

/* If there is at least one logged message in the queue, act like
 * assert_pop_message(). Otherwise, if the queue is empty, return. */
static void
assert_maybe_pop_message (const gchar    *expected_domain,
                          GLogLevelFlags  expected_log_level,
                          const gchar    *expected_message_fragment)
{
  if (logged_messages->len == 0)
    return;

  assert_pop_message (expected_domain, expected_log_level, expected_message_fragment);
}

int
main (int argc, char **argv)
{
  const ProfileParserOpenData profile_parser0 =
    { SRCDIR "/profile/this-file-does-not-exist", "*WARNING*: unable to open named profile*" };
  const ProfileParserOpenData profile_parser1 =
    { SRCDIR "/profile/broken-profile", "*WARNING*: unknown dconf database*unknown dconf database*" };
  const ProfileParserOpenData profile_parser2 =
    { SRCDIR "/profile/gdm", "*WARNING*: unknown dconf database*unknown dconf database*" };
  int retval;

  g_setenv ("XDG_RUNTIME_DIR", "/RUNTIME/", TRUE);
  g_setenv ("XDG_CONFIG_HOME", "/HOME/.config", TRUE);
  g_unsetenv ("DCONF_PROFILE");

  logged_messages = g_array_new (FALSE, FALSE, sizeof (LogMessage));
  g_array_set_clear_func (logged_messages, (GDestroyNotify) log_message_clear);
  g_log_set_writer_func (log_writer_cb, NULL, NULL);

  main_thread = g_thread_self ();

  g_test_init (&argc, &argv, NULL);

  g_test_add_data_func ("/engine/profile-parser/errors/0", &profile_parser0, test_profile_parser_errors);
  g_test_add_data_func ("/engine/profile-parser/errors/1", &profile_parser1, test_profile_parser_errors);
  g_test_add_data_func ("/engine/profile-parser/errors/2", &profile_parser2, test_profile_parser_errors);
  g_test_add_func ("/engine/profile-parser", test_profile_parser);
  g_test_add_func ("/engine/signal-threadsafety", test_signal_threadsafety);
  g_test_add_func ("/engine/sources/user", test_user_source);
  g_test_add_func ("/engine/sources/system", test_system_source);
  g_test_add_func ("/engine/sources/file", test_file_source);
  g_test_add_func ("/engine/sources/service", test_service_source);
  g_test_add_func ("/engine/read", test_read);
  g_test_add_func ("/engine/watch/fast", test_watch_fast);
  g_test_add_func ("/engine/watch/fast/simultaneous", test_watch_fast_simultaneous_subscriptions);
  g_test_add_func ("/engine/watch/fast/successive", test_watch_fast_successive_subscriptions);
  g_test_add_func ("/engine/watch/fast/short_lived", test_watch_fast_short_lived_subscriptions);
  g_test_add_func ("/engine/watch/sync", test_watch_sync);
  g_test_add_func ("/engine/change/fast", test_change_fast);
  g_test_add_func ("/engine/change/fast_redundant", test_change_fast_redundant);
  g_test_add_func ("/engine/change/sync", test_change_sync);
  g_test_add_func ("/engine/signals", test_signals);
  g_test_add_func ("/engine/sync", test_sync);

  retval = g_test_run ();

  assert_no_messages ();
  g_array_unref (logged_messages);

  return retval;
}
