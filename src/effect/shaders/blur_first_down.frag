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
uniform vec2 min_uv;
uniform vec2 max_uv;

void main()
{
    vec4 sum = texture2D(texture, uv) * 4.0;

    vec2 lb_uv = clamp(uv - halfpixel.xy * offset, min_uv, max_uv);
    sum += texture2D(texture, lb_uv);
    vec2 rt_uv = clamp(uv + halfpixel.xy * offset, min_uv, max_uv);
    sum += texture2D(texture, rt_uv);
    vec2 rb_uv = clamp(uv + vec2(halfpixel.x, -halfpixel.y) * offset, min_uv, max_uv);
    sum += texture2D(texture, rb_uv);
    vec2 lt_uv = clamp(uv - vec2(halfpixel.x, -halfpixel.y) * offset, min_uv, max_uv);
    sum += texture2D(texture, lt_uv);

    gl_FragColor = sum * 0.125;
}
