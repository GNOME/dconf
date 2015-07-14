/*
 * Copyright © 2012 Canonical Limited
 * Copyright © 2015 Red Hat Inc.
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
 * Authors: Ryan Lortie <desrt@desrt.ca>
 *          Alberto Ruiz <aruiz@redhat.com>
 */

#include "pam_dconf.h"

static char*
join_strings (pam_handle_t *pamh,
              const char   *base,
              const char   *suffix,
              const char   *file)
{
  char *result;

  result = (char*)malloc (sizeof (char)*(strlen (base) + strlen (suffix) + strlen (file) + 1));
  if (result == NULL)
    {
      pam_syslog (pamh, LOG_ERR, "Could not allocate memory");
      return NULL;
    }

  if (sprintf (result, "%s%s%s", base, suffix, file) < 0)
    {
      pam_syslog (pamh, LOG_ERR, "There was an error calling sprintf");
      free (result);
      return NULL;
    }
  return result;
}

static char*
username_profile_name (pam_handle_t *pamh)
{
  const char *user;
  int         ret;

  ret = pam_get_user (pamh, &user, "");
  if (ret != PAM_SUCCESS)
    {
      pam_syslog (pamh, LOG_ERR, "Could not get username");
      return NULL;
    }

  return join_strings (pamh, user, DCONF_PROFILE_SUFFIX, "");
}

static char*
find_file_in_dir (pam_handle_t *pamh,
                  const char   *basedir,
                  const char   *dconfdir,
                  const char   *filename)
{
  char *file_full_path;

  file_full_path = join_strings (pamh, basedir, dconfdir, filename);
  if (access (file_full_path, F_OK) != -1)
    return file_full_path;

  free (file_full_path);
  return NULL;
}

static char*
get_dconf_profile_path (pam_handle_t *pamh)
{
  char *dirs     = NULL;
  char *result   = NULL;
  char *filename = NULL;
  char *dir      = NULL;

  /* Find a $USERNAME.profile */
  filename = username_profile_name (pamh);
  if (filename == NULL)
    return NULL;

  /* We search for a profile in the default dconf path first */
  result = find_file_in_dir (pamh, DCONF_DEFAULT_DATA_DIR, DCONF_PROFILE_DIR, filename);
  if (result != NULL)
   goto out;

  if (pam_getenv (pamh, "XDG_DATA_DIRS") != NULL)
    dirs = strdup (pam_getenv (pamh, "XDG_DATA_DIRS"));
  else
    dirs = strdup ("/usr/local/share:/usr/share");

  if (dirs == NULL)
    {
      pam_syslog (pamh, LOG_ERR, "Could not allocate memory");
      goto out;
    }

  for (dir = strtok (dirs, ":"); dir; dir = strtok (NULL, ":"))
    {
      /* empty strings or relative paths are forbidden as per spec */
      if ((strlen (dir) < 1) || dir[0] != '/')
        continue;

      /* If we find a candidate we exit the loop */
      result = find_file_in_dir (pamh, dir, DCONF_PROFILE_DIR, filename);
      if (result)
        break;
    }

  if (result == NULL)
    pam_syslog (pamh, LOG_DEBUG, "Could not find a dconf profile candidate for this user");

  free (dirs);
out:
  free (filename);
  return result;
}

PAM_EXTERN int
pam_sm_open_session (pam_handle_t  *pamh,
                     int            flags,
                     int            argc,
                     const char   **argv)
{
  const char *runtime_dir_path;
  char       *dconf_profile_path;
  char       *symlink_path;
  bool        success = 0;

  runtime_dir_path = pam_getenv (pamh, "XDG_RUNTIME_DIR");

  if (runtime_dir_path == NULL)
    {
      pam_syslog (pamh, LOG_NOTICE, "XDG_RUNTIME_DIR has not been set yet.  Cannot set up dconf profile.");
      return PAM_IGNORE;
    }

  dconf_profile_path = get_dconf_profile_path (pamh);
  if (dconf_profile_path == NULL)
    {
       pam_syslog (pamh, LOG_NOTICE, "Could not find a dconf profile");
       return PAM_IGNORE;
    }

  symlink_path = join_strings (pamh,
                               runtime_dir_path,
                               "/",
                               DCONF_PROFILE_LINK);
  if (symlink_path == NULL)
    {
      free (dconf_profile_path);
      return PAM_IGNORE;
    }

  unlink (symlink_path);
  success = symlink (dconf_profile_path, symlink_path) == 0;
  if (!success)
    {
      int saved_errno = errno;
      pam_syslog (pamh, LOG_NOTICE, "failed to create symlink for dconf profile in XDG_RUNTIME_DIR");
      pam_syslog (pamh, LOG_NOTICE, strerror (saved_errno));
    }

  free (dconf_profile_path);
  free (symlink_path);
  return success? PAM_SUCCESS : PAM_IGNORE;
}

PAM_EXTERN int
pam_sm_close_session (pam_handle_t  *pamh,
                      int            flags,
                      int            argc,
                      const char   **argv)
{
  return PAM_SUCCESS;
}
