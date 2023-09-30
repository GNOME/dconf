#define _DEFAULT_SOURCE
#include "../client/dconf-client.h"
#include "../engine/dconf-engine.h"
#include "dconf-mock.h"
#include <string.h>
#include <stdlib.h>

static GThread *main_thread;

static void
test_lifecycle (void)
{
  DConfClient *client;
  GWeakRef weak;

  client = dconf_client_new ();
  g_weak_ref_init (&weak, client);
  g_object_unref (client);

  g_assert_null (g_weak_ref_get (&weak));
  g_weak_ref_clear (&weak);
}

static gboolean changed_was_called;

static void
changed (DConfClient         *client,
         const gchar         *prefix,
         const gchar * const *changes,
         const gchar         *tag,
         gpointer             user_data)
{
  g_assert_true (g_thread_self () == main_thread);

  changed_was_called = TRUE;
}

static void
check_and_free (GVariant *to_check,
                GVariant *expected)
{
  if (expected)
    {
      g_variant_ref_sink (expected);
      g_assert_nonnull (to_check);

      g_assert_cmpvariant (to_check, expected);
      g_variant_unref (to_check);
      g_variant_unref (expected);
    }
  else
    g_assert_null (to_check);
}

static void
queue_up_100_writes (DConfClient *client)
{
  gint i;

  /* We send 100 writes, letting them pile up.
   * At no time should there be more than one write on the wire.
   */
  for (i = 0; i < 100; i++)
    {
      changed_was_called = FALSE;
      dconf_client_write_fast (client, "/test/value", g_variant_new_int32 (i), NULL);
      g_assert_true (changed_was_called);

      /* We should always see the most recently written value. */
      check_and_free (dconf_client_read (client, "/test/value"), g_variant_new_int32 (i));
      check_and_free (dconf_client_read_full (client, "/test/value", DCONF_READ_DEFAULT_VALUE, NULL), NULL);
    }

  g_assert_cmpint (g_queue_get_length (&dconf_mock_dbus_outstanding_call_handles), ==, 1);
}

static void
fail_one_call (void)
{
  DConfEngineCallHandle *handle;
  GError *error;

  error = g_error_new_literal (G_FILE_ERROR, G_FILE_ERROR_NOENT, "--expected error from testcase--");
  handle = g_queue_pop_head (&dconf_mock_dbus_outstanding_call_handles);
  dconf_engine_call_handle_reply (handle, NULL, error);
  g_error_free (error);
}

static GLogWriterOutput
log_writer_cb (GLogLevelFlags   log_level,
               const GLogField *fields,
               gsize            n_fields,
               gpointer         user_data)
{
  gsize i;

  for (i = 0; i < n_fields; i++)
    {
      if (g_strcmp0 (fields[i].key, "MESSAGE") == 0 &&
          strstr (fields[i].value, "--expected error from testcase--"))
        return G_LOG_WRITER_HANDLED;
    }

  return G_LOG_WRITER_UNHANDLED;
}

static void
test_fast (void)
{
  DConfClient *client;

  g_log_set_writer_func (log_writer_cb, NULL, NULL);

  client = dconf_client_new ();
  g_signal_connect (client, "changed", G_CALLBACK (changed), NULL);

  queue_up_100_writes (client);

  /* Start indicating that the writes failed.
   *
   * Because of the pending-merge logic, we should only have had to fail two calls.
   *
   * Each time, we should see a change notify.
   */

  g_assert_cmpint (g_queue_get_length (&dconf_mock_dbus_outstanding_call_handles), == , 1);

  changed_was_called = FALSE;
  fail_one_call ();
  g_assert_true (changed_was_called);

  /* For the first failure, we should continue to see the most recently written value (99) */
  check_and_free (dconf_client_read (client, "/test/value"), g_variant_new_int32 (99));
  check_and_free (dconf_client_read_full (client, "/test/value", DCONF_READ_DEFAULT_VALUE, NULL), NULL);

  g_assert_cmpint (g_queue_get_length (&dconf_mock_dbus_outstanding_call_handles), == , 1);

  changed_was_called = FALSE;
  fail_one_call ();
  g_assert_true (changed_was_called);

  /* Should read back now as NULL */
  check_and_free (dconf_client_read (client, "/test/value"), NULL);
  check_and_free (dconf_client_read_full (client, "/test/value", DCONF_READ_DEFAULT_VALUE, NULL), NULL);

  g_assert_cmpint (g_queue_get_length (&dconf_mock_dbus_outstanding_call_handles), == , 0);

  /* Cleanup */
  g_signal_handlers_disconnect_by_func (client, changed, NULL);
  g_object_unref (client);
}

