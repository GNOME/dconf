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

#ifndef __dconf_engine_confinement_h__
#define __dconf_engine_confinement_h__

#include <gio/gio.h>

G_GNUC_INTERNAL
gboolean                dconf_engine_confinement_detect                 (void);

G_GNUC_INTERNAL
const gchar *           dconf_engine_confinement_get_app_id             (void);

#endif /* __dconf_engine_confinement_h__ */
