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

#include "config.h"

#include "dconf-engine-confinement.h"
#include "dconf-engine-source-private.h"

#include "dconf-engine.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>

static const gchar *
dconf_engine_source_proxied_get_dir (void)
{
  static const gchar *dir;

  if (g_once_init_enter (&dir))
    {
      const gchar *tmp;

      tmp = g_strdup_printf ("%s/%s", g_get_user_runtime_dir (), dconf_engine_confinement_get_app_id ());

#if 0 /* will be how it works when actually confined */
      tmp = g_get_user_runtime_dir ();
#endif

      g_once_init_leave (&dir, tmp);
    }

  g_print ("pdir %s\n", dir);

  return dir;
}

static void
dconf_engine_source_proxied_init (DConfEngineSource *source)
{
  source->bus_type = source->writable ? G_BUS_TYPE_SESSION : G_BUS_TYPE_NONE;
  source->bus_name = g_strdup ("ca.desrt.dconf.Proxy");
  source->object_path = g_strdup_printf ("/ca/desrt/dconf/Proxy/%s", dconf_engine_confinement_get_app_id ());
}

static gboolean
dconf_engine_source_proxied_needs_reopen (DConfEngineSource *source)
{
  return !source->values || !gvdb_table_is_valid (source->values);
}

static GvdbTable *
dconf_engine_source_proxied_reopen (DConfEngineSource *source)
{
  GError *error = NULL;
  GvdbTable *table;
  gchar *filename;

  filename = g_build_filename (dconf_engine_source_proxied_get_dir (), source->name, NULL);
  table = gvdb_table_new (filename, FALSE, &error);

  if (table == NULL && source->writable)
    {
      g_clear_error (&error);

      /* If the file does not exist, kick the service to have it created. */
      dconf_engine_dbus_call_sync_func (source->bus_type, source->bus_name, source->object_path,
                                        "ca.desrt.dconf.Proxy", "Init", g_variant_new ("()"), NULL, NULL);

      /* try again */
      table = gvdb_table_new (filename, FALSE, &error);
    }

  if (table == NULL)
    g_error ("Unable to open proxied dconf database %s", filename);

  g_free (filename);

  return table;
}

static void
dconf_engine_source_proxied_finalize (DConfEngineSource *source)
{
}

G_GNUC_INTERNAL
const DConfEngineSourceVTable dconf_engine_source_proxied_vtable = {
  .instance_size    = sizeof (DConfEngineSource),
  .init             = dconf_engine_source_proxied_init,
  .finalize         = dconf_engine_source_proxied_finalize,
  .needs_reopen     = dconf_engine_source_proxied_needs_reopen,
  .reopen           = dconf_engine_source_proxied_reopen
};
