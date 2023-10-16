crypto = dependency('openssl')

util_sources = files(
  'fscan.c',
  'global.c',
  'identifier.c',
  'logger.c',
  'queue.c',
  'spawn.c',
  'sysfs.c',
)

util_static = static_library(
  'util_static',
  util_sources,
  include_directories: wlcom_inc,
  dependencies : crypto,
  build_by_default : false,
)

util = declare_dependency(
  link_with: util_static,
)
