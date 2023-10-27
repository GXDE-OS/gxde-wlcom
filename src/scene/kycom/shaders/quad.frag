#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform vec4 uv4_color;
varying vec2 _v2_texcoord;

void main() {
    gl_FragColor = uv4_color;
}
