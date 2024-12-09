#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform mat3 logic2ndc;

attribute vec2 in_position;
attribute vec2 in_texcoord;

varying vec2 v_texcoord;

void main() {
    gl_Position = vec4(logic2ndc * vec3(in_position, 1.0), 1.0);
    v_texcoord = in_texcoord;
}
