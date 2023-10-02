#include "../engine/dconf-engine.h"

static void
test_engine_dbus_call (void)
{
  GError *error = NULL;
  GVariant *reply;

  /* Force a call to the engine to make sure at least one GDBusConnection
   * is cached.
   */
  reply = dconf_engine_dbus_call_sync_func (G_BUS_TYPE_SESSION,
                                            "org.freedesktop.DBus", "/", "org.freedesktop.DBus", "ListNames",
                                            g_variant_new ("()"), G_VARIANT_TYPE ("(as)"), &error);
  g_assert_no_error (error);
  g_assert_nonnull (reply);
  g_assert_true (g_variant_is_of_type (reply, G_VARIANT_TYPE ("(as)")));
  g_variant_unref (reply);
}

int
main (int argc, char **argv)
{
  GTestDBus *test_bus;
  int res;

  g_test_init (&argc, &argv, NULL);

  dconf_engine_dbus_init_for_testing ();

  g_test_add_func (DBUS_BACKEND "/dbus/engine-dbus-call", test_engine_dbus_call);

  test_bus = g_test_dbus_new (G_TEST_DBUS_NONE);

  g_test_dbus_up (test_bus);

  res = g_test_run ();

  /* g_test_dbus_down will fail if GDBusConnection leaks */
  g_test_dbus_down (test_bus);

  g_object_unref (test_bus);

  return res;
}
