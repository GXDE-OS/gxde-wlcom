#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform mat3 uv2ndc;
uniform mat3 outputInvert;

attribute vec2 inUV;

varying vec2 uv;

void main() {
    gl_Position = vec4(uv2ndc * vec3(inUV, 1.0), 1.0);
    uv = (outputInvert * vec3(inUV, 1.0)).xy;
}
