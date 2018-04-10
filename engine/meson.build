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
  meson.project_name() + '-engine',
  sources: sources,
  include_directories: top_inc,
  dependencies: glib_dep,
  c_args: cflags,
  pic: true
)
