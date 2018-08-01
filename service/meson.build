dbus_service_dir = get_option('with-dbus-service-dir').strip()
if dbus_service_dir == ''
  dbus_service_dir = join_paths(dconf_datadir, 'dbus-1', 'services')
endif

dbus_system_service_dir = get_option('with-dbus-system-service-dir').strip()
if dbus_system_service_dir == ''
  dbus_system_service_dir = join_paths(dconf_datadir, 'dbus-1', 'system-services')
endif

service_conf = configuration_data()
service_conf.set('libexecdir', dconf_libexecdir)

service = dconf_namespace + '.service'

configure_file(
  input: service + '.in',
  output: service,
  install: true,
  install_dir: dbus_service_dir,
  configuration: service_conf
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
  namespace: 'DConfDBus'
)

libdconf_service = static_library(
  name,
  sources: lib_sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  link_with: [
    libdconf_common,
    libdconf_shm,
    libgvdb
  ],
)

executable(
  'dconf-service',
  sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  link_with: libdconf_service,
  install: true,
  install_dir: dconf_libexecdir
)
