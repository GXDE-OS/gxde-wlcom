wlcom_sources += files(
  'brightness.c',
  'gamma.c',
  'config.c',
  'layout.c',
  'modes.c',
  'output.c',
  'xdg_output.c',  
  'ky_output.c',
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

if have_ukui_output
  wlcom_sources += files(
    'ukui_output.c',
  )
endif
