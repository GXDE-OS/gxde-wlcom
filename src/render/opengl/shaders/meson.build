shaders = [
  'common.vert',
  'quad.frag',
  'tex_rgba.frag',
  'tex_rgbx.frag',
  'tex_external.frag',
  'quad_ex.frag',
  'tex_rgba_ex.frag',
  'tex_rgbx_ex.frag',
  'tex_external_ex.frag',
]

foreach name : shaders
  output = name.underscorify() + '_str.h'
  var = name.underscorify() + '_str'
  wlcom_sources += custom_target(
    output,
    command: [embed, var],
    input: name,
    output: output,
    feed: true,
    capture: true,
  )
endforeach
