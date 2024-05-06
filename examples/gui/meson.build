if not add_languages('cpp', required: false)
  subdir_done()
endif

qt5 = import('qt5')

qt5dep = dependency('qt5', modules: ['Core', 'Gui', 'Widgets', 'Qml', 'Quick','QuickWidgets'], private_headers: true, required: false)
if not qt5dep.found()
  subdir_done()
endif

generated_files = qt5.preprocess(
  moc_headers: ['context.h', 'widget.h'],
  dependencies: qt5dep,
)

executable(
  'kywc-gui',
  ['main.cpp', 'context.cpp', 'ui.cpp', 'workspace.cpp', 'output.cpp', 'toplevel.cpp', 'thumbnail.cpp', generated_files],
  dependencies: [qt5dep, libkywc],
  cpp_args: ['-Wno-unused-parameter'],
  install: false
)

# Preprocess the .qrc file using the rcc command and generate a C++ source file
rcc_command = ['rcc', '-name', 'resources', '-o', 'resources.cpp', 'qml.qrc']
run_command(rcc_command, check: true)

thu_files = qt5.preprocess(
  moc_headers: ['context.h', 'ThumbnailItem.h', 'ThumInfo.h'],
  dependencies: qt5dep,
)

egl = dependency('egl')
glesv2 = dependency('glesv2')
libdrm_dep = dependency('libdrm')

executable(
  'thumbnail',
  ['ThumMain.cpp', 'context.cpp', 'workspace.cpp', 'output.cpp', 'toplevel.cpp', 'thumbnail.cpp', 'ThumbnailItem.cpp', 'resources.cpp', thu_files],
  dependencies: [qt5dep, libkywc, egl, glesv2, libdrm_dep],
  cpp_args: ['-Wno-unused-parameter'],
  install: false
)
