#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float alpha;

uniform int forceOpaque; // texture alpha force = 1
uniform float antiAliasing;
uniform float aspect; // width / height
uniform vec4 roundCornerRadius;
uniform float borderWidth;
uniform vec4 borderColor;

float sdRoundedBox(in vec2 p, in vec2 b, in vec4 r)
{
    r.xy = (p.x >0.0) ? r.xy : r.zw;
    r.x = (p.y > 0.0) ? r.x : r.y;
    vec2 q = abs(p) - b + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}

void main()
{
    vec2 st = v_texcoord * 2.0 - 1.0;
    st.x *= aspect;
    vec2 fullSize = vec2(aspect, 1.0);

    float dist = sdRoundedBox(st, fullSize, roundCornerRadius);
    float shape = 1.0 - smoothstep(0.0, antiAliasing, dist);
    vec4 texColor = texture2D(tex, v_texcoord) * alpha;
    if (forceOpaque != 0) {
        texColor.a = 1.0;
    }
    vec4 outColor = mix(vec4(0.0), texColor, shape);
    float borderCoverage = shape * smoothstep(-borderWidth - antiAliasing,
                                               -borderWidth + antiAliasing, dist);
    vec4 border = vec4(borderColor.rgb * borderColor.a, borderColor.a) * borderCoverage;
    gl_FragColor = border + outColor * (1.0 - border.a);
}
