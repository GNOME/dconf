sources = files(
  'dconf-mock-dbus.c',
  'dconf-mock-gvdb.c',
  'dconf-mock-shm.c',
)

libdconf_mock = static_library(
  'dconf-mock',
  sources: sources,
  dependencies: [glib_dep, libgvdb_dep],
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
  ['shm', ['shm.c', 'tmpdir.c'], [], [dl_dep, libdconf_common_dep, libdconf_shm_test_dep], []],
  ['gvdb', 'gvdb.c', '-DSRCDIR="@0@"'.format(test_dir), libgvdb_dep, []],
  ['gdbus-thread', 'dbus.c', '-DDBUS_BACKEND="/gdbus/thread"', libdconf_gdbus_thread_dep, []],
  ['gdbus-filter', 'dbus.c', '-DDBUS_BACKEND="/gdbus/filter"', libdconf_gdbus_filter_dep, []],
  ['gdbus-thread-leak', 'dbus-leak.c', '-DDBUS_BACKEND="/gdbus/thread"', [libdconf_client_dep, libdconf_gdbus_thread_dep], []],
  ['gdbus-filter-leak', 'dbus-leak.c', '-DDBUS_BACKEND="/gdbus/filter"', [libdconf_client_dep, libdconf_gdbus_filter_dep], []],
  ['engine', 'engine.c', '-DSRCDIR="@0@"'.format(test_dir), [dl_dep, libdconf_engine_test_dep, m_dep], libdconf_mock],
  ['client', 'client.c', '-DSRCDIR="@0@"'.format(test_dir), [libdconf_client_dep, libdconf_engine_dep], libdconf_mock],
  ['writer', 'writer.c', '-DSRCDIR="@0@"'.format(test_dir), [glib_dep, dl_dep, m_dep, libdconf_service_dep], [libdconf_mock]],
]

foreach unit_test: unit_tests
  exe = executable(
    unit_test[0],
    unit_test[1],
    c_args: unit_test[2],
    dependencies: unit_test[3],
    link_with: unit_test[4],
    include_directories: [top_inc, include_directories('../service')],
  )

  test(unit_test[0], exe, is_parallel: false, env: envs)
endforeach

symbol_test = find_program('abicheck.sh')

abi_tests = [
  ['gsettings', libdconf_settings, files('../gsettings/symbols.txt')[0]],
  ['libdconf', libdconf, files('../client/symbols.txt')[0]],
]

foreach abi_test: abi_tests
  test(
    abi_test[0],
    symbol_test,
    env: envs,
    args: [abi_test[1].full_path(), abi_test[2]],
  )
endforeach


python3 = find_program('python3', required: false)
dbus_daemon = find_program('dbus-daemon', required: false)

if python3.found() and dbus_daemon.found()
  test_dconf_py = find_program('test-dconf.py')
  test(
    'dconf',
    test_dconf_py,
    args: [dconf.full_path(), dconf_service.full_path()]
  )
else
  message('Skipping dconf test because python3 or dbus-daemon is not available')
endif

lint_scripts = [
  'shellcheck.sh',
]

if have_bash
  foreach test_name : lint_scripts
    test(
      test_name, files(test_name),
      env : test_env,
      suite : ['lint'],
      protocol : 'tap',
    )
  endforeach
endif