static gboolean changed_a, changed_b, changed_c;

static void
coalesce_changed (DConfClient         *client,
                  const gchar         *prefix,
                  const gchar * const *changes,
                  const gchar         *tag,
                  gpointer             user_data)
{
  changed_a = g_str_equal (prefix, "/test/a") || g_strv_contains (changes, "a");
  changed_b = g_str_equal (prefix, "/test/b") || g_strv_contains (changes, "b");
  changed_c = g_str_equal (prefix, "/test/c") || g_strv_contains (changes, "c");
}

static void
test_coalesce (void)
{
  gint i, a, b, c;
  gboolean should_change_a, should_change_b, should_change_c;
  g_autoptr(DConfClient) client = NULL;

  gint changes[][3] = {
    {1, 0, 0},
    {1, 1, 1},
    {0, 1, 1},
    {0, 0, 1},
    {0, 0, 0},
    {1, 0, 0},
    {1, 0, 0},
  };

  client = dconf_client_new ();
  g_signal_connect (client, "changed", G_CALLBACK (coalesce_changed), NULL);

  a = b = c = 0;

  for (i = 0; i != G_N_ELEMENTS (changes); ++i)
    {
      g_autoptr(DConfChangeset) changeset = NULL;

      should_change_a = changes[i][0];
      should_change_b = changes[i][1];
      should_change_c = changes[i][2];

      changeset = dconf_changeset_new ();

      if (should_change_a)
        dconf_changeset_set (changeset, "/test/a", g_variant_new_int32 (++a));
      if (should_change_b)
        dconf_changeset_set (changeset, "/test/b", g_variant_new_int32 (++b));
      if (should_change_c)
        dconf_changeset_set (changeset, "/test/c", g_variant_new_int32 (++c));

      changed_a = changed_b = changed_c = FALSE;

      g_assert_true (dconf_client_change_fast (client, changeset, NULL));

      /* Notifications should be only about keys we have just written. */
      g_assert_cmpint (should_change_a, ==, changed_a);
      g_assert_cmpint (should_change_b, ==, changed_b);
      g_assert_cmpint (should_change_c, ==, changed_c);

      /* We should see value from the most recent write or NULL if we haven't written it yet. */
      check_and_free (dconf_client_read (client, "/test/a"), a == 0 ? NULL : g_variant_new_int32 (a));
      check_and_free (dconf_client_read (client, "/test/b"), b == 0 ? NULL : g_variant_new_int32 (b));
      check_and_free (dconf_client_read (client, "/test/c"), c == 0 ? NULL : g_variant_new_int32 (c));
    }

  dconf_mock_dbus_async_reply (g_variant_new ("(s)", "1"), NULL);
  dconf_mock_dbus_async_reply (g_variant_new ("(s)", "2"), NULL);

  /* There should be no more requests since all but first have been
   * coalesced together. */
  dconf_mock_dbus_assert_no_async ();

  /* Cleanup */
  g_signal_handlers_disconnect_by_func (client, changed, NULL);
}

int
main (int argc, char **argv)
{
  setenv ("DCONF_PROFILE", SRCDIR "/profile/will-never-exist", TRUE);

  main_thread = g_thread_self ();

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/client/lifecycle", test_lifecycle);
  g_test_add_func ("/client/basic-fast", test_fast);
  g_test_add_func ("/client/coalesce", test_coalesce);

  return g_test_run ();
}
