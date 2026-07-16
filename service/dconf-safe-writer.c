/*
 * Copyright © 2025 Red Hat
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
 * Authors: Marek Kasik <mkasik@redhat.com>
 *
 * This file is based on DconfWriter and DconfKeyfileWriter.
 * This backend should be safe to run on homes shared over NFS
 * while maintaining compatibility of stored data as it uses
 * GVDB format.
 *
 */

#include "config.h"

#include "dconf-writer.h"
#include "dconf-writer-common.h"

#include "../common/dconf-gvdb-utils.h"

#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

typedef DConfWriterClass DConfSafeWriterClass;

typedef struct
{
  DConfWriter   parent_instance;
  gchar        *filename;
  gchar        *lock_filename;
  gint          lock_fd;
  GFileMonitor *monitor;
  guint         scheduled_update;

  DConfChangeset *uncommitted_values;
  DConfChangeset *committed_values;
} DConfSafeWriter;

G_DEFINE_TYPE (DConfSafeWriter, dconf_safe_writer, DCONF_TYPE_WRITER)

static gboolean dconf_safe_update (gpointer user_data);

static void
dconf_safe_changed (GFileMonitor      *monitor,
                    GFile             *file,
                    GFile             *other_file,
                    GFileMonitorEvent  event_type,
                    gpointer           user_data)
{
  DConfSafeWriter *sw = user_data;

  if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
      event_type == G_FILE_MONITOR_EVENT_CREATED)
    {
      if (!sw->scheduled_update)
        sw->scheduled_update = g_idle_add (dconf_safe_update, sw);
    }
}

/* Acquire lock and check GVDB for changes against committed values. */
static gboolean
dconf_safe_writer_begin (DConfWriter  *writer,
                         GError      **error)
{
  DConfSafeWriter *sw = (DConfSafeWriter *) writer;
  DConfChangeset *changes;
  gboolean missing;

  if (sw->filename == NULL)
    {
      GFile *file;
      gchar *lock_base_filename;

      sw->filename = g_build_filename (g_get_user_config_dir (), "dconf", dconf_writer_get_name (writer), NULL);
      lock_base_filename = g_strconcat (".", dconf_writer_get_name (writer), ".lock", NULL);
      sw->lock_filename = g_build_filename (g_get_user_config_dir (), "dconf", lock_base_filename, NULL);
      g_free (lock_base_filename);

      /* See https://bugzilla.gnome.org/show_bug.cgi?id=691618 */
      file = g_vfs_get_file_for_path (g_vfs_get_local (), sw->filename);
      sw->monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, error);
      g_object_unref (file);
      if (sw->monitor == NULL)
        {
          g_clear_pointer (&sw->filename, g_free);
          g_clear_pointer (&sw->lock_filename, g_free);

          return FALSE;
        }

      /* There has to be an initial change to begin watching for the "changed" signal! */
      g_signal_connect (sw->monitor, "changed", G_CALLBACK (dconf_safe_changed), sw);
    }

  sw->lock_fd = dconf_lock_file_lock (sw->lock_filename, error);
  if (sw->lock_fd == -1)
    return FALSE;

  g_clear_pointer (&sw->committed_values, dconf_changeset_unref);
  sw->committed_values = dconf_gvdb_utils_read_and_back_up_file (sw->filename, &missing, error);
  if (sw->committed_values == NULL)
    return FALSE;

  if (!DCONF_WRITER_CLASS (dconf_safe_writer_parent_class)->begin (writer, error))
    return FALSE;

  changes = dconf_writer_diff (writer, sw->committed_values);
  if (changes != NULL)
    {
      DCONF_WRITER_CLASS (dconf_safe_writer_parent_class)->change (writer, changes, "");
      dconf_changeset_unref (changes);
    }

  sw->uncommitted_values = dconf_changeset_new_database (sw->committed_values);

  return TRUE;
}

/* Add the change to uncommitted changes. */
static void
dconf_safe_writer_change (DConfWriter    *writer,
                          DConfChangeset *changeset,
                          const gchar    *tag)
{
  DConfSafeWriter *sw = (DConfSafeWriter *) writer;
  DConfChangeset *effective_changeset = dconf_changeset_filter_changes (sw->uncommitted_values, changeset);

  DCONF_WRITER_CLASS (dconf_safe_writer_parent_class)->change (writer, changeset, tag);

  if (effective_changeset != NULL)
    {
      dconf_changeset_unref (effective_changeset);
      dconf_changeset_change (sw->uncommitted_values, changeset);
    }
}

/* Store the uncommitted changes to the GVDB files. */
static gboolean
dconf_safe_writer_commit (DConfWriter  *writer,
                          GError      **error)
{
  DConfSafeWriter *sw = (DConfSafeWriter *) writer;
  g_autoptr(DConfChangeset) effective_changeset = NULL;

  effective_changeset = dconf_changeset_diff (sw->committed_values, sw->uncommitted_values);

  /* Save the changes to the file in user config directory. */
  if (effective_changeset != NULL)
    {
      if (!dconf_gvdb_utils_write_file (sw->filename, sw->uncommitted_values, error))
        return FALSE;
    }

  if (!DCONF_WRITER_CLASS (dconf_safe_writer_parent_class)->commit (writer, error))
    return FALSE;

  g_clear_pointer (&sw->committed_values, dconf_changeset_unref);
  sw->committed_values = sw->uncommitted_values;
  sw->uncommitted_values = NULL;

  return TRUE;
}

static void
dconf_safe_writer_end (DConfWriter *writer)
{
  DConfSafeWriter *sw = (DConfSafeWriter *) writer;

  DCONF_WRITER_CLASS (dconf_safe_writer_parent_class)->end (writer);

  g_clear_pointer (&sw->uncommitted_values, dconf_changeset_unref);
  close (sw->lock_fd);
  sw->lock_fd = -1;
}

static gboolean
dconf_safe_update (gpointer user_data)
{
  DConfSafeWriter *sw = user_data;

  if (dconf_safe_writer_begin (DCONF_WRITER (sw), NULL))
    dconf_safe_writer_commit (DCONF_WRITER (sw), NULL);
  dconf_safe_writer_end (DCONF_WRITER (sw));

  sw->scheduled_update = 0;

  return G_SOURCE_REMOVE;
}

static void
dconf_safe_writer_finalize (GObject *object)
{
  DConfSafeWriter *sw = (DConfSafeWriter *) object;

  if (sw->scheduled_update)
    g_source_remove (sw->scheduled_update);

  g_clear_object (&sw->monitor);
  g_free (sw->lock_filename);
  if (sw->lock_fd >= 0)
    close (sw->lock_fd);
  g_free (sw->filename);
  g_clear_pointer (&sw->committed_values, dconf_changeset_unref);

  G_OBJECT_CLASS (dconf_safe_writer_parent_class)->finalize (object);
}

static void
dconf_safe_writer_init (DConfSafeWriter *sw)
{
  dconf_writer_set_basepath (DCONF_WRITER (sw), "safe");

  sw->lock_fd = -1;
}

static void
dconf_safe_writer_class_init (DConfWriterClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = dconf_safe_writer_finalize;

  class->begin = dconf_safe_writer_begin;
  class->change = dconf_safe_writer_change;
  class->commit = dconf_safe_writer_commit;
  class->end = dconf_safe_writer_end;
}
