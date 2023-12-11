// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <pixman.h>
#include <stdlib.h>

#include "kywc/kycom/opengl.h"
#include "kywc/kycom/scene.h"
#include "kywc/kycom/transform.h"
#include "kywc/log.h"
#include "kywc/plugin.h"

#include "kywc/kycom/effect_view.h"
#include "kywc/kycom/effects.h"

struct blur_conf {
    int iterations;
    float offset;
    pixman_region32_t kde_blur_region;

    struct kywc_gl_buffer fb[2];
    struct kywc_gl_buffer *blur_fb_final;

    /* Blur node */
    struct kywc_texture_node *surface_node;
    struct kywc_transform_root_node *surface_transform_root;
    struct wl_listener handle_blur_commit;
    struct wl_listener handle_surface_node_destroy;
};

struct blur {
    bool is_listening;
    int effect_z_order;
    struct wl_listener handle_view_new;
    struct wl_listener handle_kde_blur_create;
    struct wl_listener handle_kde_blur_destroy;

    struct blur_conf default_conf;
};

struct _kywc_effect_blur_node {
    struct kywc_group_node node;
    struct blur_conf *conf;

    const char *name;
};

struct _kywc_blur_node_render_instance {
    struct kywc_render_instance base;
    struct _kywc_effect_blur_node *node;
    struct kywc_gl_program *gl_program;
    /* Surface local logic coord */
    pixman_region32_t blur_region;
    /* Frame_buffer coord */
    pixman_region32_t cpy_back_damage;
    /* Surface local logic coord*/
    pixman_region32_t corner_region;

    struct kywc_gl_buffer save_fb;
    struct wlr_box save_box;
    struct blur_conf *conf;
};

static void effect_blur_generate_render_task(const struct kywc_node *node,
                                             pixman_region32_t *damage,
                                             struct kywc_render_target *target,
                                             struct wl_list *render_tasks);

static int calculate_blur_radius(struct blur_conf *conf);

const char *blur_vertex_source = "attribute vec2 position;"
                                 "uniform mat4 matrix;"
                                 "varying vec2 fposition;"
                                 "void main() {"
                                 "    gl_Position = vec4(position, 0.0, 1.0);"
                                 "    fposition = position;"
                                 "}";

const char *up_frag_source =
    "#ifdef GL_ES\r\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\r\n"
    "precision highp float;\r\n"
    "#else\r\n"
    "precision mediump float;\r\n"
    "#endif\r\n"
    "#endif\r\n"
    "varying vec2 fposition;\r\n"
    "uniform float offset;\r\n"
    "uniform sampler2D texture;\r\n"
    "uniform vec2 halfpixel;\r\n"
    "void main()\r\n"
    "{\r\n"
    "    vec2 uv = (fposition.xy + vec2(1.0, 1.0)) / 2.0;"
    "    vec4 sum = texture2D(texture, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);"
    "    sum += texture2D(texture, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;"
    "    sum += texture2D(texture, uv + vec2(0.0, halfpixel.y * 2.0) * offset);"
    "    sum += texture2D(texture, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;"
    "    sum += texture2D(texture, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);"
    "    sum += texture2D(texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;"
    "    sum += texture2D(texture, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);"
    "    sum += texture2D(texture, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;"

    "    gl_FragColor = sum / 12.0;"
    "}";

const char *down_frag_source =
    "#ifdef GL_ES\r\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\r\n"
    "precision highp float;\r\n"
    "#else\r\n"
    "precision mediump float;\r\n"
    "#endif\r\n"
    "#endif\r\n"
    "varying vec2 fposition;\r\n"
    "uniform float offset;\r\n"
    "uniform sampler2D texture;\r\n"
    "uniform vec2 halfpixel;\r\n"
    "void main()"
    "{"
    "    vec2 uv = (fposition.xy + vec2(1.0, 1.0)) / 2.0;"
    "    vec4 sum = texture2D(texture, uv) * 4.0;"

    "    sum += texture2D(texture, uv - halfpixel.xy * offset);"
    "    sum += texture2D(texture, uv + halfpixel.xy * offset);"
    "    sum += texture2D(texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset);"
    "    sum += texture2D(texture, uv - vec2(halfpixel.x, -halfpixel.y) * offset);"

    "    gl_FragColor = sum / 8.0;"
    "}";

