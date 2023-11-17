epoxy = dependency('epoxy', version: '>=1.5.4')
gbm = dependency('gbm', version: '>=17.1.0')
wlcom_deps += [epoxy, gbm, drm_full]

wlcom_sources += files(
  'buffer.c',
  'egl.c',
  'pass.c',
  'pixel_format.c',
  'renderer.c',
  'texture.c',
)

subdir('shaders')
