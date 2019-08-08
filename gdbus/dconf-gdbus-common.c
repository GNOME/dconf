#include "../engine/dconf-engine.h"

void
dconf_engine_dbus_handle_connection_closed (GDBusConnection *connection,
                                            gboolean         remote_peer_vanished,
                                            GError          *error,
                                            GMutex          *bus_lock,
                                            gboolean        *bus_is_error,
                                            gpointer        *bus_data,
                                            GCallback        bus_closed_callback,
                                            gpointer         bus_closed_callback_user_data)
{
  g_return_if_fail (connection != NULL);
  g_return_if_fail (bus_is_error != NULL);
  g_return_if_fail (bus_data != NULL);

  g_debug ("D-Bus connection closed, invalidating cache: %s",
           error != NULL ? error->message :
             (remote_peer_vanished == FALSE ? "Close requested" : "Unknown reason"));

  g_mutex_lock (bus_lock);

  if (bus_closed_callback)
    g_signal_handlers_disconnect_by_func (connection,
                                          bus_closed_callback,
                                          bus_closed_callback_user_data);

  if (*bus_is_error)
    {
      g_clear_error ((GError **) bus_data);
      *bus_is_error = FALSE;
    }
  else
    {
      g_assert (connection == *bus_data);
      *bus_data = NULL;
    }

  g_object_unref (connection);

  g_mutex_unlock (bus_lock);
}
