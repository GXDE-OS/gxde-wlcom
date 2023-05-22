crypto = dependency('openssl')

util_resources = files(
    'identifier.c',
    'logger.c',
    'spawn.c',
)

util_static = static_library(
    'util_static',
    util_resources,
    include_directories: wlcom_inc,
    dependencies : crypto,
    build_by_default : false,
)

util = declare_dependency(
    link_with: util_static,
)
