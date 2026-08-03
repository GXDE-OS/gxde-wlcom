wlcom_sources += files(
  'action.c',
  'config.c',
  'decoration.c',
  'global_authentication.c',
  'interactive.c',
  'maximize_switcher.c',
  'window_switcher.c',
  'modal.c',
  'positioner.c',
  'split_screen_switcher.c',
  'ssd.c',
  'stack_mode.c',
  'tile_preview.c',
  'view.c',
  'window_menu.c',
  'workspace.c',
  'xdg_dialog.c',
  'xdg_popup.c',
  'xdg_shell.c',
  'xdg_activation.c',
)

wlcom_sources += files(
  'ky_toplevel.c',
  'ky_workspace.c',
)

if have_kde_virtual_desktop
  wlcom_sources += files(
    'kde_virtual_desktop.c',
  )
endif

if have_wlr_foreign_toplevel
  wlcom_sources += files(
    'wlr_foreign_toplevel.c',
  )
endif

if have_wlr_layer_shell
  wlcom_sources += files(
    'wlr_layer_shell.c',
  )
endif

if have_kde_plasma_shell
  wlcom_sources += files(
    'kde_plasma_shell.c',
  )
endif

if have_kde_plasma_window_management
  wlcom_sources += files(
    'kde_plasma_window.c',
  )
endif

if have_kde_appmenu
  wlcom_sources += files(
    'kde_appmenu.c',
  )
endif

if have_kde_blur
  wlcom_sources += files(
    'kde_blur.c',
  )
endif

if have_kde_slide
  wlcom_sources += files(
    'kde_slide.c',
  )
endif

if have_ukui_shell
  wlcom_sources += files(
    'ukui_shell.c',
  )
endif

if have_ukui_window_management
  wlcom_sources += files(
    'ukui_window.c',
  )
endif

if have_ukui_blur
  wlcom_sources += files(
    'ukui_blur.c',
  )
endif

# 从Treeland移植的部分DDE Shell协议的实现
wlcom_sources += files(
  'dde_shell.c',
  'treeland_dde_shell.c',
  'treeland_personalization.c',
)
