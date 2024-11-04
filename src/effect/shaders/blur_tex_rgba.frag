#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

varying vec2 v_uv;
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float alpha;

uniform float antiAliasing;
uniform float aspect; // width / height
uniform vec4 roundedCornerRadius;

uniform float offset;
uniform vec2 halfpixel;

vec4 get_pixel(vec2 uv)
{
    vec4 sum = texture2D(tex, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(tex, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(tex, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
    sum += texture2D(tex, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(tex, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(tex, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
    sum += texture2D(tex, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
    sum += texture2D(tex, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;

    return sum * 0.0833333333;
}

float sdRoundedBox(in vec2 p, in vec2 b, in vec4 r)
{
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x = (p.y > 0.0) ? r.x : r.y;
    vec2 q = abs(p) - b + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}

void main()
{
    vec2 st = v_uv * 2.0 - 1.0;
    st.x *= aspect;
    vec2 fullSize = vec2(aspect, 1.0);

    float dist = sdRoundedBox(st, fullSize, roundedCornerRadius);
    float shape = 1.0 - smoothstep(0.0, antiAliasing, dist);
    vec4 texColor = get_pixel(v_texcoord) * alpha;
    gl_FragColor = mix(vec4(0.0), texColor, shape);
}
