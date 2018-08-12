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

cflags = '-DG_LOG_DOMAIN="dconf"'

libdconf_client = static_library(
  'dconf-client',
  sources: sources,
  include_directories: top_inc,
  dependencies: libdconf_gdbus_thread_dep,
  c_args: cflags,
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

libdconf = shared_library(
  'dconf',
  sources: sources,
  version: libversion,
  soversion: soversion,
  include_directories: top_inc,
  dependencies: client_deps,
  c_args: cflags,
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

libdconf_vapi = files('dconf.vapi')

vapi_data = libdconf_vapi + files('dconf.deps')

install_data(
  vapi_data,
  install_dir: vapi_dir,
)
