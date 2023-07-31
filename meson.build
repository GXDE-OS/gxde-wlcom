project(
  'kylin-wayland-compositor',
  'c',
  # license: 'GPL-2.0-only',
  version: '0.2.0',
  default_options: [
    'b_ndebug=if-release',
    'c_std=c11',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
  '-Wundef',
  '-Wlogical-op',
  '-Wmissing-include-dirs',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Winit-self',
  '-Wstrict-prototypes',
  '-Wimplicit-fallthrough=2',
  '-Wendif-labels',
  '-Wstrict-aliasing=2',
  '-Woverflow',
  '-Wmissing-prototypes',
  '-Walloca',
  '-Wunused-macros',

  '-Wno-unused-parameter',
]), language: 'c')

version='"@0@"'.format(meson.project_version())
git = find_program('git', native: true, required: false)
if git.found()
  git_commit = run_command([git, 'describe', '--dirty'], check: false)
  if git_commit.returncode() == 0
    version = '"@0@"'.format(git_commit.stdout().strip())
  endif
endif
add_project_arguments('-DKYWC_VERSION=@0@'.format(version), language: 'c')

wlroots_version = ['>=0.17.0']
wlroots_proj = subproject(
  'wlroots',
  default_options: ['default_library=static', 'examples=false'],
  required: false,
  version: wlroots_version,
)
add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

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

xcb = dependency('xcb')
have_xwayland = xcb.found() and wlroots_has_xwayland
add_project_arguments(
  '-DHAVE_XWAYLAND=@0@'.format(have_xwayland.to_int()),
  language: 'c',
)

wayland_server = dependency('wayland-server', version: '>=1.19.0')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
drm_full = dependency('libdrm')
drm = drm_full.partial_dependency(compile_args: true, includes: true)
input = dependency('libinput', version: '>=1.14')
pixman = dependency('pixman-1')
math = cc.find_library('m')
jsonc = dependency('json-c', version: '>=0.13')
sdbus = dependency('libsystemd')
dl = cc.find_library('dl')
cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
rsvg  = dependency('librsvg-2.0', version: '>=2.46')

plugindir = get_option('plugin_directory')
if plugindir == ''
  plugindir = '@0@/lib/kylin-wlcom/plugins'.format(get_option('prefix'))
endif
add_project_arguments('-DPLUGIN_DIR="@0@"'.format(plugindir), language: 'c')

wlcom_inc = include_directories('include')

have_examples = get_option('examples')
have_kde_output = get_option('kde_output')
have_kde_virtual_desktop = get_option('kde_virtual_desktop')
have_wlr_output = get_option('wlr_output')
have_wlr_scene = get_option('wlr_scene')
have_wlr_foreign_toplevel = get_option('wlr_foreign_toplevel')
have_wlr_layer_shell = get_option('wlr_layer_shell')
have_kde_global_accel = get_option('kde_global_accel')
have_kde_input = get_option('kde_input')
have_kde_plasma_shell = get_option('kde_plasma_shell')
have_kde_plasma_window_management = get_option('kde_plasma_window_management')
have_kde_blur = get_option('kde_blur')
have_kde_global_settings = get_option('kde_global_settings')

if not have_kde_output and not have_wlr_output
  error('Must enable one output protocol support at least')
endif

add_project_arguments([
  '-DHAVE_KDE_OUTPUT=@0@'.format(have_kde_output.to_int()),
  '-DHAVE_KDE_VIRTUAL_DESKTOP=@0@'.format(have_kde_virtual_desktop.to_int()),
  '-DHAVE_WLR_OUTPUT=@0@'.format(have_wlr_output.to_int()),
  '-DHAVE_WLR_SCENE=@0@'.format(have_wlr_scene.to_int()),
  '-DHAVE_WLR_FOREIGN_TOPLEVEL=@0@'.format(have_wlr_foreign_toplevel.to_int()),
  '-DHAVE_WLR_LAYER_SHELL=@0@'.format(have_wlr_layer_shell.to_int()),
  '-DHAVE_KDE_GLOBAL_ACCEL=@0@'.format(have_kde_global_accel.to_int()),
  '-DHAVE_KDE_INPUT=@0@'.format(have_kde_input.to_int()),
  '-DHAVE_KDE_PLASMA_SHELL=@0@'.format(have_kde_plasma_shell.to_int()),
  '-DHAVE_KDE_PLASMA_WINDOW_MANAGEMENT=@0@'.format(have_kde_plasma_window_management.to_int()),
  '-DHAVE_KDE_BLUR=@0@'.format(have_kde_blur.to_int()),
  '-DHAVE_KDE_GLOBAL_SETTINGS=@0@'.format(have_kde_global_settings.to_int()), 
], language: 'c')

subdir('src')
subdir('protocols')

if have_examples
  subdir('clients')
  subdir('examples')
endif

wlcom_deps = [
  wayland_server, wayland_protos, server_protos,
  wlroots, xkbcommon, drm, input,
  pixman, math, jsonc,
  sdbus, util, dl,
  cairo, pango, pangocairo, rsvg,
]

symbols_file = 'wlcom.syms'
symbols_flag = '-Wl,--version-script,@0@/@1@'.format(
  meson.current_source_dir(), symbols_file)

executable(
  'kylin-wlcom',
  wlcom_sources,
  include_directories: wlcom_inc,
  dependencies: wlcom_deps,
  install: true,
  export_dynamic: true,
  link_args: symbols_flag,
  link_depends: symbols_file,
)
