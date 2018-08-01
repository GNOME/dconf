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

#include "service/dconf-writer.h"

/* Test basic initialisation of a #DConfWriter. This is essentially a smoketest. */
static void
test_writer_basic (void)
{
  g_autoptr(DConfWriter) writer = NULL;

  writer = DCONF_WRITER (dconf_writer_new (DCONF_TYPE_KEYFILE_WRITER, "some-name"));
  g_assert_nonnull (writer);

  g_assert_cmpstr (dconf_writer_get_name (writer), ==, "some-name");
}

/* TODO */
static void
test_writer_corrupt_file (void)
{
  g_autoptr(DConfWriter) writer = NULL;
  gboolean retval;
  g_autoptr(GError) local_error = NULL;

  writer = DCONF_WRITER (dconf_writer_new (DCONF_TYPE_KEYFILE_WRITER, "some-name"));
  g_assert_nonnull (writer);

  writer_iface = DCONF_DBUS_WRITER_GET_IFACE (writer);
  retval = writer_iface->handle_init (DCONF_DBUS_WRITER (writer), invocation);

  /* TODO: this is arse */
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/writer/basic", test_writer_basic);
  g_test_add_func ("/writer/corrupt-file", test_writer_corrupt_file);

  return g_test_run ();
}