static const char *effect_blur_class = "blur";
static struct kywc_gl_program blur_porgram[2];
static struct blur blur = {
    .default_conf.iterations = 2,
    .default_conf.offset = 4.0,
    .effect_z_order = 100,
    .is_listening = false,
};

/********************Blur_conf*****************************/
static void blur_conf_destroy(struct blur_conf *conf)
{
    if (!conf) {
        return;
    }
    pixman_region32_fini(&conf->kde_blur_region);
    kywc_gl_begin();

    kywc_gl_buffer_release(&conf->fb[0]);
    kywc_gl_buffer_release(&conf->fb[1]);

    kywc_gl_end();
    free(conf);
}

static struct blur_conf *blur_default_conf_create(void)
{
    struct blur_conf *conf = malloc(sizeof(*conf));
    if (!conf) {
        return NULL;
    }
    struct blur_conf *def_conf = &blur.default_conf;
    conf->iterations = def_conf->iterations;
    conf->offset = def_conf->offset;
    pixman_region32_init(&conf->kde_blur_region);

    kywc_gl_buffer_init(&conf->fb[0]);
    kywc_gl_buffer_init(&conf->fb[1]);

    conf->surface_node = NULL;
    conf->surface_transform_root = NULL;

    return conf;
}

/********************Blur_node*****************************/
static const char *effect_blur_name(void)
{
    return "kywc_blur_effect_node";
}

static struct _kywc_effect_blur_node *as_effect_blur_node(const struct kywc_node *node)
{
    if (!node) {
        return NULL;
    }
    if (node->node_name == effect_blur_name) {
        return (struct _kywc_effect_blur_node *)node;
    }
    return NULL;
}

static int effect_blur_node_radius(const struct kywc_node *node)
{
    struct _kywc_effect_blur_node *blur_node = as_effect_blur_node(node);
    if (!blur_node) {
        return 0;
    }

    return calculate_blur_radius(blur_node->conf);
}

static void effect_blur_init(struct _kywc_effect_blur_node *blur_node, struct blur_conf *blur)
{
    if (!blur_node || !blur) {
        return;
    }
    kywc_group_node_init(&blur_node->node);
    kywc_group_node_set_generate_func(&blur_node->node, effect_blur_generate_render_task);

    blur_node->name = effect_blur_class;
    blur_node->conf = blur;
    blur_node->node.node.node_name = effect_blur_name;
}

static struct kywc_gl_program *effect_blur_get_gl_program(void)
{
    if (!kywc_gl_program_is_compiled(&blur_porgram[0])) {
        kywc_gl_program_compile(&blur_porgram[0], blur_vertex_source, down_frag_source);
    }

    if (!kywc_gl_program_is_compiled(&blur_porgram[1])) {
        kywc_gl_program_compile(&blur_porgram[1], blur_vertex_source, up_frag_source);
    }

    if (kywc_gl_program_is_compiled(&blur_porgram[0]) &&
        kywc_gl_program_is_compiled(&blur_porgram[1])) {
        return blur_porgram;
    }

    kywc_log(KYWC_INFO, "Blur shader compile error!");
    return NULL;
}

static struct _kywc_effect_blur_node *effect_blur_node_create(struct blur_conf *blur,
                                                              struct kywc_effect_view *view,
                                                              struct kywc_texture_node *node)
{
    struct _kywc_effect_blur_node *rc_node = malloc(sizeof(*rc_node));
    if (!rc_node) {
        return NULL;
    }
    if (!effect_blur_get_gl_program()) {
        free(rc_node);
        return NULL;
    }

    effect_blur_init(rc_node, blur);
    return rc_node;
}

static void effect_blur_node_destroy(struct _kywc_effect_blur_node *node)
{
    free(node);
}

/********************Blur_node_render_instance*****************************/
static void blur_render(struct kywc_render_instance *instance, struct kywc_render_target *target,
                        pixman_region32_t *damage);

const struct kywc_render_instance_interface blur_node_render_instance_impl = {
    .render = blur_render,
    .compute_visible = NULL,
    .try_direct_scanout = NULL,
};

