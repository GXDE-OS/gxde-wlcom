shaders = [
  'decoration.vert',
  'decoration.frag',
]

foreach name : shaders
  output = name.underscorify() + '.h'
  var = name.underscorify()
  wlcom_sources += custom_target(
    output,
    command: [embed, var],
    input: name,
    output: output,
    feed: true,
    capture: true,
  )
endforeach
