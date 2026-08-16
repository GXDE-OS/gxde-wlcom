if not add_languages('cpp', native: false, required: false)
  subdir_done()
endif

qt6 = import('qt6')
occultation_qt = dependency(
  'qt6',
  modules: ['Core', 'DBus', 'Gui', 'Quick', 'Qml'],
  private_headers: true,
  required: false,
)

if not occultation_qt.found()
  warning('Qt 6 Quick was not found; occultation exoskeleton will not be built')
  subdir_done()
endif

occultation_resources = qt6.preprocess(
  qresources: 'resources.qrc',
  dependencies: occultation_qt,
)

occultation_moc = qt6.preprocess(
  moc_headers: [
    'exoskeleton/exoskeleton.h',
    'icon_item/icon_item.h',
    'switcher_item/switcher_item.h',
    'switcher_model/switcher_model.h',
    'thumbnail_item/thumbnail_item.h',
  ],
  dependencies: occultation_qt,
)

occultation_system_deps = [
  occultation_qt,
  libkywc,
  dependency('egl'),
  dependency('glesv2'),
  dependency('libdrm'),
]

occultation_exoskeleton = static_library(
  'occultation-exoskeleton',
  [
    'exoskeleton/exoskeleton.cc',
    'icon_item/icon_item.cc',
    'registration/registration.cc',
    'switcher_item/switcher_item.cc',
    'switcher_model/switcher_model.cc',
    'thumbnail_item/thumbnail_item.cc',
    occultation_resources,
    occultation_moc,
  ],
  dependencies: occultation_system_deps,
  cpp_args: ['-Wno-unused-parameter'],
  install: false,
)

occultation_exoskeleton_dependency = declare_dependency(
  link_with: occultation_exoskeleton,
  include_directories: include_directories('.'),
  dependencies: occultation_system_deps,
)

occultation_qml_smoke = executable(
  'occultation-qml-smoke',
  'smoke/smoke.cc',
  dependencies: occultation_exoskeleton_dependency,
  build_by_default: false,
  install: false,
)

occultation_preview_moc = qt6.preprocess(
  moc_headers: [
    'preview_model/preview_model.h',
    'preview_switcher/preview_switcher.h',
    'preview_thumbnail_item/preview_thumbnail_item.h',
  ],
  dependencies: occultation_qt,
)

occultation_switcher_preview = executable(
  'occultation-switcher-preview',
  [
    'preview/preview.cc',
    'preview_model/preview_model.cc',
    'preview_switcher/preview_switcher.cc',
    'preview_thumbnail_item/preview_thumbnail_item.cc',
    occultation_preview_moc,
  ],
  dependencies: occultation_exoskeleton_dependency,
  build_by_default: false,
  install: false,
)

test(
  'occultation-qml-smoke',
  occultation_qml_smoke,
  env: [
    'QT_QPA_PLATFORM=offscreen',
    'QT_QUICK_BACKEND=software',
  ],
  suite: 'occultation',
)
