#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform vec4 shadowRect; // pixel. left-top right-bottom
uniform float shadowSigma;
uniform vec4 shadowColor;

uniform float borderAA; // out border anti-aliasing
uniform float aspect; // width / height
uniform vec4 windowRect; // distance. x y w h
uniform vec4 roundedCornerRadius;

uniform float borderThickness;
uniform vec4 borderColor;

uniform float titleHeight;
uniform vec4 titleColor;

varying vec2 uv; // 0~1
varying vec2 pos; // pixel

// This approximates the error function, needed for the gaussian integral
vec4 erf(vec4 x) {
    vec4 s = sign(x), a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

// https://madebyevan.com/shaders/fast-rounded-rectangle-shadows/
// Return the mask for the shadow of a box from lower to upper
float boxShadow(vec2 lower, vec2 upper, vec2 point, float sigma) {
    vec4 query = vec4(point - lower, point - upper);
    vec4 integral = 0.5 + 0.5 * erf(query * (sqrt(0.5) / sigma));
    return (integral.z - integral.x) * (integral.w - integral.y);
}

float sdRoundedBox(in vec2 p, in vec2 b, in vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    vec2 q = abs(p) - b + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}

void main() {
    float shadow = boxShadow(shadowRect.xy, shadowRect.zw, pos, shadowSigma);

    vec2 st = uv * 2.0;
    st.x *= aspect;
    vec2 offset = -windowRect.xy * 2.0;

    float windowDist = sdRoundedBox(st + offset, windowRect.zw, roundedCornerRadius);
    // inner window without border
    float shapeWindow = smoothstep(0.0, borderAA, windowDist);

    // border
    float shapeWindowWithBorder = smoothstep(-borderAA, borderAA, windowDist + borderThickness);
    float shapeBorder = shapeWindowWithBorder - shapeWindow;

    // title
    float shapeTitle = 0.0;
    if (titleHeight > 0.001) {
        float titleDist = sdRoundedBox(
            st + offset + vec2(0.0, windowRect.w - titleHeight - borderThickness),
            vec2(windowRect.z - borderThickness, titleHeight),
            vec4(0.0, roundedCornerRadius[1] - borderThickness, 0.0, roundedCornerRadius[3] - borderThickness));
        shapeTitle = 1.0 - smoothstep(-borderAA, borderAA, titleDist);
    }

    // inner window remove shadow
    float shadow_mask = shapeWindowWithBorder + shapeTitle;

    // blend all color
    vec4 result = vec4(0.0); // todo from backgroud
    result = mix(result, shadowColor, shadow * shadow_mask);
    result = mix(result, titleColor, shapeTitle);
    result += shapeBorder * borderColor; // todo replace with mix

    gl_FragColor = result;
}
