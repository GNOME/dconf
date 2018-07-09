name = 'gvdb'

gvdb_builder = files('gvdb-builder.c')

sources = gvdb_builder + files('gvdb-reader.c')

cflags = '-DG_LOG_DOMAIN="gvdb (via @0@)"'.format(meson.project_name())

libgvdb = static_library(
  name,
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags
)

libgvdb_shared = static_library(
  name + '-shared',
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags,
  pic: true
)
