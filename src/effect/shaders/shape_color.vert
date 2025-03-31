#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform mat3 logic2ndc;

attribute vec2 position;

void main() {
    gl_Position = vec4(logic2ndc * vec3(position, 1.0), 1.0);
}
