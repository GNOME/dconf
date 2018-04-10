sources = files(
  'dconf-mock-dbus.c',
  'dconf-mock-gvdb.c',
  'dconf-mock-shm.c'
)

libdconf_mock = static_library(
  meson.project_name() + '-mock',
  sources: sources,
  dependencies: glib_dep
)

envs = test_env + [
  'G_TEST_SRCDIR=' + meson.current_source_dir(),
  'G_TEST_BUILDDIR=' + meson.current_build_dir(),
]

test_dir = meson.current_source_dir()

dl_dep = cc.find_library('dl', required: false)
m_dep = cc.find_library('m')

unit_tests = [
  # [name, sources, c_args, dependencies, link_with]
  ['paths', 'paths.c', [], libdconf_common_dep, []],
  ['changeset', 'changeset.c', [], libdconf_common_dep, []],
  ['shm', ['shm.c', 'tmpdir.c'], [], [dl_dep, libdconf_common_dep, libdconf_shm_dep], []],
  ['gvdb', 'gvdb.c', '-DSRCDIR="@0@"'.format(test_dir), libgvdb_dep, []],
  ['gdbus-thread', 'dbus.c', '-DDBUS_BACKEND="/gdbus/thread"', libdconf_gdbus_thread_dep, []],
  ['gdbus-filter', 'dbus.c', '-DDBUS_BACKEND="/gdbus/filter"', libdconf_gdbus_filter_dep, []],
  ['engine', 'engine.c', '-DSRCDIR="@0@"'.format(test_dir), [dl_dep, libdconf_engine_dep, m_dep], libdconf_mock],
  ['client', 'client.c', '-DSRCDIR="@0@"'.format(test_dir), [libdconf_client_dep, libdconf_engine_dep], libdconf_mock]
]

foreach unit_test: unit_tests
  exe = executable(
    unit_test[0],
    unit_test[1],
    c_args: unit_test[2],
    dependencies: unit_test[3],
    link_with: unit_test[4]
  )

  test(unit_test[0], exe, is_parallel: false, env: envs)
endforeach
