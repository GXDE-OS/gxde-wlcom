shaders = [
  'blur_tex.vert',
  'blur_tex_rgba.frag',
  'blur.vert',
  'blur_up.frag',
  'blur_down.frag',
  'touchclick.vert',
  'touchclick.frag',
  'touchtrail.vert',
  'touchtrail.frag',
  'long_touch.vert',
  'long_touch.frag',
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
