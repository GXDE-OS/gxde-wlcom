wlcom_sources += files(
  'animator.c',
  'blur.c',
  'capture.c',
  'circle_progressbar.c',
  'config.c',
  'effect.c',
  'fade.c',
  'locate_pointer.c',
  'magic_lamp.c',
  'mouse_trail.c',
  'move.c',
  'output_transform.c',
  'showfps.c',
  'scale.c',
  'shake_cursor.c',
  'shake_view.c',
  'soft_gamma.c',
  'touch_long.c',
  'touchclick.c',
  'touch_trail.c',
  'trail.c',
  'translation.c',
  'transform.c',
  'zoom.c',
)

wlcom_sources += files(
  'ky_capture.c',
)

if have_ukui_screenshot
  wlcom_sources += files(
    'screenshot.c',
  )
endif

if have_ukui_watermark
  wlcom_sources += files(
    'watermark.c',
  )
endif

if have_kde_slide
  wlcom_sources += files(
    'slide.c',
  )
endif

subdir('shaders')
