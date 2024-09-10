wlcom_sources += files(
  'pass.c',
  'pixel_format.c',
  'renderer.c',
  'shm_buffer.c',
  'wayland_drm.c',
)

if get_option('tracy')
  wlcom_sources += files('profile.c', 'opengl/profile.c')
endif

subdir('opengl')
