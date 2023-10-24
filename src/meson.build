wlcom_sources += files(
  'main.c',
  'server.c',

  'plugin/config.c',
  'plugin/plugin.c',
)

subdir('config')
subdir('input')
subdir('output')
subdir('painter')
subdir('render')
subdir('scene')
subdir('theme')
subdir('util')
subdir('view')
subdir('widget')

if have_xwayland
  subdir('xwayland')
endif
