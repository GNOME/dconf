cflags = '-DG_LOG_DOMAIN="dconf"'

libdconf_gdbus_thread = static_library(
  'dconf-gdbus-thread',
  sources: 'dconf-gdbus-thread.c',
  include_directories: top_inc,
  dependencies: libdconf_engine_dep,
  c_args: cflags,
  pic: true
)

libdconf_gdbus_thread_dep = declare_dependency(
  dependencies: libdconf_engine_dep,
  link_with: libdconf_gdbus_thread
)

libdconf_gdbus_filter = static_library(
  'dconf-gdbus-filter',
  sources: 'dconf-gdbus-filter.c',
  include_directories: top_inc,
  dependencies: libdconf_engine_dep,
  c_args: cflags,
  pic: true
)

libdconf_gdbus_filter_dep = declare_dependency(
  dependencies: libdconf_engine_dep,
  link_with: libdconf_gdbus_filter
)
