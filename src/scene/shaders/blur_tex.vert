#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform mat3 proj;
uniform mat3 tex_proj;
attribute vec2 sdfpos; // shape window part
attribute vec2 pos; //blur_tex
varying vec2 v_uv;
varying vec2 v_texcoord;

void main() {
    vec3 pos3 = vec3(pos, 1.0);
    gl_Position = vec4(pos3 * proj, 1.0);
    v_uv = (vec3(sdfpos, 1.0) * tex_proj).xy;
    v_texcoord = (pos3 * tex_proj).xy;
}
