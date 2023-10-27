uniform mat4 MVP;
attribute vec2 v2_pos;

attribute vec2 v2_texcoord;
varying vec2 _v2_texcoord;

void main() {
    gl_Position = MVP * vec4(v2_pos, 0.0, 1.0);
    _v2_texcoord = v2_texcoord;
}
