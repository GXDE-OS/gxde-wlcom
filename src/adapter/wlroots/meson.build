add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

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

if wlroots_has_xwayland
  add_project_arguments('-DHAVE_XWAYLAND=1', language: 'c')
endif

# glue codes
adapter_resources = files(
  'server.c',
)

adapter_static = static_library(
  'adapter_static',
  adapter_resources,
  include_directories: wlcom_inc,
  dependencies : wlroots,
  build_by_default : false,
)
