name = meson.project_name() + '-gdbus-thread'

cflags = '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())

libdconf_gdbus_thread = static_library(
  name,
  sources: name + '.c',
  include_directories: top_inc,
  dependencies: libdconf_engine_dep,
  c_args: cflags,
  pic: true
)

libdconf_gdbus_thread_dep = declare_dependency(
  dependencies: libdconf_engine_dep,
  link_with: libdconf_gdbus_thread
)

name = meson.project_name() + '-gdbus-filter'

libdconf_gdbus_filter = static_library(
  name,
  sources: name + '.c',
  include_directories: top_inc,
  dependencies: libdconf_engine_dep,
  c_args: cflags,
  pic: true
)

libdconf_gdbus_filter_dep = declare_dependency(
  dependencies: libdconf_engine_dep,
  link_with: libdconf_gdbus_filter
)
