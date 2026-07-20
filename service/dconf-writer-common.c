/*
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
 */

#include "config.h"

#include "dconf-writer-common.h"

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>

gint
dconf_lock_file_lock (const gchar  *lock_filename,
                      GError      **error)
{
  gint lock_fd;

  lock_fd = open (lock_filename, O_RDWR | O_CREAT, 0666);
  if (lock_fd == -1)
    {
      gchar *dirname;

      /* Maybe it failed because the directory doesn't exist.  Try
       * again, after mkdir().
       */
      dirname = g_path_get_dirname (lock_filename);
      g_mkdir_with_parents (dirname, 0700);
      g_free (dirname);

      lock_fd = open (lock_filename, O_RDWR | O_CREAT, 0666);
      if (lock_fd == -1)
        {
          gint saved_errno = errno;

          g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                       "%s: %s", lock_filename, g_strerror (saved_errno));

          return lock_fd;
        }
    }

  while (TRUE)
    {
      struct flock lock = { 0 };

      lock.l_type = F_WRLCK;
      lock.l_whence = 0;
      lock.l_start = 0;
      lock.l_len = 0; /* lock all bytes */

      if (fcntl (lock_fd, F_SETLKW, &lock) == 0)
        break;

      if (errno != EINTR)
        {
          gint saved_errno = errno;

          g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                       "%s: unable to fcntl(F_SETLKW): %s", lock_filename, g_strerror (saved_errno));
          close (lock_fd);
          lock_fd = -1;
          return lock_fd;
        }

      /* it was EINTR.  loop again. */
    }

  return lock_fd;
}
