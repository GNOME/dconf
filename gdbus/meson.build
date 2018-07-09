name = meson.project_name() + '-gdbus-thread'

sources = files('dconf-gdbus-thread.c')

cflags = '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())

libdconf_gdbus_thread = static_library(
  name,
  sources: sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  c_args: cflags
)

libdconf_gdbus_thread_shared = static_library(
  name + '-shared',
  sources: sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  c_args: cflags,
  pic: true
)

name = meson.project_name() + '-gdbus-filter'

sources = files('dconf-gdbus-filter.c')

libdconf_gdbus_filter = static_library(
  name,
  sources: sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  c_args: cflags
)

libdconf_gdbus_filter_shared = static_library(
  name + '-shared',
  sources: sources,
  include_directories: top_inc,
  dependencies: gio_unix_dep,
  c_args: cflags,
  pic: true
)
