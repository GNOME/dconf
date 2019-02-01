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

deps = [
  libdconf_common_hidden_dep,
  libdconf_gdbus_thread_dep,
]

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
  link_whole: libdconf_client,
)

libdconf = shared_library(
  'dconf',
  sources: sources,
  version: libversion,
  soversion: soversion,
  include_directories: top_inc,
  dependencies: deps,
  c_args: dconf_c_args,
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

if vapigen_dep.found()
  vapi_data = files(
    'dconf.vapi',
    'dconf.deps',
  )

  vapi_dir = vapigen_dep.get_pkgconfig_variable(
    'vapidir',
    define_variable: ['datadir', dconf_datadir]
  )

  install_data(
    vapi_data,
    install_dir: vapi_dir,
  )
endif



