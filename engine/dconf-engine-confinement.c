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

#include "dconf-engine-confinement.h"

static const gchar *dconf_engine_confinement_app_id;

gboolean
dconf_engine_confinement_detect (void)
{
  return dconf_engine_confinement_get_app_id ()[0] != '\0';
}

const gchar *
dconf_engine_confinement_get_app_id (void)
{
  if (g_once_init_enter (&dconf_engine_confinement_app_id))
    {
      const gchar *tmp;

      tmp = g_getenv ("CONFINED_APPID");
      if (!tmp)
        tmp = "";

      g_once_init_leave (&dconf_engine_confinement_app_id, g_strdup (tmp));
    }

  return dconf_engine_confinement_app_id;
}