static void save_backgroud_extent(struct _kywc_blur_node_render_instance *node_render,
                                  pixman_region32_t *cpy_back_damage,
                                  struct kywc_render_target *target)
{
    pixman_box32_t *cpy_box = pixman_region32_extents(cpy_back_damage);
    node_render->save_box.x = cpy_box->x1;
    node_render->save_box.y = cpy_box->y1;
    node_render->save_box.width = cpy_box->x2 - cpy_box->x1;
    node_render->save_box.height = cpy_box->y2 - cpy_box->y1;
    if (!node_render->save_box.width || !node_render->save_box.height) {
        return;
    }
    kywc_target_render_begin(target);
    kywc_gl_buffer_allocate(&node_render->save_fb, kywc_gl_get_current_framebuffer(),
                            node_render->save_box.width, node_render->save_box.height);
    kywc_target_cpy_framebuffer_box(&node_render->save_fb, target, &node_render->save_box);
    kywc_target_render_end(target);
}

static void blur_node_render_instance_destroy(struct wl_listener *listener, void *data)
{
    struct kywc_render_instance *render_instance =
        wl_container_of(listener, render_instance, destroy_listener);

    struct _kywc_blur_node_render_instance *blur_render =
        wl_container_of(render_instance, blur_render, base);

    kywc_gl_buffer_release(&blur_render->save_fb);
    pixman_region32_fini(&blur_render->blur_region);
    pixman_region32_fini(&blur_render->cpy_back_damage);
    pixman_region32_fini(&blur_render->corner_region);
    kywc_render_instance_handle_destroy(listener, data);
}

static struct _kywc_blur_node_render_instance *
blur_node_render_instance_create(struct _kywc_effect_blur_node *blur_node,
                                 pixman_region32_t *translucent, pixman_region32_t *cpy_back_damage,
                                 pixman_region32_t *corner_region,
                                 struct kywc_render_target *target)
{
    struct _kywc_blur_node_render_instance *blur_render = malloc(sizeof(*blur_render));
    if (!blur_render) {
        return NULL;
    }
    struct kywc_gl_program *program = effect_blur_get_gl_program();
    if (!program) {
        free(blur_render);
        return NULL;
    }

    blur_render->gl_program = program;
    blur_render->node = blur_node;
    blur_render->conf = blur_node->conf;
    kywc_gl_buffer_init(&blur_render->save_fb);
    save_backgroud_extent(blur_render, cpy_back_damage, target);

    pixman_region32_init(&blur_render->cpy_back_damage);
    pixman_region32_copy(&blur_render->cpy_back_damage, cpy_back_damage);
    pixman_region32_init(&blur_render->blur_region);
    pixman_region32_copy(&blur_render->blur_region, translucent);
    pixman_region32_init(&blur_render->corner_region);
    pixman_region32_copy(&blur_render->corner_region, corner_region);

    kywc_render_instance_init(&blur_render->base, &blur_node_render_instance_impl,
                              blur_node_render_instance_destroy);
    return blur_render;
}

static int calculate_blur_radius(struct blur_conf *conf)
{
    if (!conf) {
        return 0;
    }
    return pow(2, conf->iterations + 1) * conf->offset;
}

static void render_iteration(pixman_region32_t *blur_damage, struct kywc_gl_buffer *in,
                             struct kywc_gl_buffer *out, int width, int height)
{
    /* Special case for small regions where we can't really blur, because we */
    /* simply have too few pixels */
    width = width < 1 ? 1 : width;
    height = height < 1 ? 1 : height;

    int ofb = kywc_gl_get_current_framebuffer();
    kywc_gl_buffer_allocate(out, ofb, width, height);
    kywc_gl_buffer_bind(out);
    glDisable(GL_BLEND);
    vec4 color = { 0.f, 0.f, 0.f, 0.f };
    kywc_gl_clear(color);
    kywc_gl_buffer_draw_region(in, blur_damage);
}

