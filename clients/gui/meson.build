if not add_languages('cpp', required: false)
  subdir_done()
endif

qt5 = import('qt5')
qt5dep = dependency('qt5', modules: ['Core', 'Gui', 'Widgets'], private_headers: true)

generated_files = qt5.preprocess(
  moc_headers: ['context.h', 'widget.h'],
  dependencies: qt5dep,
)

executable(
  'kywc-gui',
  ['main.cpp', 'context.cpp', 'ui.cpp', 'workspace.cpp', 'output.cpp', 'toplevel.cpp', generated_files],
  dependencies: [qt5dep, libkywc],
  cpp_args: ['-Wno-unused-parameter'],
  install: false
)
