project(
    'kylin-wayland-compositor',
    'c',
    # license: 'GPL-2.0-only',
    version: '0.1.0',
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

wayland_server = dependency('wayland-server', version: '>=1.19.0')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
drm_full = dependency('libdrm')
drm = drm_full.partial_dependency(compile_args: true, includes: true)
input = dependency('libinput', version: '>=1.14')
pixman = dependency('pixman-1')
math = cc.find_library('m')
jsonc = dependency('json-c', version: '>=0.13')
systemd = dependency('libsystemd')
uuid = dependency('uuid')
dl = cc.find_library('dl')

plugindir = get_option('plugin_directory')
if plugindir == ''
    plugindir = '@0@/lib/kylin-wlcom/plugins'.format(get_option('prefix'))
endif
add_project_arguments('-DPLUGIN_DIR="@0@"'.format(plugindir), language: 'c')

wlcom_inc = include_directories('include')

#subdir('protocols')
subdir('src')

if get_option('examples')
    subdir('examples')
endif

wlcom_deps = [
    wayland_server,
    wayland_protos,
    server_protos,
    xkbcommon,
    drm,
    input,
    pixman,
    math,
    jsonc,
    systemd,
    uuid,
    dl,
    adapter,
    #widget,
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
