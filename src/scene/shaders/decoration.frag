#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif

uniform vec4 shadowRect; // pixel. left-top right-bottom
uniform float shadowSize;
uniform float shadowCornerRadius;
uniform float shadowOverlap;
uniform vec2 shadowOffset;
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

float sdRoundedBox(in vec2 p, in vec2 b, in vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    vec2 q = abs(p) - b + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}

float chameleonShadow(vec2 point) {
    if (shadowSize <= 0.1) {
        return 0.0;
    }

    vec2 center = (shadowRect.xy + shadowRect.zw) * 0.5;
    vec2 halfSize = (shadowRect.zw - shadowRect.xy) * 0.5;
    vec2 local = point - center;
    vec2 q = abs(local) - halfSize + shadowCornerRadius;
    vec2 outward = max(q, 0.0);
    float outwardLength = length(outward);
    float distance = min(max(q.x, q.y), 0.0) + outwardLength - shadowCornerRadius;

    // deepin-chameleon uses a ten-stop radial gradient whose alpha is
    // 0.6 * exp(-x^2 / 0.15).  Account for the part of that gradient which
    // overlaps the rounded window before evaluating the visible falloff.  Its
    // offset trims the negative sides; it does not move the shadow rectangle.
    // Start farther into the radial curve than the window radius alone.  If
    // the curve starts at the 8 px corner radius, its near-constant high-alpha
    // section becomes a solid horizontal bar below a wide window.
    float overlap = max(shadowOverlap, 3.0) + shadowSize * 0.15;
    vec2 direction = outwardLength > 0.001 ? outward / outwardLength : vec2(0.0);
    vec2 directionalOffset = vec2(local.x < 0.0 ? shadowOffset.x : 0.0,
                                  local.y < 0.0 ? shadowOffset.y : 0.0);
    // Offset changes the directional weight, but must not translate the dense
    // part of the shadow outside the window as a solid strip.
    float offset = dot(direction, directionalOffset) * 0.5;
    float x = (max(distance, 0.0) + overlap + offset) / shadowSize;
    return 0.6 * exp(-(x * x) / 0.15);
}

void main() {
    float shadow = chameleonShadow(pos);

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
    vec4 result = vec4(0.0); // todo from background
    result = mix(result, shadowColor, shadow * shadow_mask);
    result = mix(result, titleColor, shapeTitle);
    result += shapeBorder * borderColor; // todo replace with mix

    gl_FragColor = result;
}
