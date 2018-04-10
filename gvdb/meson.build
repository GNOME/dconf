gvdb_builder = files('gvdb-builder.c')

sources = gvdb_builder + files('gvdb-reader.c')

deps = [
  gio_dep,
  glib_dep
]

cflags = '-DG_LOG_DOMAIN="gvdb (via @0@)"'.format(meson.project_name())

libgvdb = static_library(
  'gvdb',
  sources: sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: cflags,
  pic: true
)

libgvdb_dep = declare_dependency(
  dependencies: deps,
  link_with: libgvdb
)
