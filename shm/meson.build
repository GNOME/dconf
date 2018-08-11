libdconf_shm = static_library(
  'dconf-shm',
  sources: 'dconf-shm.c',
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: '-DG_LOG_DOMAIN="dconf"',
  pic: true
)

libdconf_shm_dep = declare_dependency(
  dependencies: glib_dep,
  link_with: libdconf_shm
)
