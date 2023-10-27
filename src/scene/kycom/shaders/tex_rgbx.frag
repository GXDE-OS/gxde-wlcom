#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform sampler2D tex2D;
uniform vec2 uv2_base;
uniform vec2 uv2_scale;

vec4 get_pixel(vec2 uv) {
    uv = uv2_base + uv2_scale * uv;
    return vec4(texture2D(tex2D, uv).rgb, 1.0);
}
