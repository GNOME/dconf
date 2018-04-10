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
    libdconf_engine,
    libdconf_gdbus_thread,
    libdconf_shm,
    libgvdb
  ],
  install: true,
  install_dir: gio_module_dir
)

unit_test = 'abicheck'

test(
  unit_test,
  find_program(unit_test + '.sh'),
  env: 'GSETTINGS_LIB=' + libdconf_settings.full_path()
)
