@builtin_ext@
@builtin@

varying vec2 _v2_texcoord;
uniform vec4 uv4_color;

void main()
{
    vec4 v4_tex_color = get_pixel(_v2_texcoord);
    v4_tex_color.rgb = v4_tex_color.rgb * uv4_color.a;
    gl_FragColor = v4_tex_color * uv4_color;
}
