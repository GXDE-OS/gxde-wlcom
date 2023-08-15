uniform samplerExternalOES tex2D;
uniform mediump vec2 uv2_base;
uniform mediump vec2 uv2_scale;

mediump vec4 get_pixel(highp vec2 uv) {
    uv = uv2_base + uv2_scale * uv;
    return texture2D(tex2D, uv);
}
