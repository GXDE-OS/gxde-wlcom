wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')
wayland_scanner = find_program('wayland-scanner')

protocols = {
  'wl-drm': 'drm.xml',
  'ext-idle-notifier-v1': wl_protocol_dir / 'staging/ext-idle-notify/ext-idle-notify-v1.xml',
  'xdg-shell': wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
  'xdg-output-unstable-v1': wl_protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
  'zwp-tablet-manager-v2': wl_protocol_dir / 'unstable/tablet/tablet-unstable-v2.xml',
  'zwp-text-input-v1': wl_protocol_dir / 'unstable/text-input/text-input-unstable-v1.xml',
  'zwp-text-input-v2': 'text-input-unstable-v2.xml',
  'xdg-decoration': wl_protocol_dir / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml',
  'xdg-dialog': wl_protocol_dir / 'staging/xdg-dialog/xdg-dialog-v1.xml',
  'org-kde-kwin-idle': 'idle.xml',
  'zwp-pointer-constraints-v1': wl_protocol_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml',
}

protocols += {
  'kywc-capture-manager-v1': 'kywc-capture-v1.xml',
  'kywc-output-manager-v1': 'kywc-output-v1.xml',
  'kywc-toplevel-manager-v1': 'kywc-toplevel-v1.xml',
  'kywc-workspace-manager-v1': 'kywc-workspace-v1.xml',
}

if have_kde_output
  protocols += {
    'kde-output-device-v2': 'kde-output-device-v2.xml',
    'kde-output-management-v2' : 'kde-output-management-v2.xml',
    'kde-primary-output-v1': 'kde-primary-output-v1.xml',
    'org-kde-kwin-dpms-manager': 'dpms.xml',
  }
endif

if have_kde_virtual_desktop
  protocols += {
    'org-kde-plasma-virtual-desktop-management': 'plasma-virtual-desktop.xml',
  }
endif

if have_wlr_output
  protocols += {
    'zwlr-output-power-manager-v1': 'wlr-output-power-management-unstable-v1.xml',
  }
endif

if have_wlr_layer_shell
  protocols += {
    'zwlr-layer-shell-v1': 'wlr-layer-shell-unstable-v1.xml',
  }
endif

if have_kde_plasma_shell
  protocols += {
    'org-kde-plasma-shell': 'plasma-shell.xml',
  }
endif

if have_kde_plasma_window_management
  protocols += {
    'org-kde-plasma-window-management': 'plasma-window-management.xml',
  }
endif

if have_kde_blur
  protocols += {
    'org-kde-kwin-blur-manager': 'blur.xml',
  }
endif

if have_kde_slide
  protocols += {
    'org-kde-kwin-slide-manager': 'slide.xml',
  }
endif

if have_kde_keystate
  protocols += {
    'org-kde-kwin-keystate-manager': 'kde_keystate.xml',
  }
endif

if have_ukui_shell
  protocols += {
    'ukui-shell': 'ukui-shell.xml',
  }
endif

if have_ukui_window_management
  protocols += {
    'ukui-window-management': 'ukui-window-management.xml',
  }
endif

if have_ukui_output
  protocols += {
    'ukui-output-v1': 'ukui-output-v1.xml',
  }
endif

if have_ukui_blur
  protocols += {
    'ukui-blur': 'ukui-blur-v1.xml',
  }
endif

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
  wlcom_sources += code

  server_header = custom_target(
    name.underscorify() + '_server_h',
    input: path,
    output: '@BASENAME@-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
  wlcom_sources += server_header

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
