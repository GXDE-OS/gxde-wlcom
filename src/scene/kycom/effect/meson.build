wlcom_sources += files(
    'effects.c',
    'effect_error.c',
    'effect_addon.c',
    'effect_transform.c',
    'effect_view_impl.c'
)

effects_ld_args = ['-Wl,--version-script',
            join_paths(meson.current_source_dir(), 'effects.sym')]

effects_link_depends = files('effects.sym')

shared_module(
    'kywc_round_corner_effect',
    ['round_corner.c'],
    include_directories : [wlcom_inc,
                         '/usr/include/pixman-1',
                         ],
    c_args: '-fvisibility=default',
    link_args : effects_ld_args,
    link_depends : effects_link_depends,
    install_dir: plugindir,
    install: true
)

shared_module(
    'kywc_blur_effect',
    ['blur.c'],
    include_directories : [wlcom_inc,
                         '/usr/include/pixman-1',
                         ],
    c_args: '-fvisibility=default',
    link_args : effects_ld_args,
    link_depends : effects_link_depends,
    install_dir: plugindir,
    install: true
)

shared_module(
     'kywc_scale_effect',
     ['scale.c'],
    include_directories : [wlcom_inc,
                          '/usr/include/pixman-1',
                          ],
     c_args: '-fvisibility=default',
     link_args : effects_ld_args,
     link_depends : effects_link_depends,
     install_dir: plugindir,
     install: true
 )
