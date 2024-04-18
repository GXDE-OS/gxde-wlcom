#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform mat3 uv2ndc;
uniform mat3 uv2tex;

attribute vec2 inUV;

varying vec2 v_texcoord;

void main()
{
    vec3 pos3 = vec3(inUV, 1.0);
    gl_Position = vec4(uv2ndc * pos3, 1.0);
    v_texcoord = (uv2tex * pos3).xy;
}
