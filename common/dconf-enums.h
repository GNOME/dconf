/*
 * Copyright © 2013 Canonical Limited
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

#ifndef __dconf_error_h__
#define __dconf_error_h__

#include <glib.h>

G_BEGIN_DECLS

#define DCONF_ERROR (dconf_error_quark ())
GQuark dconf_error_quark (void);

typedef enum
{
  DCONF_ERROR_FAILED,
  DCONF_ERROR_PATH,
  DCONF_ERROR_NOT_WRITABLE
} DConfError;

typedef enum
{
  DCONF_READ_FLAGS_NONE                 = 0,
  DCONF_READ_DEFAULT_VALUE              = (1u << 0),
  DCONF_READ_USER_VALUE                 = (1u << 1)
} DConfReadFlags;

G_END_DECLS

#endif /* __dconf_error_h__ */
