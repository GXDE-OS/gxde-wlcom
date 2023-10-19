svgs = [
  'button_light.svg',
  'button_dark.svg',
  'unknown.svg'
]

foreach name : svgs
  output = name.underscorify() + '_src.h'
  var = name.underscorify() + '_src'
  wlcom_sources += custom_target(
    output,
    command: [embed, var],
    input: name,
    output: output,
    feed: true,
    capture: true,
  )
endforeach

wlcom_sources += files(
  'config.c',
  'icon.c',
  'theme.c',
)
