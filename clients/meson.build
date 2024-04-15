wayland_client = dependency('wayland-client')

subdir('libkywc')

clients = {}

if have_kde_output
  clients += {
    'output-doctor': {
    'src': 'output_doctor.c',
    'proto': ['kde-output-management-v2', 'kde-output-device-v2',
              'kde-primary-output-v1', 'org-kde-kwin-dpms-manager'],
    },
  }
endif

if have_kde_virtual_desktop
  clients += {
    'virtual-desktop': {
    'src': 'virtual_desktop.c',
    'proto': ['org-kde-plasma-virtual-desktop-management'],
    },
  }
endif

egl = dependency('egl')
glesv2 = dependency('glesv2')

clients += {
  'monitor': {
  'src': ['monitor.c', 'buffer.c'],
  'dep': [libkywc, egl, glesv2],
  },
}

foreach name, info : clients
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
