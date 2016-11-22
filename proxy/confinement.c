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

#include "confinement.h"

gboolean
confinement_check (GVariant    *credentials,
                   gboolean    *out_is_confined,
                   Permissions *out_confined_permissions)
{
  Permissions permissions;
  gboolean is_confined;

  if (!confinement_check_flatpak (credentials, &is_confined, &permissions))
    return FALSE;

  if (!is_confined)
    /* snap goes here */;

  *out_is_confined = is_confined;

  if (is_confined)
    *out_confined_permissions = permissions;

  return TRUE;
}
