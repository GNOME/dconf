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

#ifndef __dconf_gvdb_utils_h__
#define __dconf_gvdb_utils_h__

#include "gvdb/gvdb-reader.h"
#include "./dconf-changeset.h"

DConfChangeset *                dconf_gvdb_utils_changeset_from_table   (GvdbTable       *table);
DConfChangeset *                dconf_gvdb_utils_read_and_back_up_file  (const gchar     *filename,
                                                                         gboolean        *file_missing,
                                                                         GError         **error);
GHashTable *                    dconf_gvdb_utils_table_from_changeset   (DConfChangeset  *database);
gboolean                        dconf_gvdb_utils_write_file             (const gchar     *filename,
                                                                         DConfChangeset  *database,
                                                                         GError         **error);

#endif /* __dconf_gvdb_utils_h__ */
