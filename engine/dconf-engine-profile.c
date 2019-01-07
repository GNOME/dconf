/*
 * Copyright © 2010 Codethink Limited
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "config.h"

#include "dconf-engine-profile.h"
#include "dconf-engine-mockable.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "dconf-engine-source.h"

#define MANDATORY_DIR           "/run/dconf/user/" /* + getuid () */
#define RUNTIME_PROFILE         /* XDG_RUNTIME_DIR + */ "/dconf/profile"

/* This comment attempts to document the exact semantics of
 * profile-loading.
 *
 * In no situation should the result of profile loading be an abort.
 * There must be a defined outcome for all possible situations.
 * Warnings may be issued to stderr, however.
 *
 * The first step is to determine what profile is to be used.  If a
 * profile is explicitly specified by the API then it has the top
 * priority.  Otherwise, if the DCONF_PROFILE environment variable is
 * set, it takes next priority.
 *
 * In both of those cases, if the named profile starts with a slash
 * character then it is taken to be an absolute pathname.  If it does
 * not start with a slash then it is assumed to specify a profile file
 * relative to /etc/dconf/profile/ or
 * <ulink url='http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html'>XDG_DATA_DIRS</ulink>/dconf/profile/,
 * taking the file in /etc in preference (ie: DCONF_PROFILE=test looks
 * first for profile file /etc/dconf/profile/test, falling back to
 * /usr/local/share/dconf/profile/test, then to
 * /usr/share/dconf/profile/test).
 *
 * If opening the profile file fails then the null profile is used.
 * This is a profile that contains zero sources.  All keys will be
 * unwritable and all reads will return NULL.
 *
 * In the case that no explicit profile was given and DCONF_PROFILE is
 * unset, dconf attempts to open and use a profile called "user" (ie:
 * /etc/dconf/profile/user or XDG_DATA_DIRS/dconf/profile/user).  If
 * that fails then the fallback is to act as if the profile file existed
 * and contained a single line: "user-db:user".
 *
 * Note that the fallback case for a missing profile file is different
 * in the case where a profile was explicitly specified (either by the
 * API or the environment) and the case where one was not.
 *
 * Once a profile file is opened, each line is treated as a possible
 * source.  Comments and empty lines are ignored.
 *
 * All valid source specification lines need to start with 'user-db:',
 * 'system-db:', 'service-db:' or 'file-db:'.  If a line doesn't start
 * with one of these then it gets ignored.  If all the lines in the file
 * get ignored then the result is effectively the null profile.
 *
 * If the first source is a "user-db:" or "service-db:" then the
 * resulting profile will be writable.  No profile starting with a
 * "system-db:" or "file-db:" source can ever be writable.
 *
 * Note: even if the source fails to initialise (due to a missing file,
 * for example) it will remain in the source list.  This could have a
 * performance cost: in the case of a system-db, for example, dconf will
 * check if the file has come into existence on every read.
 */

static DConfEngineSource **
dconf_engine_null_profile (gint *n_sources)
{
  *n_sources = 0;

  return NULL;
}

static DConfEngineSource **
dconf_engine_default_profile (gint *n_sources)
{
  DConfEngineSource **sources;

  sources = g_new (DConfEngineSource *, 1);
  sources[0] = dconf_engine_source_new_default ();
  *n_sources = 1;

  return sources;
}

static DConfEngineSource *
dconf_engine_profile_handle_line (gchar *line)
{
  DConfEngineSource *source;
  gchar *end;

  /* remove whitespace at the front */
  while (g_ascii_isspace (*line))
    line++;

  /* find the end of the line (or start of comments) */
  end = line + strcspn (line, "#\n");

  /* remove whitespace at the end */
  while (end > line && g_ascii_isspace (end[-1]))
    end--;

  /* if we're left with nothing, return NULL */
  if (line == end)
    return NULL;

  *end = '\0';

  source = dconf_engine_source_new (line);

  if (source == NULL)
    g_warning ("unknown dconf database description: %s", line);

  return source;
}

static DConfEngineSource **
dconf_engine_read_profile_file (FILE *file,
                                gint *n_sources)
{
  DConfEngineSource **sources;
  gchar line[80];
  gint n = 0, a;

  sources = g_new (DConfEngineSource *, (a = 4));

  while (fgets (line, sizeof line, file))
    {
      DConfEngineSource *source;

      /* The input file has long lines. */
      if G_UNLIKELY (!strchr (line, '\n'))
        {
          GString *long_line;

          long_line = g_string_new (line);
          while (fgets (line, sizeof line, file))
            {
              g_string_append (long_line, line);
              if (strchr (line, '\n'))
                break;
            }

          source = dconf_engine_profile_handle_line (long_line->str);
          g_string_free (long_line, TRUE);
        }

      else
        source = dconf_engine_profile_handle_line (line);

      if (source != NULL)
        {
          if (n == a)
            sources = g_renew (DConfEngineSource *, sources, a *= 2);

          sources[n++] = source;
        }
    }

  *n_sources = n;

  return g_realloc_n (sources, n, sizeof (DConfEngineSource *));
}

