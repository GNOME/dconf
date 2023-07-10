testable_sources = files(
  'dconf-engine.c',
  'dconf-engine-profile.c',
  'dconf-engine-source.c',
  'dconf-engine-source-file.c',
  'dconf-engine-source-user.c',
  'dconf-engine-source-service.c',
  'dconf-engine-source-system.c',
)

sources = testable_sources + files(
  'dconf-engine-mockable.c',
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
  c_args: dconf_c_args,
  pic: true,
)

libdconf_engine_dep = declare_dependency(
  dependencies: engine_deps,
  link_with: libdconf_engine,
)

libdconf_engine_test = static_library(
  'dconf-engine-test',
  sources: testable_sources,
  include_directories: top_inc,
  dependencies: engine_deps,
  c_args: dconf_c_args,
  pic: true,
)

libdconf_engine_test_dep = declare_dependency(
  dependencies: engine_deps,
  link_with: libdconf_engine_test,
)
