wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')
wayland_scanner = find_program('wayland-scanner')

protocols = {
    'xdg-shell': wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
    'kde-output-device-v2': 'kde-output-device-v2.xml',
    'kde-output-management-v2' : 'kde-output-management-v2.xml',
    'kde-primary-output-v1': 'kde-primary-output-v1.xml',
    'org-kde-kwin-dpms-manager': 'dpms.xml',
}

protos_src = []

protocols_code = {}
protocols_server_header = {}
protocols_client_header = {}
foreach name, path : protocols
    code = custom_target(
        name.underscorify() + '_c',
        input: path,
        output: '@BASENAME@-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )
    protos_src += code

    server_header = custom_target(
        name.underscorify() + '_server_h',
        input: path,
        output: '@BASENAME@-protocol.h',
        command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    )
    protos_src += server_header

    client_header = custom_target(
        name.underscorify() + '_client_h',
        input: path,
        output: '@BASENAME@-client-protocol.h',
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
        build_by_default: false,
    )

    protocols_code += { name: code }
    protocols_server_header += { name: server_header }
    protocols_client_header += { name: client_header }
endforeach

lib_server_protos = static_library(
    'server_protos',
    protos_src,
    dependencies: [wayland_server]
)

server_protos = declare_dependency(
    link_with: lib_server_protos,
    sources: protos_src,
)
