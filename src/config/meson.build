wlcom_sources += files(
  'common.c',
  'config.c',
)

if have_kde_global_accel
  qtkey_mapper_gen = executable('qtkey-mapper', 'qtkey_mapper.c')
  map_table_h = custom_target(
    'qtkey_map_table.h',
    output : 'qtkey_map_table.h',
    capture: true,
    command : [qtkey_mapper_gen],
  )

  wlcom_sources += map_table_h
  wlcom_sources += files(
    'kde_global_accel.c',
  )
endif

if have_kde_input
  wlcom_sources += files(
    'kde_input.c',
  )
endif

if have_ukui_gsettings
  wlcom_sources += files(
    'ukui_gsettings.c',
  )
  glib = dependency('glib-2.0', version: '>= 2.68')
  wlcom_deps += glib
endif
