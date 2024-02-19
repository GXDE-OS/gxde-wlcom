precision highp float;

uniform vec2 size;
uniform mat3 transform;

attribute vec2 position;

varying vec2 uv;
varying vec2 pos;

void main() {
    gl_Position = vec4(transform * vec3(position, 1.0), 1.0);
    pos = position;
    uv = position / size;
}
