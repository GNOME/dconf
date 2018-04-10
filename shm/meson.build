name = meson.project_name() + '-shm'

cflags = '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())

libdconf_shm = static_library(
  name,
  sources: name + '.c',
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags,
  pic: true
)

libdconf_shm_dep = declare_dependency(
  dependencies: glib_dep,
  link_with: libdconf_shm
)
