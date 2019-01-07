/*
 * Copyright © 2019 Daniel Playfair Cal
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
 * Author: Daniel Playfair Cal <daniel.playfair.cal@gmail.com>
 */

#ifndef __dconf_engine_mockable_h__
#define __dconf_engine_mockable_h__

#include <gio/gio.h>
#include <stdio.h>

G_GNUC_INTERNAL
FILE *dconf_engine_fopen    (const char *pathname,
                             const char *mode);

#endif
