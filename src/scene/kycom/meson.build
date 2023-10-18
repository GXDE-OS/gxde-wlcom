wlcom_sources += files(
  'opengl.c',
  'output_layout.c',
  'rect.c',
  'scene.c',
  'target.c',
  'texture.c',
  'transform.c',
)

epoxy = dependency('epoxy')
cglm = dependency('cglm')

wlcom_deps += [ epoxy, cglm ]

subdir('shaders')
subdir('effect')

