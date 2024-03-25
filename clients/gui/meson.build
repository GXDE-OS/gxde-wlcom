if not add_languages('cpp', required: false)
  subdir_done()
endif

qt5 = import('qt5')
qt5dep = dependency('qt5', modules: ['Core', 'Gui'], private_headers: true, required: false)
if not qt5dep.found()
  subdir_done()
endif

generated_files = qt5.preprocess(
  moc_headers: ['context.h'],
  dependencies: qt5dep,
)

executable(
  'kywc-gui',
  ['main.cpp', 'context.cpp', generated_files],
  dependencies: [qt5dep, libkywc],
  cpp_args: ['-Wno-unused-parameter'],
  install: false
)
