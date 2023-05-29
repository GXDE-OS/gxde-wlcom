wlcom_sources += files(
  'scene.c',
  'surface.c',
  'subsurface.c',
  'xdg_shell.c',
)

have_wlr_scene = get_option('wlr_scene')
add_project_arguments(
  '-DHAVE_WLR_SCENE=@0@'.format(have_wlr_scene.to_int()),
  language: 'c',
)
