sources = gvdb_builder + libdconf_vapi + files(
  'dconf-dump.vala',
  'dconf-update.vala',
  'dconf.vala',
  'gvdb.vapi'
)

deps = [
  libdconf_dep,
  valac.find_library('posix')
]

executable(
  meson.project_name(),
  sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: '-w',
  install: true,
  install_dir: dconf_bindir
)

install_data(
  'completion/dconf',
  install_dir: join_paths(dconf_datadir, 'bash-completion', 'completions')
)
