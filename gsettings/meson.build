deps = [
  libdconf_common_hidden_dep,
  libdconf_gdbus_thread_dep,
]

libdconf_settings = shared_library(
  'dconfsettings',
  sources: 'dconfsettingsbackend.c',
  include_directories: top_inc,
  dependencies: deps,
  c_args: '-DG_LOG_DOMAIN="dconf"',
  install: true,
  install_dir: gio_module_dir,
)

unit_test = 'abicheck'

test(
  unit_test,
  find_program(unit_test + '.sh'),
  env: 'GSETTINGS_LIB=' + libdconf_settings.full_path(),
)
