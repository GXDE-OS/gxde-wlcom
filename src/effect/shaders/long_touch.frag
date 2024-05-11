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

// Inspired by https://www.shadertoy.com/view/Mtlyz7
#define PI 3.141592653589793

const float innerRadius = 0.7;
const float outerRadius = 1.0;
const float startAngle = 0.0;
const vec3 fgColor = vec3(0.0, 0.68, 1.0);
const vec3 bgColor = vec3(0.6);

void main() {
    vec2 centerPos = uv * 2.0 - 1.0;
    float dist = length(centerPos);
    float smoothThresh = fwidth(dist);

    float inner = smoothstep(innerRadius, innerRadius + smoothThresh, dist);
    float outer = smoothstep(outerRadius, outerRadius - smoothThresh, dist);
    float bandAlpha = inner * outer;

    float angle = (atan(centerPos.y, centerPos.x) + PI);
    float startEdge = smoothstep(angle, angle - smoothThresh * 1.0, startAngle);
    float endEdge = smoothstep(angle, angle + smoothThresh * 1.0, endAngle);
    float angleAlpha = startEdge * endEdge;

    vec4 bg = vec4(bgColor, bandAlpha);
    vec4 fg = vec4(fgColor, bandAlpha * angleAlpha);
    vec3 color = mix(bg.rgb, fg.rgb, fg.a);
    gl_FragColor = vec4(color, bg.a);
}
