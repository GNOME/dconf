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
  'dconf-gvdb-utils.c',
)

libdconf_common = static_library(
  'dconf-common',
  sources: sources,
  include_directories: top_inc,
  dependencies: [glib_dep, libgvdb_dep],
  c_args: dconf_c_args,
  pic: true,
)

libdconf_common_dep = declare_dependency(
  dependencies: [glib_dep, libgvdb_dep],
  link_whole: libdconf_common,
)

libdconf_common_hidden = static_library(
  'dconf-common-hidden',
  sources: sources,
  include_directories: top_inc,
  dependencies: [glib_dep, libgvdb_dep],
  c_args: dconf_c_args + cc.get_supported_arguments('-fvisibility=hidden'),
  pic: true,
)

libdconf_common_hidden_dep = declare_dependency(
  dependencies: [glib_dep, libgvdb_dep],
  link_with: libdconf_common_hidden,
)
