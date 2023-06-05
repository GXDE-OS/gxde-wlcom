wlcom_sources += files(
  'decoration.c',
  'interactive.c',
  'view.c',
  'workspace.c',
  'xdg_popup.c',
  'xdg_shell.c',
)

have_kde_virtual_desktop = get_option('kde_virtual_desktop')
if have_kde_virtual_desktop
  wlcom_sources += files(
    'kde_virtual_desktop.c',
  )
endif

add_project_arguments([
  '-DHAVE_KDE_VIRTUAL_DESKTOP=@0@'.format(have_kde_virtual_desktop.to_int()),
], language: 'c')
