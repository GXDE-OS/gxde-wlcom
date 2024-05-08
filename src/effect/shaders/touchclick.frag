#ifdef GL_ES
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
    float r = radius;
    float l = length(uv - vec2(0.5, 0.5));
    float d = step(0.0, l - r);
    d = 1.0 - d;

    // attenuation
    if (d > 0.0) {
        gl_FragColor = vec4(d, d, d, 1.0 - (r - l + attenuation));
    }
    else {
        gl_FragColor = vec4(d);
    }
}
