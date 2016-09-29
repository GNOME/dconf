/*
 * Copyright © 2010 Codethink Limited
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

#ifndef __gvdb_reader_h__
#define __gvdb_reader_h__

#include <glib.h>

typedef struct _GvdbTable GvdbTable;

typedef struct
{
  const gchar *string;
  guint        components;
  guint32     *hashes;
  guint       *lengths;
  guint32      my_hashes[16];
  guint        my_lengths[16];
} GvdbPath;

G_BEGIN_DECLS

G_GNUC_INTERNAL
void                    gvdb_path_init                                  (GvdbPath     *path,
                                                                         const gchar  *string,
                                                                         gchar         separator);

G_GNUC_INTERNAL
void                    gvdb_path_clear                                 (GvdbPath     *path);

G_GNUC_INTERNAL
GvdbTable *             gvdb_table_new_from_bytes                       (GBytes       *bytes,
                                                                         gboolean      trusted,
                                                                         GError      **error);
G_GNUC_INTERNAL
GvdbTable *             gvdb_table_new                                  (const gchar  *filename,
                                                                         gboolean      trusted,
                                                                         GError      **error);
G_GNUC_INTERNAL
void                    gvdb_table_free                                 (GvdbTable    *table);
G_GNUC_INTERNAL
gchar **                gvdb_table_get_names                            (GvdbTable    *table,
                                                                         gint         *length);
G_GNUC_INTERNAL
gchar **                gvdb_table_list                                 (GvdbTable    *table,
                                                                         const gchar  *key);
G_GNUC_INTERNAL
GvdbTable *             gvdb_table_get_table                            (GvdbTable    *table,
                                                                         const gchar  *key);
G_GNUC_INTERNAL
GVariant *              gvdb_table_get_raw_value                        (GvdbTable    *table,
                                                                         const gchar  *key);
G_GNUC_INTERNAL
GVariant *              gvdb_table_get_value                            (GvdbTable    *table,
                                                                         const gchar  *key);

G_GNUC_INTERNAL
gboolean                gvdb_table_has_value                            (GvdbTable    *table,
                                                                         const gchar  *key);
G_GNUC_INTERNAL
gboolean                gvdb_table_is_valid                             (GvdbTable    *table);

G_GNUC_INTERNAL
GVariant *              gvdb_table_get_value_for_path                   (GvdbTable    *table,
                                                                         GvdbPath     *path);

G_GNUC_INTERNAL
GVariant *              gvdb_table_get_best_value_for_path              (GvdbTable    *table,
                                                                         GvdbPath     *path);

G_END_DECLS

#endif /* __gvdb_reader_h__ */
