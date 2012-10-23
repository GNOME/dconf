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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "dconf-engine-source-private.h"

#include "../shm/dconf-shm.h"
#include "dconf-engine.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>

static void
dconf_engine_source_user_nfs_init (DConfEngineSource *source)
{
  GError *error = NULL;
  GVariant *reply;

  source->bus_type = G_BUS_TYPE_SESSION;
  source->bus_name = g_strdup ("ca.desrt.dconf");
  source->object_path = g_strdup_printf ("/ca/desrt/dconf/Writer/%s", source->name);
  source->writable = TRUE;

  /* We need to get the dconf-service to come online and notice that
   * we're on an NFS home directory.  In that case it will copy the
   * given database into the XDG_RUNTIME_DIR which is where we will
   * access it.
   *
   * This prevents us from doing mmap() on a file on NFS (which often
   * results in us seeing SIGBUS).
   */
  reply = dconf_engine_dbus_call_sync_func (G_BUS_TYPE_SESSION, source->bus_name, source->object_path,
                                            "ca.desrt.dconf.Writer", "Init", NULL, G_VARIANT_TYPE_UNIT, &error);

  if (reply)
    g_variant_unref (reply);
  else
    {
      g_warning ("Trying to start the dconf service failed: %s.  Expect problems.", error->message);
      g_error_free (error);
    }
}

static gboolean
dconf_engine_source_user_nfs_needs_reopen (DConfEngineSource *source)
{
  return !source->values || !gvdb_table_is_valid (source->values);
}

static GvdbTable *
dconf_engine_source_user_nfs_reopen (DConfEngineSource *source)
{
  GvdbTable *table;
  gchar *filename;

  filename = g_build_filename (dconf_shm_get_shmdir (), source->name, NULL);
  table = gvdb_table_new (filename, FALSE, NULL);
  g_free (filename);

  return table;
}

static void
dconf_engine_source_user_nfs_finalize (DConfEngineSource *source)
{
}

G_GNUC_INTERNAL
const DConfEngineSourceVTable dconf_engine_source_user_nfs_vtable = {
  .instance_size    = sizeof (DConfEngineSource),
  .init             = dconf_engine_source_user_nfs_init,
  .finalize         = dconf_engine_source_user_nfs_finalize,
  .needs_reopen     = dconf_engine_source_user_nfs_needs_reopen,
  .reopen           = dconf_engine_source_user_nfs_reopen
};
