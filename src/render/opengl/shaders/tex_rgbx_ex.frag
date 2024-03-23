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

uniform float pixelDistance; // 1px in distance
uniform float aspect; // width / height
uniform vec4 roundedCornerRadius;

float sdRoundedBox( in vec2 p, in vec2 b, in vec4 r )
{
    r.xy = (p.x>0.0)?r.xy : r.zw;
    r.x  = (p.y>0.0)?r.x  : r.y;
    vec2 q = abs(p)-b+r.x;
    return min(max(q.x,q.y),0.0) + length(max(q,0.0)) - r.x;
}

void main() {
	vec2 st = v_texcoord * 2.0 - 1.0;
    st.x *= aspect;
	vec2 fullSize = vec2(aspect, 1.0);

	float dist = sdRoundedBox(st, fullSize, roundedCornerRadius);
	float aa = pixelDistance * 0.5;
    float shape = 1.0 - smoothstep(-aa, 0.0, dist);
    vec4 texColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0);
	gl_FragColor = mix(vec4(0.0), texColor, shape);
}
