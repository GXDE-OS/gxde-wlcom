wlcom_sources += files(
  'main.c',
  'server.c',
  'gxde_identifier.c',

  'plugin/config.c',
  'plugin/plugin.c',
)

subdir('backend')
subdir('config')
subdir('effect')
subdir('input')
subdir('output')
subdir('painter')
subdir('render')
subdir('scene')
subdir('security')
subdir('theme')
subdir('util')
subdir('view')
subdir('widget')

if have_xwayland
  subdir('xwayland')
endif
