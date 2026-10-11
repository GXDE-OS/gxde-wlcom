epoxy = dependency('epoxy', version: '>=1.5.4')
gbm = dependency('gbm', version: '>=17.1.0')
wlcom_deps += [epoxy, gbm]

wlcom_sources += files(
  'buffer.c',
  'egl.c',
  'pass.c',
  'renderer.c',
  'texture.c',
)

subdir('shaders')
