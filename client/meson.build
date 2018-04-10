client_inc = include_directories('.')

install_headers(
  'dconf.h',
  subdir: meson.project_name()
)

install_headers(
  'dconf-client.h',
  subdir: join_paths(meson.project_name(), 'client')
)

name = meson.project_name() + '-client'

cflags = '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())

libdconf_client = static_library(
  name,
  sources: name + '.c',
  include_directories: top_inc,
  dependencies: libdconf_gdbus_thread_dep,
  c_args: cflags,
  pic: true
)

libdconf_client_dep = declare_dependency(
  dependencies: gio_dep,
  link_with: libdconf_client
)

libdconf = shared_library(
  meson.project_name(),
  sources: name + '.c',
  version: libversion,
  soversion: soversion,
  include_directories: top_inc,
  dependencies: deps,
  c_args: cflags,
  install: true
)

libdconf_dep = declare_dependency(
  include_directories: client_inc,
  dependencies: gio_dep,
  link_with: libdconf
)

pkg.generate(
  libraries: libdconf,
  version: meson.project_version(),
  name: meson.project_name(),
  description: meson.project_name() + ' client library',
  filebase: meson.project_name(),
  subdirs: meson.project_name(),
  requires: 'gio-2.0 ' + gio_req_version,
  variables: 'exec_prefix=' + dconf_libexecdir
)

libdconf_vapi = files(meson.project_name() + '.vapi')

vapi_data = libdconf_vapi + files(meson.project_name() + '.deps')

install_data(
  vapi_data,
  install_dir: join_paths(dconf_datadir, 'vala', 'vapi')
)
