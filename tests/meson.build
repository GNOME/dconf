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

test_dir = meson.current_source_dir()

dl_dep = cc.find_library('dl', required: false)
m_dep = cc.find_library('m')

unit_tests = [
  # [name, sources, c_args, dependencies, link_with]
  ['paths', 'paths.c', [], glib_dep, libdconf_common],
  ['changeset', 'changeset.c', [], glib_dep, libdconf_common],
  ['shm', ['shm.c', 'tmpdir.c'], [], [glib_dep, dl_dep], libdconf_shm],
  ['gvdb', 'gvdb.c', '-DSRCDIR="@0@"'.format(test_dir), glib_dep, libgvdb],
  ['gdbus-thread', 'dbus.c', '-DDBUS_BACKEND="/gdbus/thread"', gio_unix_dep, libdconf_gdbus_thread],
  ['gdbus-filter', 'dbus.c', '-DDBUS_BACKEND="/gdbus/filter"', gio_unix_dep, libdconf_gdbus_filter],
  ['engine', 'engine.c', '-DSRCDIR="@0@"'.format(test_dir), [glib_dep, dl_dep, m_dep], [libdconf_engine, libdconf_common, libdconf_mock]],
  ['client', 'client.c', '-DSRCDIR="@0@"'.format(test_dir), gio_unix_dep, [libdconf_client, libdconf_engine, libdconf_common, libdconf_mock]]
]

foreach unit_test: unit_tests
  exe = executable(
    unit_test[0],
    unit_test[1],
    c_args: unit_test[2],
    dependencies: unit_test[3],
    link_with: unit_test[4]
  )

  test(unit_test[0], exe, is_parallel: false)
endforeach
