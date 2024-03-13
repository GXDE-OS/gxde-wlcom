files_libkywc = files(
  'libkywc.c',
  'output.c',
  'toplevel.c',
  'workspace.c',
  # providers
  'kywc_output.c',
  'kywc_toplevel.c',
  'kywc_workspace.c',
)

protos = [
  'kywc-output-manager-v1', 
  'kywc-toplevel-manager-v1',
  'kywc-workspace-manager-v1',
]

foreach p : protos 
  files_libkywc += protocols_code[p]
  files_libkywc += protocols_client_header[p]
endforeach

ld_args = '-Wl,--version-script,@0@/@1@'.format(
  meson.current_source_dir(), 'kywc.syms')
link_depends = files('kywc.syms')

kywc = shared_library(
  'kywc',
  files_libkywc,
  c_args: '-fvisibility=default',
  link_args : ld_args,
  link_depends : link_depends,
  dependencies: wayland_client,
  install : true,
  version : meson.project_version(),
)

libkywc = declare_dependency(
  link_with: kywc,
  dependencies: wayland_client,
  include_directories: include_directories('.'),
)

install_headers('libkywc.h')

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  kywc,
  description: 'Kylin wayland compositor client library',
  url: 'https://gitee.com/openkylin/kylin-wayland-compositor',
)
