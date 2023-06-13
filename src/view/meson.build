wlcom_sources += files(
  'decoration.c',
  'interactive.c',
  'positioner.c',
  'view.c',
  'workspace.c',
  'xdg_popup.c',
  'xdg_shell.c',
)

if have_xwayland
  wlcom_sources += files(
    'xwayland.c',
  )
endif

if have_kde_virtual_desktop
  wlcom_sources += files(
    'kde_virtual_desktop.c',
  )
endif

if have_wlr_layer_shell
  wlcom_sources += files(
    'wlr_layer_shell.c',
  )
endif
