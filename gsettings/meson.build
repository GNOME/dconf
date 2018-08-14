# We use the libraries directly, as the dependency objects use
# link_whole; this avoids the gsettings backend module exposing
# symbols other than g_io_module_*
backend_deps = [
  libdconf_common_hidden,
  libdconf_gdbus_thread,
]

libdconf_settings = shared_library(
  'dconfsettings',
  sources: 'dconfsettingsbackend.c',
  include_directories: top_inc,
  link_with: backend_deps,
  dependencies: gio_dep,
  c_args: dconf_c_args,
  install: true,
  install_dir: gio_module_dir,
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
