name = meson.project_name() + '-engine'

sources = files(
  'dconf-engine.c',
  'dconf-engine-profile.c',
  'dconf-engine-source.c',
  'dconf-engine-source-file.c',
  'dconf-engine-source-user.c',
  'dconf-engine-source-service.c',
  'dconf-engine-source-system.c'
)

cflags = '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name())

libdconf_engine = static_library(
  name,
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags
)

libdconf_engine_shared = static_library(
  name + '-shared',
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags,
  pic: true
)
