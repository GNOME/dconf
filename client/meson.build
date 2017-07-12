client_inc = include_directories('.')

install_headers(
  'dconf.h',
  subdir: meson.project_name()
)

install_headers(
  'dconf-client.h',
  subdir: join_paths(meson.project_name(), 'client')
)

sources = files('dconf-client.c')

cflags = '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())

libdconf_client = static_library(
  meson.project_name() + '-client',
  sources: sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  c_args: cflags,
  pic: true
)

libdconf = shared_library(
  meson.project_name(),
  sources: sources,
  version: libversion,
  soversion: soversion,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  c_args: cflags,
  link_with: [
    libdconf_common_shared,
    libdconf_engine_shared,
    libdconf_gdbus_thread_shared,
    libdconf_shm_shared,
    libgvdb_shared
  ],
  install: true,
  install_dir: dconf_libdir
)

libdconf_dep = declare_dependency(
  link_with: libdconf,
  include_directories: client_inc,
  dependencies: gio_unix_dep
)

pkg.generate(
  libraries: libdconf,
  version: dconf_version,
  name: meson.project_name(),
  description: meson.project_name() + ' client library',
  filebase: meson.project_name(),
  subdirs: meson.project_name(),
  requires: 'gio-unix-2.0 ' + gio_unix_req_version,
  variables: 'exec_prefix=' + dconf_libexecdir,
  install_dir: join_paths(dconf_libdir, 'pkgconfig')
)

libdconf_vapi = files(meson.project_name() + '.vapi')

vapi_data = libdconf_vapi + files(meson.project_name() + '.deps')

install_data(
  vapi_data,
  install_dir: join_paths(dconf_datadir, 'vala', 'vapi')
)
