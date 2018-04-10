common_inc = include_directories('.')

headers = files(
  'dconf-changeset.h',
  'dconf-enums.h',
  'dconf-paths.h'
)

install_headers(
  headers,
  subdir: join_paths(meson.project_name(), 'common')
)

name = meson.project_name() + '-common'

sources = files(
  'dconf-changeset.c',
  'dconf-error.c',
  'dconf-paths.c'
)

cflags = ['-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())]

libdconf_common = static_library(
  name,
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags,
  pic: true
)

libdconf_common_dep = declare_dependency(
  dependencies: glib_dep,
  link_with: libdconf_common
)

cflags += cc.get_supported_arguments('-fvisibility=hidden')

libdconf_common_hidden = static_library(
  name + '-hidden',
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags,
  pic: true
)

libdconf_common_hidden_dep = declare_dependency(
  dependencies: glib_dep,
  link_with: libdconf_common_hidden
)
