/*
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "dconf-shm.h"

#include <glib.h>

#include <sys/statfs.h>

#ifndef ECRYPTFS_SUPER_MAGIC
#define ECRYPTFS_SUPER_MAGIC 0xf15f
#endif

#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif

/* returns TRUE if the filesystem is capable */
static gboolean
dconf_shm_check (const gchar *filename)
{
  struct statfs buf;

  if (statfs (filename, &buf) != 0)
    return FALSE;

  return buf.f_type != NFS_SUPER_MAGIC && buf.f_type != ECRYPTFS_SUPER_MAGIC;
}

gboolean
dconf_shm_homedir_is_native (void)
{
  static gsize homedir_is_native;

  if (g_once_init_enter (&homedir_is_native))
    {
      gboolean is_native;

      is_native = dconf_shm_check (g_get_home_dir ());

      g_once_init_leave (&homedir_is_native, is_native + 1);
    }

  return homedir_is_native - 1;
}