static void blur_fb0(struct blur_conf *conf, struct kywc_gl_program *tex_blur_program,
                     pixman_region32_t *damage, int width, int height)
{
    int iterations = conf->iterations;
    float offset = conf->offset;
    int sampleWidth, sampleHeight;
    GLfloat pos_vertex[8] = { -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f };

    struct kywc_gl_buffer *fb = conf->fb;
    kywc_gl_begin();
    glDisable(GL_BLEND);

    kywc_gl_program_use(&tex_blur_program[0], TEXTURE_TYPE_RGBA);
    kywc_gl_buffer_bind(&fb[0]);
    kywc_gl_program_attrib_pointer(&tex_blur_program[0], "position", 2, 0, pos_vertex, GL_FLOAT);
    kywc_gl_program_uniform1f(&tex_blur_program[0], "offset", offset);
    pixman_region32_t region;
    pixman_region32_init(&region);
    for (int i = 0; i < iterations; i++) {
        sampleWidth = width / (1 << i);
        sampleHeight = height / (1 << i);

        kywc_region_scale(&region, damage, (1.0 / (1 << i)));
        kywc_gl_program_uniform2f(&tex_blur_program[0], "halfpixel", 0.5f / sampleWidth,
                                  0.5f / sampleHeight);
        render_iteration(&region, &fb[i % 2], &fb[1 - i % 2], sampleWidth, sampleHeight);
        conf->blur_fb_final = &fb[1 - i % 2];
    }
    kywc_gl_buffer_unbind(0);
    kywc_gl_program_deactive(&tex_blur_program[0]);

    kywc_gl_program_use(&tex_blur_program[1], TEXTURE_TYPE_RGBA);
    kywc_gl_program_attrib_pointer(&tex_blur_program[1], "position", 2, 0, pos_vertex, GL_FLOAT);
    kywc_gl_program_uniform1f(&tex_blur_program[1], "offset", offset);
    for (int i = iterations - 1; i >= 0; i--) {
        sampleWidth = width / (1 << i);
        sampleHeight = height / (1 << i);

        kywc_region_scale(&region, damage, (1.0 / (1 << i)));
        kywc_gl_program_uniform2f(&tex_blur_program[0], "halfpixel", 0.5f / sampleWidth,
                                  0.5f / sampleHeight);
        render_iteration(&region, &fb[1 - i % 2], &fb[i % 2], sampleWidth, sampleHeight);
        conf->blur_fb_final = &fb[i % 2];
    }
    pixman_region32_fini(&region);
    kywc_gl_buffer_unbind(0);
    kywc_gl_program_deactive(&tex_blur_program[1]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, 0);
    kywc_gl_end();
}

