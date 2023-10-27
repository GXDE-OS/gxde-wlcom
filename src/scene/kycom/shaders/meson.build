shaders = [
	'default_common.vert',
	'default_tex.frag',
	'quad.frag',
	'tex_external.frag',
	'tex_rgba.frag',
	'tex_rgbx.frag',
]

foreach name : shaders
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
