client_inc = include_directories('.')

install_headers(
  'dconf.h',
  subdir: 'dconf',
)

install_headers(
  'dconf-client.h',
  subdir: join_paths('dconf', 'client'),
)

sources = files('dconf-client.c')

libdconf_client = static_library(
  'dconf-client',
  sources: sources,
  include_directories: top_inc,
  dependencies: libdconf_gdbus_thread_dep,
  c_args: dconf_c_args,
  pic: true,
)

libdconf_client_dep = declare_dependency(
  dependencies: gio_dep,
  link_with: libdconf_client,
)

client_deps = [
  libdconf_common_dep,
  libdconf_engine_dep,
  libdconf_gdbus_thread_dep,
]

symbol_map = join_paths(meson.current_source_dir(), 'symbol.map')
ldflags = cc.get_supported_link_arguments('-Wl,--version-script,@0@'.format(symbol_map))

libdconf = shared_library(
  'dconf',
  sources: sources,
  version: libversion,
  soversion: soversion,
  include_directories: top_inc,
  dependencies: client_deps,
  c_args: dconf_c_args,
  link_args: ldflags,
  link_depends: symbol_map,
  install: true,
)

libdconf_dep = declare_dependency(
  include_directories: client_inc,
  dependencies: gio_dep,
  link_with: libdconf,
)

pkg.generate(
  libdconf,
  description: 'dconf client library',
  subdirs: 'dconf',
  requires: 'gio-2.0 ' + gio_req_version,
  variables: 'exec_prefix=${prefix}',
)

if get_option('vapi')
  vapigen_dep = dependency('vapigen')

  vapi_data = files(
    'dconf.vapi',
    'dconf.deps',
  )

  vapi_dir = vapigen_dep.get_variable(
    pkgconfig: 'vapidir',
    pkgconfig_define: ['datadir', dconf_datadir]
  )

  install_data(
    vapi_data,
    install_dir: vapi_dir,
  )
endif



