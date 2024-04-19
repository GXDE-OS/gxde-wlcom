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
    vec4 sum = texture2D(texture, uv) * 4.0;

    sum += texture2D(texture, uv - halfpixel.xy * offset);
    sum += texture2D(texture, uv + halfpixel.xy * offset);
    sum += texture2D(texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset);
    sum += texture2D(texture, uv - vec2(halfpixel.x, -halfpixel.y) * offset);

    gl_FragColor = sum * 0.125;
}
