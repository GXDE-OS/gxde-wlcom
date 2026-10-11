svgs = [
  'base_light.svg',
  'base_dark.svg',
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
