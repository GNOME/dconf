sources = files('dconfsettingsbackend.c')

cflags = '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())

libdconf_settings = shared_library(
  meson.project_name() + 'settings',
  sources: sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  c_args: cflags,
  link_with: [
    libdconf_common_hidden,
    libdconf_engine_shared,
    libdconf_gdbus_thread_shared,
    libdconf_shm_shared,
    libgvdb_shared
  ],
  install: true,
  install_dir: join_paths(dconf_libdir, 'gio', 'modules')
)

unit_test = 'abicheck'

test(
  unit_test,
  find_program(unit_test + '.sh'),
  env: 'GSETTINGS_LIB=' + libdconf_settings.full_path()
)
