#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

attribute vec2 position;
varying vec2 uv;
void main() 
{
    gl_Position = vec4(position, 0.0, 1.0);
    uv = (position.xy + vec2(1.0, 1.0)) * 0.5;
}
