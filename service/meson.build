service_conf = configuration_data()
service_conf.set('libexecdir', dconf_libexecdir)

service = dconf_namespace + '.service'

configure_file(
  input: service + '.in',
  output: service,
  configuration: service_conf,
  install: true,
  install_dir: dbus_session_service_dir,
)

sources = [
  'dconf-blame.c',
  'dconf-gvdb-utils.c',
  'dconf-keyfile-writer.c',
  'dconf-service.c',
  'dconf-shm-writer.c',
  'dconf-writer.c',
  'main.c',
]

sources += gnome.gdbus_codegen(
  'dconf-generated',
  dconf_namespace + '.xml',
  interface_prefix: dconf_namespace + '.',
  namespace: 'DConfDBus',
)

deps = [
  gio_unix_dep,
  libdconf_common_dep,
  libdconf_shm_dep,
  libgvdb_dep,
]

executable(
  'dconf-service',
  sources,
  include_directories: top_inc,
  dependencies: deps,
  install: true,
  install_dir: dconf_libexecdir,
)
