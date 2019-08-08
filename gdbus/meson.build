libdconf_gdbus_common_sources = files(
  'dconf-gdbus-common.c',
)

libdconf_gdbus_thread_sources = libdconf_gdbus_common_sources + files(
  'dconf-gdbus-thread.c',
)

libdconf_gdbus_thread = static_library(
  'dconf-gdbus-thread',
  sources: libdconf_gdbus_thread_sources,
  include_directories: top_inc,
  dependencies: libdconf_engine_dep,
  c_args: dconf_c_args,
  pic: true,
)

libdconf_gdbus_thread_dep = declare_dependency(
  dependencies: libdconf_engine_dep,
  link_with: libdconf_gdbus_thread,
)

libdconf_gdbus_filter_sources = libdconf_gdbus_common_sources + files(
  'dconf-gdbus-filter.c',
)

libdconf_gdbus_filter = static_library(
  'dconf-gdbus-filter',
  sources: libdconf_gdbus_filter_sources,
  include_directories: top_inc,
  dependencies: libdconf_engine_dep,
  c_args: dconf_c_args,
  pic: true,
)

libdconf_gdbus_filter_dep = declare_dependency(
  dependencies: libdconf_engine_dep,
  link_with: libdconf_gdbus_filter,
)
