wlcom_sources += files(
  'animation.c',
  'box.c',
  'scene.c',
  'surface.c',
  'subsurface.c',
  'xdg_shell.c',
)

if not have_wlr_scene
  wlcom_sources += files(
    'buffer.c',
    'output.c',
    'rect.c',
  )
endif
