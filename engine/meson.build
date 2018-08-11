sources = files(
  'dconf-engine.c',
  'dconf-engine-profile.c',
  'dconf-engine-source.c',
  'dconf-engine-source-file.c',
  'dconf-engine-source-user.c',
  'dconf-engine-source-service.c',
  'dconf-engine-source-system.c',
)

engine_deps = [
  libdconf_common_dep,
  libgvdb_dep,
]

libdconf_engine = static_library(
  'dconf-engine',
  sources: sources,
  include_directories: top_inc,
  dependencies: engine_deps + [libdconf_shm_dep],
  c_args: '-DG_LOG_DOMAIN="dconf"',
  pic: true,
)

libdconf_engine_dep = declare_dependency(
  dependencies: engine_deps,
  link_with: libdconf_engine,
)
