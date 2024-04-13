shaders = [
  'decoration.vert',
  'decoration.frag',
  'blur_tex_rgba.frag',
  'blur_tex.vert',
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
