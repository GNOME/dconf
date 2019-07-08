gvdb_builder = files('gvdb-builder.c')

sources = gvdb_builder + files('gvdb-reader.c')

gvdb_deps = [
  gio_dep,
  glib_dep,
]

cflags = [
  '-DG_LOG_DOMAIN="gvdb (via dconf)"',
  '-DG_LOG_USE_STRUCTURED=1',
  '-DGVDB_USE_WEAK_SYMBOLS',
]

libgvdb = static_library(
  'gvdb',
  sources: sources,
  include_directories: top_inc,
  dependencies: gvdb_deps,
  c_args: cflags,
  pic: true,
)

libgvdb_dep = declare_dependency(
  dependencies: gvdb_deps,
  link_with: libgvdb,
)
