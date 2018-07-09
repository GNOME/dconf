name = meson.project_name() + '-shm'

sources = files('dconf-shm.c')

cflags = '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())

libdconf_shm = static_library(
  name,
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags
)

libdconf_shm_shared = static_library(
  name + '-shared',
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags,
  pic: true
)
