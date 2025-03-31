#ifdef GL_ES
#extension GL_OES_standard_derivatives : enable
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

varying vec2 uv;

uniform float endAngle;

#define PI 3.141592653589793

const float innerRadius = 0.7;
const float outerRadius = 1.0;
const vec3 fgColor = vec3(0.0, 0.68, 1.0);
const vec3 bgColor = vec3(0.6);

void main() {
    vec2 st = uv * 2.0 - 1.0;
    float dist = length(st);
    float aa = fwidth(dist);

    float outer = smoothstep(-aa, aa, outerRadius - dist);
    float inner = smoothstep(-aa, aa, dist - innerRadius);
    
    float angle = atan(st.y, st.x) + PI;
    float mask = smoothstep(-aa, aa, endAngle - angle);

    float bandAlpha = outer * inner;
    vec4 bg = vec4(bgColor, bandAlpha);
    vec4 fg = vec4(fgColor, bandAlpha * mask);
    vec3 color = mix(bg.rgb, fg.rgb, fg.a);
    gl_FragColor = vec4(color, bg.a);
}
