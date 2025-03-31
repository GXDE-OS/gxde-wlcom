shaders = [
  'blur_tex.vert',
  'blur_first_down.frag',
  'blur_first_down.vert',
  'blur_tex_rgba.frag',
  'blur.vert',
  'blur_up.frag',
  'blur_down.frag',
  'circle_progressbar.vert',
  'circle_progressbar.frag',
  'magic_lamp.vert',
  'magic_lamp.frag',
  'shape_color.vert',
  'shape_color.frag',
  'tap_ripple.vert',
  'tap_ripple.frag',
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
