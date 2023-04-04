wayland_client = dependency('wayland-client')

clients = {
    'output-doctor': {
    'src': 'output_doctor.c',
    'proto': ['kde-output-management-v2',
        'kde-output-device-v2', 'kde-primary-output-v1'],
    },
}

foreach name, info : clients
    extra_src = []
    foreach p : info.get('proto')
        extra_src += protocols_code[p]
        extra_src += protocols_client_header[p]
    endforeach

    executable(
        name,
        [info.get('src'), extra_src],
        dependencies: [wayland_client, info.get('dep', [])],
        build_by_default: get_option('examples'),
    )
endforeach

shared_module(
    'simple',
    'simple_plugin.c',
    name_prefix: '',

    include_directories : wlcom_inc,
    install_dir: plugindir,
    install: true
)
