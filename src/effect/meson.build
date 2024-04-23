wlcom_sources += files(
  'blur.c',
  'capture.c',
  'config.c',
  'effect.c',
  'showfps.c',
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

subdir('shaders')
