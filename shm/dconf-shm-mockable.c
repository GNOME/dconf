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

/**
 * This module contains the production implementations of methods used in
 * dconf_shm that need to be mocked out for tests.
 *
 * In some cases, external methods are wrapped with a different name. This is
 * done so that it is not necessary to redefine the external functions in
 * unit tests in order to mock them out, and therefore easy to also call the
 * non mocked versions in tests if necessary.
 */

#include "config.h"

#include "dconf-shm-mockable.h"

#include <unistd.h>

ssize_t
dconf_shm_pwrite (int fd, const void *buf, size_t count, off_t offset)
{
  return pwrite (fd, buf, count, offset);
}
