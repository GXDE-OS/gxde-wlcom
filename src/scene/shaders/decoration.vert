precision highp float;

uniform vec2 size;
uniform mat3 uv2ndc;
uniform mat3 inverseTransform;

attribute vec2 inUV;

varying vec2 uv;
varying vec2 pos;

void main() {
    gl_Position = vec4(uv2ndc * vec3(inUV, 1.0), 1.0);
    uv = (inverseTransform * vec3(inUV, 1.0)).xy;
    pos = uv * size;
}
