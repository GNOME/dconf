sources = files(
  'dconf.c',
)

bin_deps = [
  libdconf_common_dep,
  libdconf_dep,
]

dconf = executable(
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
