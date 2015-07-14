/*
 * Copyright © 2015 Red Hat Limited
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
 * Author: Alberto Ruiz <aruiz@redhat.com>
 */

#ifndef  __pam_dconf_h__
#define __pam_dconf_h__

#define PAM_SM_SESSION

#include <security/pam_modules.h>
#include <security/pam_ext.h>

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#define DCONF_PROFILE_DIR      "/dconf/profile/"
#define DCONF_PROFILE_SUFFIX   ".profile"
#define DCONF_PROFILE_LINK     "dconf.profile"
#define DCONF_DEFAULT_DATA_DIR "/etc"


#endif
