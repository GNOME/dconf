deps = [
  libdconf_common_hidden_dep,
  libdconf_gdbus_thread_dep
]

libdconf_settings = shared_library(
  'dconfsettings',
  sources: 'dconfsettingsbackend.c',
  include_directories: top_inc,
  dependencies: deps,
  c_args: '-DG_LOG_DOMAIN="dconf"',
  install: true,
  install_dir: gio_module_dir
)

envs = test_env + [
  'G_TEST_SRCDIR=' + meson.current_source_dir(),
  'G_TEST_BUILDDIR=' + meson.current_build_dir(),
  'GSETTINGS_LIB=' + libdconf_settings.full_path(),
]

unit_test = 'abicheck'

test(
  unit_test,
  find_program(unit_test + '.sh'),
  env: envs,
)
