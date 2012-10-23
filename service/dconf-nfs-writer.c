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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "dconf-writer.h"

#include "../gvdb/gvdb-reader.h"

#include <sys/fcntl.h>
#include <errno.h>

typedef DConfWriterClass DConfNfsWriterClass;

typedef struct
{
  DConfWriter parent_instance;

  gchar *lockfile;
  gchar *filename;
  gint lock_fd;
} DConfNfsWriter;

G_DEFINE_TYPE (DConfNfsWriter, dconf_nfs_writer, DCONF_TYPE_WRITER)

static void
dconf_nfs_writer_constructed (GObject *object)
{
  DConfNfsWriter *nfs = (DConfNfsWriter *) object;
  DConfWriter *writer = DCONF_WRITER (object);

  G_OBJECT_CLASS (dconf_nfs_writer_parent_class)->constructed (object);

  nfs->filename = g_build_filename (g_get_user_config_dir (), "dconf", writer->name, NULL);
}

static DConfChangeset *
dconf_nfs_writer_diff (DConfNfsWriter  *nfs,
                       GHashTable      *old,
                       GError         **error)
{
  DConfChangeset *changeset;
  GvdbTable *new;
  GBytes *bytes;

  {
    gchar *contents;
    gsize size;

    if (!g_file_get_contents (nfs->filename, &contents, &size, error))
      return NULL;

    bytes = g_bytes_new_take (contents, size);
  }

  new = gvdb_table_new (bytes, FALSE, error);

  g_bytes_unref (bytes);

  if (new == NULL)
    return NULL;
}

static gboolean
dconf_nfs_writer_begin (DConfWriter  *writer,
                        GError      **error)
{
  DConfNfsWriter *nfs = (DConfNfsWriter *) writer;
  DConfChangeset *changeset;
  struct flock lock;
  gint fd;

  nfs->lock_fd = open (nfs->lockfile, O_CREAT | O_WRONLY, 0600);

  if (nfs->lock_fd == -1)
    {
      gint saved_errno = errno;

      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Cannot open dconf lockfile %s: %s", nfs->lockfile, g_strerror (saved_errno));
      goto out;
    }

  lock.l_whence = SEEK_SET;
  lock.l_type = F_WRLCK;
  lock.l_start = 0;
  lock.l_len = 0;

  if (fcntl (nfs->lock_fd, F_SETLKW, &lock) != 0)
    {
      gint saved_errno = errno;

      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Unable to lock dconf lockfile %s: %s", nfs->lockfile, g_strerror (saved_errno));
      goto out;
    }

  if (!DCONF_WRITER_CLASS (dconf_nfs_writer_parent_class)->begin (writer, error))
    goto out;

  changeset = dconf_nfs_writer_diff (nfs, writer->uncommited_values, error);
  if (changeset == NULL)
    goto out;

  if (!dconf_changeset_is_empty (changeset))
    dconf_writer_change (writer, changeset, "(updated from nfs home directory)");

  dconf_changeset_unref (changeset);

  return TRUE;

out:
  if (writer->uncommited_values)
    DCONF_WRITER_CLASS (dconf_nfs_writer_parent_class)->end (writer);

  if (nfs->lock_fd != -1)
    {
      close (nfs->lock_fd);
      nfs->lock_fd = -1;
    }

  return FALSE;
}

static gboolean
dconf_nfs_writer_commit (DConfWriter  *writer,
                         GError      **error)
{
  return DCONF_WRITER_CLASS (dconf_nfs_writer_parent_class)->commit (writer, error);
}

static void
dconf_nfs_writer_end (DConfWriter *writer)
{
  DConfNfsWriter *nfs = (DConfNfsWriter *) writer;

  DCONF_WRITER_CLASS (dconf_nfs_writer_parent_class)->end (writer);

  if (nfs->lock_fd != -1)
    {
      close (nfs->lock_fd);
      nfs->lock_fd = -1;
    }
}

static void
dconf_nfs_writer_init (DConfNfsWriter *nfs)
{
  dconf_writer_set_native (DCONF_WRITER (nfs), FALSE);

  nfs->lock_fd = -1;
}

static void
dconf_nfs_writer_class_init (DConfNfsWriterClass *class)
{
  class->begin = dconf_nfs_writer_begin;
  class->commit = dconf_nfs_writer_commit;
  class->end = dconf_nfs_writer_end;
}

DConfWriter *
dconf_nfs_writer_new (const gchar *name)
{
  return g_object_new (dconf_nfs_writer_get_type (), NULL);
}
