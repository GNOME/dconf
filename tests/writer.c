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

#include "service/dconf-generated.h"
#include "service/dconf-writer.h"

static void
test_writer_basic (void)
{
  g_autoptr(DConfWriter) writer = NULL;

  writer = (DConfWriter *) dconf_writer_new (DCONF_TYPE_KEYFILE_WRITER, "some-name");
  g_assert_nonnull (writer);

  g_assert_cmpstr (dconf_writer_get_name (writer), ==, "some-name");
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/writer/basic", test_writer_basic);

  return g_test_run ();
}
