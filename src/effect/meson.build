wlcom_sources += files(
  'animator.c',
  'blur.c',
  'capture.c',
  'config.c',
  'effect.c',
  'fade.c',
  'move.c',
  'showfps.c',
  'scale.c',
  'soft_gamma.c',
  'touchclick.c',
  'touchtrail.c',
  'translation.c',
  'long_touch.c',
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
