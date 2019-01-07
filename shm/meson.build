sources = files(
  'dconf-shm.c',
  'dconf-shm-mockable.c',
)

libdconf_shm = static_library(
  'dconf-shm',
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: dconf_c_args,
  pic: true,
)

libdconf_shm_dep = declare_dependency(
  dependencies: glib_dep,
  link_with: libdconf_shm,
)


libdconf_shm_test = static_library(
  'dconf-shm-test',
  sources: 'dconf-shm.c',
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: dconf_c_args,
  pic: true,
)

libdconf_shm_test_dep = declare_dependency(
  dependencies: glib_dep,
  link_with: libdconf_shm,
)
