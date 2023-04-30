wlroots_version = ['>=0.17.0']
wlroots_proj = subproject(
    'wlroots',
    default_options: ['default_library=static', 'examples=false'],
    required: false,
    version: wlroots_version,
)

if wlroots_proj.found()
    wlroots = wlroots_proj.get_variable('wlroots')
    wlroots_conf = wlroots_proj.get_variable('conf_data')
    wlroots_has_xwayland = wlroots_conf.get('WLR_HAS_XWAYLAND') == 1
else
    wlroots = dependency('wlroots', version: wlroots_version)
    wlroots = dependency('wlroots')
    wlroots_has_xwayland = cc.get_define('WLR_HAS_XWAYLAND',
        prefix: '#include <wlr/config.h>', dependencies: wlroots) == '1'
endif

extra_flags = ['-DWLR_USE_UNSTABLE']

xcb = dependency('xcb')
if xcb.found() and wlroots_has_xwayland
    extra_flags += '-DHAVE_XWAYLAND=1'
endif

# glue codes
adapter_resources = files(
    'server.c',
    'input.c',
    'output.c',
    'seat.c',
    'shell.c',
)

adapter_static = static_library(
    'adapter_static',
    [adapter_resources, protocols_client_header['xdg-shell']],
    c_args: extra_flags,
    include_directories: wlcom_inc,
    dependencies : wlroots,
    build_by_default : false,
)

adapter = declare_dependency(
    link_with: adapter_static,
)
