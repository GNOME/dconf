backend_deps = [
  libdconf_common_dep,
  libdconf_gdbus_thread_dep,
]

symbol_map = join_paths(meson.current_source_dir(), 'symbol.map')
ldflags = cc.get_supported_link_arguments('-Wl,--version-script,@0@'.format(symbol_map))

libdconf_settings = shared_library(
  'dconfsettings',
  sources: 'dconfsettingsbackend.c',
  include_directories: top_inc,
  dependencies: backend_deps,
  c_args: dconf_c_args,
  link_args: ldflags,
  link_depends: symbol_map,
  install: true,
  install_dir: gio_module_dir,
)
