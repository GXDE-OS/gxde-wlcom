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
    add_project_arguments('-DHAVE_KDE_OUTPUT=1', language: 'c')
endif

have_wlr_output = get_option('wlr_output')
if have_wlr_output
    wlcom_sources += files(
        'wlr_output.c',
    )
    add_project_arguments('-DHAVE_WLR_OUTPUT=1', language: 'c')
endif
