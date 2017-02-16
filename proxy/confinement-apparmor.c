/*
 * Copyright © 2017 Canonical Limited
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
 * Author: William Hua <william.hua@canonical.com>
 */

#define _GNU_SOURCE

#include "confinement.h"

#ifdef HAVE_APPARMOR
#include <sys/apparmor.h>
#endif /* HAVE_APPARMOR */

gboolean
confinement_check_apparmor (GVariant    *credentials,
                            gboolean    *out_is_confined,
                            Permissions *out_permissions)
{
#ifdef HAVE_APPARMOR
  g_autofree gchar *context = NULL;
  const gchar *label;
  aa_dconf_info info;
  gchar **readable;
  gchar **writable;
  gint i;

  if (!g_variant_lookup (credentials, "LinuxSecurityLabel", "^ay", &context))
    {
      g_warning ("Caller credentials are missing LinuxSecurityLabel field");
      return FALSE;
    }

  label = aa_splitcon (context, NULL);

  if (!g_strcmp0 (label, "unconfined"))
    {
      *out_is_confined = FALSE;
      return TRUE;
    }

  if (aa_query_dconf_info (label, &info))
    {
      g_warning ("Kernel has no dconf data for %s", label);
      return FALSE;
    }

  /* Merge the readable path lists */
  readable = g_new (gchar *, info.r_n + info.ar_n + 1);
  for (i = 0; i < info.r_n; i++)
    readable[i] = g_strdup (info.r_paths[i]);
  for (i = 0; i < info.ar_n; i++)
    readable[info.r_n + i] = g_strdup (info.ar_paths[i]);
  readable[info.r_n + info.ar_n] = NULL;

  /* Merge the writable path lists */
  writable = g_new (gchar *, info.rw_n + info.arw_n + 1);
  for (i = 0; i < info.rw_n; i++)
    writable[i] = g_strdup (info.rw_paths[i]);
  for (i = 0; i < info.arw_n; i++)
    writable[info.rw_n + i] = g_strdup (info.arw_paths[i]);
  writable[info.rw_n + info.arw_n] = NULL;

  aa_clear_dconf_info (&info);

  permission_list_init (&out_permissions->readable, readable);
  permission_list_init (&out_permissions->writable, writable);
  out_permissions->ipc_dir = g_build_filename (g_get_user_runtime_dir (), label, NULL);
  out_permissions->app_id = g_strdup (label);

  *out_is_confined = TRUE;
#else
  *out_is_confined = FALSE;
#endif /* HAVE_APPARMOR */

  return TRUE;
}
