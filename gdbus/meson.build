sources = files('dconf-gdbus-thread.c')

cflags = '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())

libdconf_gdbus_thread = static_library(
  meson.project_name() + '-gdbus-thread',
  sources: sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  c_args: cflags,
  pic: true
)

sources = files('dconf-gdbus-filter.c')

libdconf_gdbus_filter = static_library(
  meson.project_name() + '-gdbus-filter',
  sources: sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  c_args: cflags,
  pic: true
)
