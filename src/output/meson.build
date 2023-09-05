wlcom_sources += files(
  'config.c',
  'output.c',
  'layout.c',
  'brightness.c',
  'colortemp.c',
  'modes.c',
)

if have_kde_output
  wlcom_sources += files(
    'kde_output.c',
  )
endif

if have_wlr_output
  wlcom_sources += files(
    'wlr_output.c',
  )
endif
