#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

varying vec2 uv;
uniform float offset;
uniform sampler2D texture;
uniform vec2 halfpixel;
void main()
{
    vec4 sum = texture2D(texture, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(texture, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(texture, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
    sum += texture2D(texture, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(texture, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
    sum += texture2D(texture, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
    sum += texture2D(texture, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;

    gl_FragColor = sum * 0.0833333333;
}
