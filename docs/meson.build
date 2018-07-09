gnome.gtkdoc(
  meson.project_name(),
  main_xml: meson.project_name() + '-docs.xml',
  src_dir: [
    common_inc,
    client_inc
  ],
  dependencies: libdconf_dep,
  scan_args: '--rebuild-types',
  gobject_typesfile: meson.project_name() + '.types',
  install: true,
  install_dir: join_paths(dconf_datadir, 'gtk-doc', 'html', meson.project_name())
)

if get_option('enable-man')
  xsltproc = find_program('xsltproc', required: false)
  assert(xsltproc.found(), 'xsltproc is required for enable-man')

  xsltproc_cmd = [
    xsltproc,
    '--output', '@OUTPUT@',
    '--nonet',
    '--stringparam', 'man.output.quietly', '1',
    '--stringparam', 'funcsynopsis.style', 'ansi',
    '--stringparam', 'man.th.extra1.suppress', '1',
    '--stringparam', 'man.authors.section.enabled', '0',
    '--stringparam', 'man.copyright.section.enabled', '0',
    'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
    '@INPUT@'
  ]

  mans = [
    ['dconf-service.xml', 'dconf-service', '1'],
    ['dconf-tool.xml', 'dconf', '1'],
    ['dconf-overview.xml', 'dconf', '7']
  ]

  foreach man: mans
    output = '@0@.@1@'.format(man[1], man[2])
    man_dir = 'man' + man[2]

    custom_target(
      output,
      input: man[0],
      output: output,
      command: xsltproc_cmd,
      install: true,
      install_dir: join_paths(dconf_mandir, man_dir)
    )
  endforeach
endif
