wlcom_sources += files(
  'config.c',
  'output.c',
  'layout.c',
)

have_kde_output = get_option('kde_output')
if have_kde_output
  wlcom_sources += files(
    'kde_output.c',
  )
endif

have_wlr_output = get_option('wlr_output')
if have_wlr_output
  wlcom_sources += files(
    'wlr_output.c',
  )
endif

if not have_kde_output and not have_wlr_output
  error('Must enable one output protocol support at least')
endif

add_project_arguments([
  '-DHAVE_KDE_OUTPUT=@0@'.format(have_kde_output.to_int()),
  '-DHAVE_WLR_OUTPUT=@0@'.format(have_wlr_output.to_int()),
], language: 'c')
