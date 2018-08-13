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

lib_sources = [
  'dconf-blame.c',
  'dconf-gvdb-utils.c',
  'dconf-keyfile-writer.c',
  'dconf-service.c',
  'dconf-shm-writer.c',
  'dconf-writer.c',
]
sources = [
  'main.c',
]

lib_sources += gnome.gdbus_codegen(
  'dconf-generated',
  dconf_namespace + '.xml',
  interface_prefix: dconf_namespace + '.',
  namespace: 'DConfDBus',
)

libdconf_service = static_library(
  'dconf-service',
  sources: lib_sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  link_with: [
    libdconf_common,
    libdconf_shm,
    libgvdb,
  ],
)

executable(
  'dconf-service',
  sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  link_with: libdconf_service,
  install: true,
  install_dir: dconf_libexecdir,
)
