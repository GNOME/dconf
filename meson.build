project(
  'dconf', ['c', 'vala'],
  version: '0.28.0',
  license: 'LGPL2.1+',
  default_options: [
    'buildtype=debugoptimized',
    'warning_level=1'
  ],
  meson_version: '>= 0.41.0'
)

dconf_version = meson.project_version()
version_array = dconf_version.split('.')
dconf_major_version = version_array[0].to_int()
dconf_minor_version = version_array[1].to_int()
dconf_micro_version = version_array[2].to_int()

dconf_prefix = get_option('prefix')
dconf_bindir = join_paths(dconf_prefix, get_option('bindir'))
dconf_datadir = join_paths(dconf_prefix, get_option('datadir'))
dconf_includedir = join_paths(dconf_prefix, get_option('includedir'))
dconf_libdir = join_paths(dconf_prefix, get_option('libdir'))
dconf_libexecdir = join_paths(dconf_prefix, get_option('libexecdir'))
dconf_mandir = join_paths(dconf_prefix, get_option('mandir'))

dconf_namespace = 'ca.desrt.dconf'

soversion = 1
current = 0
revision = 0
libversion = '@0@.@1@.@2@'.format(soversion, current, revision)

cc = meson.get_compiler('c')
valac = meson.get_compiler('vala')

config_h = configuration_data()

# package
set_defines = [
  ['PACKAGE', meson.project_name()],
  ['PACKAGE_BUGREPORT', 'http://bugzilla.gnome.org/enter_bug.cgi?product=dconf'],
  ['PACKAGE_NAME', meson.project_name()],
  ['PACKAGE_STRING', '@0@ @1@'.format(meson.project_name(), dconf_version)],
  ['PACKAGE_TARNAME', meson.project_name()],
  ['PACKAGE_URL', 'https://wiki.gnome.org/Projects/dconf'],
  ['PACKAGE_VERSION', dconf_version],
  ['VERSION', dconf_version],
  ['GETTEXT_PACKAGE', meson.project_name()]
]

foreach define: set_defines
  config_h.set_quoted(define[0], define[1])
endforeach

# compiler flags
common_flags = ['-DHAVE_CONFIG_H']

if get_option('buildtype').contains('debug')
  test_cflags = [
    '-fno-common',
    '-Wmissing-prototypes',
    '-Wwrite-strings'
  ]

  foreach cflag: test_cflags
    if cc.has_argument(cflag)
      common_flags += [cflag]
    endif
  endforeach
endif

add_project_arguments(common_flags, language: 'c')

gio_unix_req_version = '>= 2.25.7'

glib_dep = dependency('glib-2.0', version: '>= 2.44.0')
gio_unix_dep = dependency('gio-unix-2.0', version: gio_unix_req_version)

gio_querymodules = find_program('gio-querymodules', required: false)
if gio_querymodules.found()
  gio_modules_dir = get_option('with-gio-modules-dir').strip()
  if gio_modules_dir == ''
    gio_modules_dir = join_paths(dconf_libdir, 'gio', 'modules')
  endif

  meson.add_install_script('meson_post_install.py', gio_querymodules.path(), gio_modules_dir)
endif

configure_file(
  output: 'config.h',
  configuration: config_h
)

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

if get_option('enable-gtk-doc')
  subdir('docs')
endif

subdir('tests')
