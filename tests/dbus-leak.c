#include "../client/dconf-client.h"
#include "../engine/dconf-engine.h"

static void
test_engine_dbus_call (void)
{
  DConfClient *client;
  GError *error = NULL;
  GVariant *reply;

  /* Invoking dconf_engine_dbus_call_sync_func() without initializing
   * the dbus engine will fail with DCONF_ERROR_FAILED.
   */
  reply = dconf_engine_dbus_call_sync_func (G_BUS_TYPE_SESSION,
                                            "org.freedesktop.DBus", "/", "org.freedesktop.DBus", "ListNames",
                                            g_variant_new ("()"), G_VARIANT_TYPE ("(as)"), &error);
  g_assert_error (error, DCONF_ERROR, DCONF_ERROR_FAILED);
  g_assert_null (reply);
  g_clear_error (&error);

  /* Creating a DConfClient object will force the initialization of
   * the dbus engine when creating the internal DConfEngine object.
   */
  client = dconf_client_new ();

  /* Now that we initialized the dbus engine, force a call to the engine to
   * make sure at least one GDBusConnection is cached.
   */
  reply = dconf_engine_dbus_call_sync_func (G_BUS_TYPE_SESSION,
                                            "org.freedesktop.DBus", "/", "org.freedesktop.DBus", "ListNames",
                                            g_variant_new ("()"), G_VARIANT_TYPE ("(as)"), &error);
  g_assert_no_error (error);
  g_assert (reply != NULL);
  g_assert (g_variant_is_of_type (reply, G_VARIANT_TYPE ("(as)")));
  g_variant_unref (reply);

  /* Unreffing the last client should de-init the dbus engine given the last
   * internal DConfEngine object should also be destroyed when the last client
   * is destroyed.
   */
  g_object_unref (client);

  /* Invoking dconf_engine_dbus_call_sync_func() again should fail as
   * unreffing the last client should de-init the dbus engine.
   */
  reply = dconf_engine_dbus_call_sync_func (G_BUS_TYPE_SESSION,
                                            "org.freedesktop.DBus", "/", "org.freedesktop.DBus", "ListNames",
                                            g_variant_new ("()"), G_VARIANT_TYPE ("(as)"), &error);
  g_assert_error (error, DCONF_ERROR, DCONF_ERROR_FAILED);
  g_assert_null (reply);
  g_clear_error (&error);
}

int
main (int argc, char **argv)
{
  GTestDBus *test_bus;
  int res;

  g_test_init (&argc, &argv, NULL);

  dconf_engine_dbus_init_for_testing ();

  g_test_add_func ("/dbus/engine-dbus-call", test_engine_dbus_call);

  test_bus = g_test_dbus_new (G_TEST_DBUS_NONE);

  g_test_dbus_up (test_bus);

  res = g_test_run ();

  /* g_test_dbus_down will fail if GDBusConnection leaks */
  g_test_dbus_down (test_bus);

  return res;
}
