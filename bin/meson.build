sources = gvdb_builder + libdconf_vapi + files(
  'dconf-dump.vala',
  'dconf-update.vala',
  'dconf.vala',
  'gvdb.vapi',
)

bin_deps = [
  libdconf_common_dep,
  libdconf_dep,
  valac.find_library('posix'),
]

executable(
  'dconf',
  sources,
  include_directories: top_inc,
  dependencies: bin_deps,
  install: true,
)

if enable_bash_completion
  install_data(
    'completion/dconf',
    install_dir: completions_dir,
  )
endif
