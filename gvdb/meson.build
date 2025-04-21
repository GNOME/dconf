gvdb_dep = dependency('gvdb')

gvdb_deps = [
  gio_dep,
  glib_dep,
]

cflags = [
  '-DG_LOG_DOMAIN="gvdb (via dconf)"',
  '-DG_LOG_USE_STRUCTURED=1',
  '-DGVDB_USE_WEAK_SYMBOLS',
]

gvdb_include = dependency('gvdb-include')

libgvdb = static_library(
  'gvdb',
  include_directories: top_inc,
  dependencies: gvdb_deps + gvdb_dep,
  c_args: cflags,
  pic: true,
)

libgvdb_dep = declare_dependency(
  dependencies: gvdb_deps + gvdb_include,
  link_with: libgvdb,
)
