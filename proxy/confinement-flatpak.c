/*
 * Copyright © 2016 Canonical Limited
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
 * Author: Allison Lortie <desrt@desrt.ca>
 */

#define _GNU_SOURCE

#include "confinement.h"

#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static gboolean
verify_tmpfs (gint      fd,
              gboolean *out_is_tmpfs)
{
  struct statfs buf;

  if (fstatfs (fd, &buf) != 0)
    return FALSE;

  *out_is_tmpfs = (buf.f_type == TMPFS_MAGIC);

  return TRUE;
}

static gboolean
verify_regular (gint      fd,
                gboolean *out_is_regular,
                gsize    *out_size)
{
  struct stat buf;

  if (fstat (fd, &buf) != 0)
    return FALSE;

  *out_is_regular = S_ISREG (buf.st_mode);
  *out_size = buf.st_size;

  return TRUE;
}

static void
fd_clear (gint *fd)
{
  if (*fd != -1)
    close (*fd);
}

typedef gint fd;
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(fd, fd_clear)

static gboolean
get_flatpak_info_keyfile (guint      pid,
                          GKeyFile **out_keyfile)
{
  g_autoptr(GKeyFile) keyfile = NULL;
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(fd) root_fd = -1;
  g_auto(fd) info_fd = -1;
  gboolean is_regular;
  gboolean is_tmpfs;
  gchar rootdir[40];
  gsize size;

  snprintf (rootdir, sizeof rootdir, "/proc/%u/root", pid);
  root_fd = open (rootdir, O_DIRECTORY | O_PATH);

  if (root_fd == -1)
    {
      g_warning ("pid %u: cannot access root filesystem: %s", pid, g_strerror (errno));
      return FALSE;
    }

  /* <desrt> do you guarantee for always and forever that the root fs of a flatpak app is tmpfs?
   * <alexlarsson> yes
   */
  if (!verify_tmpfs (root_fd, &is_tmpfs))
    {
      g_warning ("pid %u: fstatfs() on root filesystem failed: %s", pid, g_strerror (errno));
      return FALSE;
    }

  if (!is_tmpfs)
    {
      /* Unconfined */

      *out_keyfile = NULL;
      return TRUE;
    }

  info_fd = openat (root_fd, ".flatpak-info", O_RDONLY | O_NOCTTY);
  if (info_fd == -1 && errno == ENOENT)
    {
      /* Unconfined */
      *out_keyfile = NULL;
      return TRUE;
    }

  /* This is now surely a flatpak-confined application.  We only have two
   * options past this point: failure, or returning a non-NULL GKeyFile.
   */

  if (info_fd == -1)
    {
      g_warning ("pid %u: failed to open .flatpak-info file: %s", pid, g_strerror (errno));
      return FALSE;
    }

  if (!verify_regular (info_fd, &is_regular, &size))
    {
      g_warning ("pid %u: fstat() on .flatpak-info file failed: %s", pid, g_strerror (errno));
      return FALSE;
    }

  if (!is_regular)
    {
      g_warning ("pid %u: .flatpak-info is not regular file", pid);
      return FALSE;
    }

  if (size > 1000000)
    {
      g_warning ("pid %u: .flatpak-info file is unreasonably large", pid);
      return FALSE;
    }

  contents = g_malloc (size);
  if (read (info_fd, contents, size) != size)
    {
      /* "No error" in this case means that the size changed */
      g_warning ("pid %u: failed to read entire .flatpak-info file: %s", pid, g_strerror (errno));
      return FALSE;
    }

  keyfile = g_key_file_new ();
  if (!g_key_file_load_from_data (keyfile, contents, size, G_KEY_FILE_NONE, &error))
    {
      g_warning ("pid %u: cannot parse .flatpak-info contents: %s", pid, error->message);
      return FALSE;
    }

  *out_keyfile = g_steal_pointer (&keyfile);

  return TRUE;
}

gboolean
confinement_check_flatpak (GVariant    *credentials,
                           gboolean    *out_is_confined,
                           Permissions *out_permissions)
{
  g_autoptr(GKeyFile) keyfile = NULL;
  g_autoptr(GError) error = NULL;
  gchar *appid;
  guint pid;

  if (!g_variant_lookup (credentials, "ProcessID", "u", &pid))
    {
      g_warning ("Caller credentials are missing ProcessID field");
      return FALSE;
    }

  if (!get_flatpak_info_keyfile (pid, &keyfile))
    /* this will throw its own g_warning() */
    return FALSE;

  if (keyfile == NULL)
    {
      /* Everything went OK, but we didn't find a keyfile there.  As far
       * as flatpak is concerned, this app is unconfined.
       */
      *out_is_confined = FALSE;
      return TRUE;
    }

  appid = g_key_file_get_string (keyfile, "Application", "name", &error);
  if (appid == NULL)
    {
      g_warning ("pid %u: .flatpak-info: %s", pid, error->message);
      return FALSE;
    }

  /* We will have success now, even if we don't find the dconf keys (in
   * which case there is simply no permissions to access dconf and we
   * share an empty database).
   */
  permission_list_init (&out_permissions->readable,
                        g_key_file_get_string_list (keyfile, "Policy dconf", "readable", NULL, NULL));
  permission_list_init (&out_permissions->writable,
                        g_key_file_get_string_list (keyfile, "Policy dconf", "writable", NULL, NULL));
  out_permissions->ipc_dir = g_build_filename (g_get_user_runtime_dir (), "app", appid, "dconf", NULL);
  out_permissions->app_id = appid;

  *out_is_confined = TRUE;

  return TRUE;
}
