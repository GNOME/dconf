sources = gvdb_builder + libdconf_vapi + files(
  'dconf-dump.vala',
  'dconf-update.vala',
  'dconf.vala',
  'gvdb.vapi'
)

deps = [
  libdconf_common_dep,
  libdconf_dep,
  valac.find_library('posix')
]

executable(
  meson.project_name(),
  sources,
  include_directories: top_inc,
  dependencies: deps,
  install: true
)

install_data(
  'completion/dconf',
  install_dir: join_paths(dconf_datadir, 'bash-completion', 'completions')
)
