#ifdef GL_ES
#extension GL_OES_standard_derivatives : enable
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

varying vec2 uv;

uniform float radius;
uniform float attenuation;

void main() {
    float dist = distance(uv, vec2(0.5, 0.5));
    float aa = fwidth(dist);
    float dist2 = smoothstep(-aa, aa, radius - dist);

    float alpha = radius - dist;
    alpha = 1.0 - alpha - attenuation;
    alpha *= dist2;
    gl_FragColor = vec4(vec3(dist2), alpha);
}
