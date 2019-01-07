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

#ifndef __dconf_shm_mockable_h__
#define __dconf_shm_mockable_h__

#include <glib.h>

G_GNUC_INTERNAL
ssize_t                 dconf_shm_pwrite   (int          fd,
                                            const void  *buf,
                                            size_t       count,
                                            off_t        offset);

#endif /* __dconf_shm_mockable_h__ */
