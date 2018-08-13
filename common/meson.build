common_inc = include_directories('.')

headers = files(
  'dconf-changeset.h',
  'dconf-enums.h',
  'dconf-paths.h',
)

install_headers(
  headers,
  subdir: join_paths('dconf', 'common'),
)

sources = files(
  'dconf-changeset.c',
  'dconf-error.c',
  'dconf-paths.c',
)

libdconf_common = static_library(
  'dconf-common',
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: '-DG_LOG_DOMAIN="dconf"',
  pic: true,
)

libdconf_common_dep = declare_dependency(
  dependencies: glib_dep,
  link_with: libdconf_common,
)
