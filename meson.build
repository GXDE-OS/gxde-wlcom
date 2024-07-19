project(
  'kylin-wayland-compositor',
  'c',
  license: 'MulanPSL-2.0',
  version: '1.3.0',
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

if get_option('buildtype') == 'release'
  add_project_arguments(cc.get_supported_arguments([
    '-Wno-unused-variable', '-Wno-unused-but-set-variable'
  ]), language: 'c')
endif

version = '"@0@"'.format(meson.project_version())
dpkg = find_program('dpkg-parsechangelog', native: true, required: false)
if dpkg.found()
  dpkg_version = run_command([dpkg, '-SVersion'], check: false)
  if dpkg_version.returncode() == 0
    version = '"@0@"'.format(dpkg_version.stdout().strip())
  endif
endif
git = find_program('git', native: true, required: false)
if git.found()
  git_commit = run_command([git, 'rev-parse', '--short=7', 'HEAD'], check: false)
  if git_commit.returncode() == 0
    version += '".g@0@"'.format(git_commit.stdout().strip())
  endif
endif
add_project_arguments('-DKYWC_VERSION=@0@'.format(version), language: 'c')

wlroots = dependency(
  'wlroots',
  default_options: ['default_library=static', 'examples=false', 'renderers=vulkan'],
  version: ['>=0.17.0', '<0.18.0'],
)

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')
wlroots_has_xwayland = wlroots.get_variable('have_xwayland') == 'true'

xcb = dependency('xcb')
have_xwayland = xcb.found() and wlroots_has_xwayland
add_project_arguments(
  '-DHAVE_XWAYLAND=@0@'.format(have_xwayland.to_int()),
  language: 'c',
)

wayland_server = dependency('wayland-server', version: '>=1.19.0')
wayland_protos = dependency('wayland-protocols', version: '>=1.34')
xkbcommon = dependency('xkbcommon')
drm = dependency('libdrm')
input = dependency('libinput', version: '>=1.14')
pixman = dependency('pixman-1')
math = cc.find_library('m')
jsonc = dependency('json-c', version: '>=0.13')
sdbus = dependency('libsystemd')
dl = cc.find_library('dl')
thread = dependency('threads')
cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
rsvg  = dependency('librsvg-2.0', version: '>=2.46')
udev = dependency('libudev')
unwind = dependency('libunwind', required : get_option('libunwind'))

plugindir = get_option('plugin_directory')
if plugindir == ''
  plugindir = '@0@/lib/kylin-wlcom/plugins'.format(get_option('prefix'))
endif
add_project_arguments('-DPLUGIN_DIR="@0@"'.format(plugindir), language: 'c')

wlcom_sources = []

wlcom_deps = [
  wayland_server, wayland_protos,
  wlroots, xkbcommon, drm, input, udev,
  pixman, xcb, jsonc, sdbus,
  math, dl, thread, unwind,
  cairo, pango, pangocairo, rsvg,
]

wlcom_inc = include_directories('include')

have_examples = get_option('examples')
have_kde_output = get_option('kde_output')
have_kde_virtual_desktop = get_option('kde_virtual_desktop')
have_wlr_output = get_option('wlr_output')
have_wlr_foreign_toplevel = get_option('wlr_foreign_toplevel')
have_wlr_layer_shell = get_option('wlr_layer_shell')
have_kde_global_accel = get_option('kde_global_accel')
have_kde_input = get_option('kde_input')
have_kde_plasma_shell = get_option('kde_plasma_shell')
have_kde_plasma_window_management = get_option('kde_plasma_window_management')
have_kde_blur = get_option('kde_blur')
have_kde_slide = get_option('kde_slide')
have_ukui_screenshot = get_option('ukui_screenshot')
have_ukui_gsettings = get_option('ukui_gsettings')
have_kde_keystate = get_option('kde_keystate')
have_libkywc = get_option('libkywc')
have_kde_clipboard = get_option('kde_clipboard')
have_ukui_watermark = get_option('ukui_watermark')
have_ukui_shell = get_option('ukui_shell')
have_ukui_window_management = get_option('ukui_window_management')

add_project_arguments([
  '-DHAVE_LIBUNWIND=@0@'.format(unwind.found().to_int()),
  '-DHAVE_KDE_OUTPUT=@0@'.format(have_kde_output.to_int()),
  '-DHAVE_KDE_VIRTUAL_DESKTOP=@0@'.format(have_kde_virtual_desktop.to_int()),
  '-DHAVE_WLR_OUTPUT=@0@'.format(have_wlr_output.to_int()),
  '-DHAVE_WLR_FOREIGN_TOPLEVEL=@0@'.format(have_wlr_foreign_toplevel.to_int()),
  '-DHAVE_WLR_LAYER_SHELL=@0@'.format(have_wlr_layer_shell.to_int()),
  '-DHAVE_KDE_GLOBAL_ACCEL=@0@'.format(have_kde_global_accel.to_int()),
  '-DHAVE_KDE_INPUT=@0@'.format(have_kde_input.to_int()),
  '-DHAVE_KDE_PLASMA_SHELL=@0@'.format(have_kde_plasma_shell.to_int()),
  '-DHAVE_KDE_PLASMA_WINDOW_MANAGEMENT=@0@'.format(have_kde_plasma_window_management.to_int()),
  '-DHAVE_KDE_BLUR=@0@'.format(have_kde_blur.to_int()),
  '-DHAVE_KDE_SLIDE=@0@'.format(have_kde_slide.to_int()),
  '-DHAVE_UKUI_SCREENSHOT=@0@'.format(have_ukui_screenshot.to_int()), 
  '-DHAVE_UKUI_GSETTINGS=@0@'.format(have_ukui_gsettings.to_int()), 
  '-DHAVE_KDE_KEYSTATE=@0@'.format(have_kde_keystate.to_int()),
  '-DHAVE_KDE_CLIPBOARD=@0@'.format(have_kde_clipboard.to_int()),
  '-DHAVE_UKUI_WATERMARK=@0@'.format(have_ukui_watermark.to_int()),
  '-DHAVE_UKUI_SHELL=@0@'.format(have_ukui_shell.to_int()),
  '-DHAVE_UKUI_WINDOW_MANAGEMENT=@0@'.format(have_ukui_window_management.to_int()),
], language: 'c')

msgfmt = find_program('msgfmt', required: get_option('nls'))
add_project_arguments('-DHAVE_NLS=@0@'.format(msgfmt.found().to_int()), language: 'c')
if msgfmt.found()
  source_root = meson.current_source_dir()
  subdir('po')
endif

subdir('data')
subdir('protocols')
subdir('src')

if have_libkywc
  subdir('libkywc')
endif

if have_examples
  subdir('examples')
endif

summary({
  'prefix': get_option('prefix'),
  'plugin': plugindir,
  },
  section: 'Directories')

summary({
  'examples': have_examples,
  'libunwind': unwind.found(),
  'nls': msgfmt.found(),
  'libkywc': have_libkywc,
  'kde_output': have_kde_output,
  'kde_virtual_desktop': have_kde_virtual_desktop,
  'wlr_output': have_wlr_output,
  'wlr_foreign_toplevel': have_wlr_foreign_toplevel,
  'wlr_layer_shell': have_wlr_layer_shell,
  'kde_global_accel': have_kde_global_accel,
  'kde_input': have_kde_input,
  'kde_plasma_shell': have_kde_plasma_shell,
  'kde_plasma_window_management': have_kde_plasma_window_management,
  'kde_blur': have_kde_blur,
  'kde_slide': have_kde_slide,
  'ukui_screenshot': have_ukui_screenshot,
  'ukui_gsettings': have_ukui_gsettings,
  'kde_keystate': have_kde_keystate,
  'kde_clipboard': have_kde_clipboard,
  'ukui_watermark': have_ukui_watermark,
  'ukui_shell': have_ukui_shell,
  'ukui_window_management': have_ukui_window_management,
  },
  section: 'Configuration', bool_yn: true)

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
