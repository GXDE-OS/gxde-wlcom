wayland_client = dependency('wayland-client')

examples = {}

if have_kde_output
  examples += {
    'output-doctor': {
    'src': 'output_doctor.c',
    'proto': ['kde-output-management-v2', 'kde-output-device-v2',
              'kde-primary-output-v1', 'org-kde-kwin-dpms-manager'],
    },
  }
endif

if have_kde_virtual_desktop
  examples += {
    'virtual-desktop': {
    'src': 'virtual_desktop.c',
    'proto': ['org-kde-plasma-virtual-desktop-management'],
    },
  }
endif

egl = dependency('egl')
glesv2 = dependency('glesv2')
wayland_egl = dependency('wayland-egl')

examples += {
  'monitor': {
  'src': 'monitor.c',
  'dep': libkywc,
  },
}

examples += {
  'thumbnail': {
  'src': ['thumbnail.c', 'buffer.c'],
  'proto': ['xdg-shell', 'xdg-decoration'],
  'dep': [libkywc, egl, glesv2, wayland_egl],
  },
}

if have_kde_keystate
  examples += {
    'kde_keystate': {
    'src': 'kde_keystate.c',
    'proto': ['org-kde-kwin-keystate-manager'],
    },
  }
endif

foreach name, info : examples
  extra_src = []
  foreach p : info.get('proto', [])
    extra_src += protocols_code[p]
    extra_src += protocols_client_header[p]
  endforeach

  executable(
    name,
    [info.get('src'), extra_src],
    dependencies: [wayland_client, info.get('dep', [])],
    build_by_default: true,
  )
endforeach

subdir('gui')

shared_module(
  'simple',
  'simple_plugin.c',
  name_prefix: '',

  include_directories : wlcom_inc,
  install_dir: plugindir,
  install: false
)