/* Find a profile file with the name given in 'profile' and open it. */
static FILE *
dconf_engine_open_profile_file (const gchar *profile)
{
  const gchar * const *xdg_data_dirs;
  const gchar *prefix = SYSCONFDIR;
  FILE *fp;

  xdg_data_dirs = g_get_system_data_dirs ();

  /* First time through, we check SYSCONFDIR, then we check XDG_DATA_DIRS,
   * in order.  We stop looking as soon as we successfully open a file
   * or in the case that we run out of XDG_DATA_DIRS.
   *
   * If we hit an error other than ENOENT then we warn about that and
   * exit immediately.  We should only attempt fallback in the case that
   * the file in the higher-precedence directory is non-existent.
   */
  do
    {
      gchar *filename;

      filename = g_build_filename (prefix, "dconf/profile", profile, NULL);
      fp = dconf_engine_fopen (filename, "r");

      /* If it wasn't ENOENT then we don't want to continue on to check
       * other paths.  Fail immediately.
       */
      if (fp == NULL && errno != ENOENT)
        {
          g_warning ("Unable to open %s: %s", filename, g_strerror (errno));
          g_free (filename);
          return NULL;
        }

      g_free (filename);
    }
  while (fp == NULL && (prefix = *xdg_data_dirs++));

  /* If we didn't find it, this could be NULL.  That's OK. */
  return fp;
}

static FILE *
dconf_engine_open_mandatory_profile (void)
{
  gchar path[20 + sizeof MANDATORY_DIR];
  gint mdlen = strlen (MANDATORY_DIR);

  memcpy (path, MANDATORY_DIR, mdlen);
  snprintf (path + mdlen, 20, "%u", (guint) getuid ());

  return dconf_engine_fopen (path, "r");
}

static FILE *
dconf_engine_open_runtime_profile (void)
{
  const gchar *runtime_dir;
  gchar *path;
  gint rdlen;

  runtime_dir = g_get_user_runtime_dir ();
  rdlen = strlen (runtime_dir);

  path = g_alloca (rdlen + sizeof RUNTIME_PROFILE);
  memcpy (path, runtime_dir, rdlen);
  memcpy (path + rdlen, RUNTIME_PROFILE, sizeof RUNTIME_PROFILE);

  return dconf_engine_fopen (path, "r");
}

DConfEngineSource **
dconf_engine_profile_open (const gchar *profile,
                           gint        *n_sources)
{
  DConfEngineSource **sources;
  FILE *file = NULL;

  /* We must consider a few different possibilities for the dconf
   * profile file.  We proceed until we have either
   *
   *   a) a profile name; or
   *
   *   b) a profile file is open
   *
   * If we get a profile name, even if the file is missing, we will use
   * that name rather than falling back to another possibility.  In this
   * case, we will issue a warning.
   *
   * Therefore, at each step, we ensure that there is no profile name or
   * file yet open before checking the next possibility.
   *
   * Note that @profile is an argument to this function, so we will end
   * up trying none of the five possibilities if that is given.
   */

  /* 1. Mandatory profile */
  if (profile == NULL)
    file = dconf_engine_open_mandatory_profile ();

  /* 2. Environment variable */
  if (profile == NULL && file == NULL)
    profile = g_getenv ("DCONF_PROFILE");

  /* 3. Runtime profile */
  if (profile == NULL && file == NULL)
    file = dconf_engine_open_runtime_profile ();

  /* 4. User profile */
  if (profile == NULL && file == NULL)
    file = dconf_engine_open_profile_file ("user");

  /* 5. Default profile */
  if (profile == NULL && file == NULL)
    return dconf_engine_default_profile (n_sources);

  /* At this point either we have a profile name or file open, but never
   * both.  If it's a profile name, we try to open it.
   */
  if (profile != NULL)
    {
      g_assert (file == NULL);

      if (profile[0] != '/')
        file = dconf_engine_open_profile_file (profile);
      else
        file = dconf_engine_fopen (profile, "r");
    }

  if (file != NULL)
    {
      sources = dconf_engine_read_profile_file (file, n_sources);
      fclose (file);
    }
  else
    {
      g_warning ("unable to open named profile (%s): using the null configuration.", profile);
      sources = dconf_engine_null_profile (n_sources);
    }

  return sources;
}
