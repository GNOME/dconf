project(
  'dconf', ['c'],
  version: '0.39.1',
  license: 'LGPL2.1+',
  meson_version: '>= 0.47.0',
)

dconf_prefix = get_option('prefix')
dconf_datadir = join_paths(dconf_prefix, get_option('datadir'))
dconf_libdir = join_paths(dconf_prefix, get_option('libdir'))
dconf_libexecdir = join_paths(dconf_prefix, get_option('libexecdir'))
dconf_mandir = join_paths(dconf_prefix, get_option('mandir'))
dconf_sysconfdir = join_paths(dconf_prefix, get_option('sysconfdir'))

dconf_namespace = 'ca.desrt.dconf'

soversion = 1
current = 0
revision = 0
libversion = '@0@.@1@.@2@'.format(soversion, current, revision)

cc = meson.get_compiler('c')

# compiler flags
common_flags = ['-DSYSCONFDIR="@0@"'.format(dconf_sysconfdir)]

if get_option('buildtype').contains('debug')
  common_flags += cc.get_supported_arguments([
    '-fno-common',
    '-Wmissing-prototypes',
    '-Wwrite-strings',
  ])
endif

add_project_arguments(common_flags, language: 'c')

dconf_c_args = [
  '-DG_LOG_DOMAIN="dconf"',
  '-DG_LOG_USE_STRUCTURED=1',
]

gio_req_version = '>= 2.25.7'

gio_dep = dependency('gio-2.0', version: gio_req_version)
gio_unix_dep = dependency('gio-unix-2.0', version: gio_req_version)
glib_dep = dependency('glib-2.0', version: '>= 2.44.0')

gio_module_dir = gio_dep.get_pkgconfig_variable('giomoduledir', define_variable: ['libdir', dconf_libdir])
dbus_session_service_dir = dependency('dbus-1').get_pkgconfig_variable('session_bus_services_dir', define_variable: ['datadir', dconf_datadir])

systemd_userunitdir = get_option('systemduserunitdir')

if systemd_userunitdir == ''
  systemd_dep = dependency('systemd', required: false)

  if systemd_dep.found()
    systemd_userunitdir = systemd_dep.get_pkgconfig_variable('systemduserunitdir')
  else
    # Fall back to the upstream default.
    # Note that this is always 'lib', even if libdir is something else
    systemd_userunitdir = join_paths(dconf_prefix, 'lib', 'systemd', 'user')
  endif
endif

enable_bash_completion = get_option('bash_completion')
if enable_bash_completion
  bash_completion_dep = dependency('bash-completion')
  completions_dir = bash_completion_dep.get_pkgconfig_variable(
    'completionsdir',
    # bash-completion 2.10 changed the substitutions
    define_variable: bash_completion_dep.version().version_compare('>= 2.10') ? ['datadir', dconf_datadir] : ['prefix', dconf_prefix],
  )
endif

configure_file(
  output: 'config.h',
  configuration: configuration_data(),
)

test_env = [
  'G_DEBUG=gc-friendly,fatal-warnings',
  'MALLOC_CHECK_=2',
  'LC_ALL=C.UTF-8',
]

gnome = import('gnome')
pkg = import('pkgconfig')

top_inc = include_directories('.')

subdir('shm')
subdir('gvdb')
subdir('common')
subdir('engine')
subdir('service')
subdir('gdbus')
subdir('gsettings')
subdir('client')
subdir('bin')
subdir('docs')
subdir('tests')

meson.add_install_script('meson_post_install.py', gio_module_dir)