static void render_cpy_region_back_ofb(struct kywc_gl_buffer *buffer,
                                       struct kywc_render_target *target,
                                       pixman_region32_t *cpy_region, struct wlr_box *cpy_boundbox)
{
    struct wlr_box cpy_buffer;
    struct wlr_box dst_fb_box;

    int nrects = pixman_region32_n_rects(cpy_region);
    /* Scene buffer local coordinate(buffer in scene, not the cpy buffer) */
    pixman_box32_t *logic_rect = pixman_region32_rectangles(cpy_region, &nrects);

    for (int i = 0; i < nrects; i++) {
        dst_fb_box.x = logic_rect[i].x1;
        dst_fb_box.y = logic_rect[i].y1;
        dst_fb_box.width = logic_rect[i].x2 - logic_rect[i].x1;
        dst_fb_box.height = logic_rect[i].y2 - logic_rect[i].y1;

        cpy_buffer.x = dst_fb_box.x - cpy_boundbox->x;
        cpy_buffer.y = dst_fb_box.y - cpy_boundbox->y;
        cpy_buffer.width = dst_fb_box.width;
        cpy_buffer.height = dst_fb_box.height;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, buffer->fb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->buffer.fb);
        glBlitFramebuffer(cpy_buffer.x, cpy_buffer.y, cpy_buffer.x + cpy_buffer.width,
                          cpy_buffer.y + cpy_buffer.height, dst_fb_box.x, dst_fb_box.y,
                          dst_fb_box.x + dst_fb_box.width, dst_fb_box.y + dst_fb_box.height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    kywc_target_render_end(target);
}

static void copy_blur_region_backgroud(struct kywc_gl_buffer *dst, struct kywc_render_target *src,
                                       pixman_region32_t *cpy_area,
                                       struct wlr_box *framebuffer_cpybox)
{
    if (!pixman_region32_not_empty(cpy_area)) {
        return;
    }
    pixman_box32_t *cpy_box = pixman_region32_extents(cpy_area);
    struct wlr_box cpy_geometry = {
        .x = cpy_box->x1,
        .y = cpy_box->y1,
        .width = cpy_box->x2 - cpy_box->x1,
        .height = cpy_box->y2 - cpy_box->y1,
    };

    struct wlr_box cpy;
    kywc_target_get_framebuffer_box(src, &cpy_geometry, &cpy);
    framebuffer_cpybox->x = cpy.x;
    framebuffer_cpybox->y = cpy.y;
    framebuffer_cpybox->width = cpy.width;
    framebuffer_cpybox->height = cpy.height;

    kywc_gl_begin();
    kywc_gl_buffer_allocate(dst, src->current_ofb, cpy.width, cpy.height);
    kywc_target_cpy_framebuffer_box(dst, src, &cpy);
    kywc_gl_end();
}

static void blur_render(struct kywc_render_instance *instance, struct kywc_render_target *target,
                        pixman_region32_t *damage)
{
    struct _kywc_blur_node_render_instance *render = wl_container_of(instance, render, base);
    if (!render->gl_program) {
        return;
    }

    kywc_target_render_begin(target);
    struct wlr_box view_box = {
        .x = target->view_box.x,
        .y = target->view_box.y,
        .width = floor(target->view_box.width),
        .height = floor(target->view_box.height),
    };
    struct wlr_box src_box;
    kywc_target_get_framebuffer_box(target, &view_box, &src_box);

    struct blur_conf *configure = render->conf;
    struct kywc_gl_buffer *background = &configure->fb[0];
    /* Translucent pos in frame_buffer. */
    struct wlr_box cpy_framebufer_box = { 0 };
    copy_blur_region_backgroud(background, target, &render->blur_region, &cpy_framebufer_box);
    /* Blur region pos in buffer, logic coord. */
    pixman_box32_t *cpy_box = pixman_region32_extents(&render->blur_region);

    pixman_region32_t blur_region_on_cpy, extern_blur_region;
    pixman_region32_init(&blur_region_on_cpy);
    pixman_region32_init(&extern_blur_region);
    /* Region in frame buffer; 0,0 is buffer topleft. */
    kywc_target_get_frame_region(target, damage, &blur_region_on_cpy);
    /* Offset in cpy bufer; 0,0 is cpy buffer topleft */
    pixman_region32_translate(&blur_region_on_cpy, -cpy_framebufer_box.x, -cpy_framebufer_box.y);

    configure->blur_fb_final = NULL;
    blur_fb0(configure, render->gl_program, &blur_region_on_cpy, background->width,
             background->height);
    pixman_region32_fini(&blur_region_on_cpy);
    pixman_region32_fini(&extern_blur_region);

    struct kywc_gl_texture *tex = configure->fb[1].fb_tex;
    if (configure->blur_fb_final) {
        tex = configure->blur_fb_final->fb_tex;
    }
    if (!tex) {
        return;
    }
    struct kywc_gl_geometry geometry = {
        .x1 = cpy_box->x1,
        .y1 = cpy_box->y1,
        .x2 = cpy_box->x2,
        .y2 = cpy_box->y2,
    };
    vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    kywc_target_render_begin(target);
    kywc_target_render_texture_with_transform(tex, (enum kywc_gl_transform)target->wl_transform,
                                              target, &geometry, color, RENDER_FLAG_CACHED);

    pixman_region32_subtract(damage, damage, &render->corner_region);

    kywc_target_draw_damage(target, tex, damage);
    kywc_gl_render_texture_clear_cached();
    kywc_target_render_end(target);

    if (render->save_fb.fb == 0 || render->save_fb.fb == (GLuint)(-1)) {
        return;
    }
    render_cpy_region_back_ofb(&render->save_fb, target, &render->cpy_back_damage,
                               &render->save_box);
}

/********************generate_render_task*******************************/
static void get_blur_region(pixman_region32_t *kde_blur_region, pixman_region32_t *blur_region,
                            struct _kywc_effect_blur_node *blur_node, struct wlr_box *bound_box)
{
    if (!pixman_region32_not_empty(kde_blur_region)) {
        pixman_region32_init_rect(blur_region, bound_box->x, bound_box->y, bound_box->width,
                                  bound_box->height);
    } else {
        pixman_region32_copy(blur_region, kde_blur_region);
    }
}

static void effect_blur_generate_render_task(const struct kywc_node *node,
                                             pixman_region32_t *damage,
                                             struct kywc_render_target *target,
                                             struct wl_list *render_tasks)
{
    struct _kywc_effect_blur_node *blur_node = wl_container_of(node, blur_node, node);
    if (!render_tasks || !damage) {
        return;
    }

    /**
     * Calc child node damage, only blur damage region.
     * Maybe child node sub the opaque region from damage,
     * so calc child first.
     */
    struct wlr_box bound_box;
    node->get_bounding_box(node, &bound_box);
    /* local _damage. */
    pixman_region32_t node_damage, node_corner, node_blur_region;
    pixman_region32_init(&node_damage);
    pixman_region32_init(&node_corner);
    pixman_region32_init(&node_blur_region);
    pixman_region32_intersect_rect(&node_damage, damage, bound_box.x, bound_box.y, bound_box.width,
                                   bound_box.height);

    /* Texture_node hasn't child. */
    /***********/
    struct kywc_group_node *group = &blur_node->node;
    kywc_group_node_children_generate_render(group, damage, target, render_tasks);
    /***********/

    if (!pixman_region32_not_empty(&node_damage)) {
        goto final;
    }

    struct blur_conf *conf = blur_node->conf;
    if (!conf) {
        kywc_log(KYWC_INFO, "conf is NULL.");
        return;
    }
    get_blur_region(&conf->kde_blur_region, &node_blur_region, blur_node, &bound_box);

    /* Update the node translucent background. */
    int r = effect_blur_node_radius(node);
    r = r / target->scale;
    pixman_region32_t extern_damage;
    pixman_region32_init(&extern_damage);
    kywc_region_adjust(&extern_damage, &node_damage, r, r, r, r);

    pixman_region32_intersect_rect(&extern_damage, &extern_damage, bound_box.x, bound_box.y,
                                   bound_box.width, bound_box.height);
    /* Need the out logic region*/
    pixman_region32_intersect_rect(&extern_damage, &extern_damage, target->view_box.x,
                                   target->view_box.y, target->view_box.width,
                                   target->view_box.height);

    /* Add extern damage to the backend(down) node, backed node repaint the region. */
    pixman_region32_union(damage, damage, &extern_damage);

    /* Node_damge & node_translucent, only blur damage translucent rengion*/
    pixman_region32_intersect(&node_blur_region, &node_blur_region, &extern_damage);
    if (!pixman_region32_not_empty(&node_blur_region)) {
        pixman_region32_fini(&extern_damage);
        goto final;
    }

    /* Generate render instance */
    pixman_region32_t cpy_back_damage, node_damge_frame;
    pixman_region32_init(&cpy_back_damage);
    pixman_region32_init(&node_damge_frame);
    kywc_target_get_frame_region(target, &extern_damage, &cpy_back_damage);
    kywc_target_get_frame_region(target, &node_damage, &node_damge_frame);

    pixman_region32_subtract(&cpy_back_damage, &cpy_back_damage, &node_damge_frame);

    /* Corner region doesn't render blur texture*/
    pixman_region32_copy(&node_corner, &conf->surface_node->corner_region);

    int tex_lx, tex_ly;
    kywc_node_coords(&conf->surface_node->node, &tex_lx, &tex_ly);
    pixman_region32_translate(&node_corner, tex_lx - target->lx, tex_ly - target->ly);

    struct _kywc_blur_node_render_instance *node_render = blur_node_render_instance_create(
        blur_node, &node_blur_region, &cpy_back_damage, &node_corner, target);

    struct kywc_render_task *task =
        kywc_render_task_create(&node_render->base, &extern_damage, target);

    wl_list_insert(render_tasks, &task->link);

    pixman_region32_fini(&node_damge_frame);
    pixman_region32_fini(&cpy_back_damage);
    pixman_region32_fini(&extern_damage);
final:
    pixman_region32_fini(&node_damage);
    pixman_region32_fini(&node_corner);
    pixman_region32_fini(&node_blur_region);
}

/*****************************************************************************/
static void handle_kde_blur_commit(struct wl_listener *listener, void *data)
{
    struct blur_conf *conf = wl_container_of(listener, conf, handle_blur_commit);
    struct kde_blur *blur = data;

    pixman_region32_copy(&conf->kde_blur_region, &blur->region);
    conf->offset = ((int32_t)blur->strength == -1) ? 4.0 : blur->strength * 1.0 / 1000;
}

static struct blur_conf *texture_node_remove_blur_node(struct kywc_texture_node *surface_node)
{
    if (!surface_node) {
        return NULL;
    }
    struct _kywc_effect_blur_node *transform_node =
        (struct _kywc_effect_blur_node *)kywc_node_transform_remove(&surface_node->node,
                                                                    effect_blur_class);
    if (!transform_node) {
        return NULL;
    }
    struct blur_conf *conf = transform_node->conf;

    effect_blur_node_destroy(transform_node);

    kywc_transform_root_destory(conf->surface_transform_root);

    return conf;
}

static struct blur_conf *kde_blur_remove_blur_node(struct kde_blur *_kde_blur)
{
    struct kywc_texture_node *surface_node = kywc_blur_get_texture_node(_kde_blur);
    return texture_node_remove_blur_node(surface_node);
}

static void handle_surface_node_destroy(struct wl_listener *listener, void *data)
{
    struct blur_conf *conf = wl_container_of(listener, conf, handle_surface_node_destroy);
    /* Maybe freed. */
    wl_list_remove(&conf->handle_blur_commit.link);
    wl_list_remove(&conf->handle_surface_node_destroy.link);

    blur_conf_destroy(conf);
}

static bool kde_blur_add_blur_node(struct blur_conf *conf, struct kde_blur *_kde_blur)
{
    if (!conf || !_kde_blur) {
        return false;
    }

    struct kywc_texture_node *surface_node = kywc_blur_get_texture_node(_kde_blur);
    if (!surface_node) {
        return false;
    }
    conf->surface_transform_root = kywc_transform_root_create(&surface_node->node);
    if (!conf->surface_transform_root) {
        kywc_log(KYWC_INFO, "%s create tranform root error: %p !", surface_node->node.node_name(),
                 surface_node);
        return false;
    }
    conf->surface_node = surface_node;

    if (kywc_node_transform_get(&surface_node->node, effect_blur_class)) {
        return false;
    }

    struct _kywc_effect_blur_node *transform_node =
        effect_blur_node_create(conf, NULL, surface_node);
    if (!transform_node) {
        return false;
    }

    bool ret = kywc_node_transform_add(&surface_node->node, &transform_node->node,
                                       blur.effect_z_order, effect_blur_class);
    if (!ret) {
        kywc_log(KYWC_INFO, "Add blur transform failed.");
        effect_blur_node_destroy(transform_node);
        return false;
    }
    conf->handle_surface_node_destroy.notify = handle_surface_node_destroy;
    wl_signal_add(&conf->surface_node->node.events.destroy, &conf->handle_surface_node_destroy);
    return true;
}

static void handle_kde_blur_create(struct wl_listener *listener, void *data)
{
    struct kde_blur *_kde_blur = data;
    struct blur_conf *conf = blur_default_conf_create();
    if (!conf) {
        kywc_log(KYWC_INFO, "Create blur conf error.");
        return;
    }
    if (!kde_blur_add_blur_node(conf, _kde_blur)) {
        blur_conf_destroy(conf);
        return;
    }

    conf->handle_blur_commit.notify = handle_kde_blur_commit;
    wl_signal_add(&_kde_blur->events.commit, &conf->handle_blur_commit);
}

static void handle_kde_blur_destroy(struct wl_listener *listener, void *data)
{
    struct kde_blur *_kde_blur = data;
    struct blur_conf *conf = kde_blur_remove_blur_node(_kde_blur);
    if (!conf) {
        return;
    }

    wl_list_remove(&conf->handle_blur_commit.link);
    wl_list_remove(&conf->handle_surface_node_destroy.link);

    blur_conf_destroy(conf);
}

static bool blur_plugin_init(void *plugin, void **teardown_data)
{
    struct kywc_effect_server *server = kywc_effect_server();
    if (!server || !kywc_renderer_is_opengl(server)) {
        kywc_log(KYWC_ERROR, "Blur_plugin_init failed");
        return false;
    }
    if (!blur.is_listening) {
        blur.is_listening = true;
        blur.handle_kde_blur_create.notify = handle_kde_blur_create;
        kywc_surface_add_blur_listener(&blur.handle_kde_blur_create);

        blur.handle_kde_blur_destroy.notify = handle_kde_blur_destroy;
        kywc_surface_add_blur_destroy_listener(&blur.handle_kde_blur_destroy);
    }

    return true;
}

static void blur_plugin_teardown(void *teardown_data)
{
    kywc_gl_program_delete(&blur_porgram[0]);
    kywc_gl_program_delete(&blur_porgram[1]);
    wl_list_remove(&blur.handle_kde_blur_create.link);
    wl_list_remove(&blur.handle_kde_blur_destroy.link);
    blur.is_listening = false;
}

static struct kywc_plugin_info info = {
    .name = "kywc_blur_effect.so",
    .abi_version = 1,
    .version = 1,
    .class = "blur",
};

struct kywc_plugin_data libkywc_blur_effect_plugin_data = {
    .info = &info,
    .options = NULL,
    .option = NULL,
    .setup = blur_plugin_init,
    .teardown = blur_plugin_teardown,
};
