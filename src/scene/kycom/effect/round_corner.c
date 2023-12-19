// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include "kywc/kycom/opengl.h"
#include "kywc/kycom/scene.h"
#include "kywc/kycom/texture.h"
#include "kywc/log.h"
#include "kywc/plugin.h"

#include "kywc/kycom/effect_addon.h"
#include "kywc/kycom/effect_view.h"

enum round_corner_type {
    TOP_LEFT_ROUND_CORNER = 1 << 0,
    TOP_RIGHT_ROUND_CORNER = 1 << 1,
    BOTTOM_RIGHT_ROUND_CORNER = 1 << 2,
    BOTTOM_LEFT_ROUND_CORNER = 1 << 3,
    ALL_ROUND_CORNER = 15,
};

struct round_corner_conf {
    struct wlr_addon conf_addon;
    /* radius (pixel). */
    float radius;
    int border_width;
    float active_border_color[4];
    float inactive_border_color[4];
};

struct round_corner_data {
    struct kywc_effect_view *view;
    enum round_corner_type type;
    struct round_corner_conf *conf;
    int border_width;
    float border_color[4];

    struct wlr_addon data_addon;
    struct wl_listener handle_view_premap;
    struct wl_listener handle_view_map;
    struct wl_listener handle_view_unmap;
    struct wl_listener handle_view_destroy;
    struct wl_listener handle_view_activate;
    struct wl_listener handle_view_decoration;
};

struct round_corner {
    bool is_listening;
    int effect_z_order;
    struct wl_listener handle_view_new;
    struct wl_listener handle_theme_update;

    struct round_corner_conf default_conf;
    struct round_corner_conf conf;
};

struct effect_round_corner_node {
    struct kywc_group_node node;
    struct round_corner_data *data;

    struct kywc_group_node *view_node;
    struct kywc_texture_node *main_surface_node;
    const char *name;
    // texture-local(surface-local) coordinates
    pixman_region32_t opaque_region;
};

struct round_corner_node_render_instance {
    struct kywc_render_instance base;
    struct effect_round_corner_node *node;
};

struct texture_node_round_corner_render_instance {
    struct kywc_render_instance base;
    struct round_corner_data *round_corner_configure;
    struct kywc_texture_node *main_surface_node;
    struct kywc_gl_program *gl_program;
};

static void effect_round_corner_generate_render_task(const struct kywc_node *node,
                                                     pixman_region32_t *damage,
                                                     struct kywc_render_target *target,
                                                     struct wl_list *render_tasks);

static void tex_node_round_corner_generate_render_task(const struct kywc_node *node,
                                                       pixman_region32_t *damage,
                                                       struct kywc_render_target *target,
                                                       struct wl_list *render_tasks);

const char *vertex_source = "attribute vec2 position;"
                            "varying vec2 fposition;"
                            "uniform mat4 matrix;"
                            "void main() {"
                            "    gl_Position = matrix * vec4(position, 0.0, 1.0);"
                            "    fposition = position;"
                            "}";

const char *frag_source =
    "@builtin_ext@"
    "@builtin@"
    "varying vec2 fposition;"
    // Top left corner
    "uniform vec2 top_left;"
    // Top left corner with shadows included
    "uniform vec2 full_top_left;"
    // Bottom right corner
    "uniform vec2 bottom_right;"
    // Bottom right corner with shadows included
    "uniform vec2 full_bottom_right;"
    // Rounding radius
    "uniform float radius;"
    // Border color vec4(1.0,1.0,1.0,1.0)
    "uniform vec4 border_color;"
    "uniform float border_width;"
    // Corner position
    "uniform int corner_type;"

    "vec4 get_sample_pos_pixel(vec2 sample_ref_pos,"
    "        float dis_x, float dis_y) {"
    "    vec2 uv;"
    "    vec2 sample_pos = sample_ref_pos + vec2(ceil(dis_x) , ceil(dis_y));"
    "    if (sample_pos.x <= full_top_left.x || sample_pos.y <= full_top_left.y ||"
    "        sample_pos.x >= full_bottom_right.x || sample_pos.y >= full_bottom_right.y) {"
    "        discard;"
    "    }"
    "    uv = (sample_pos - full_top_left) / (full_bottom_right - full_top_left);"
    "    return get_pixel(uv);"
    "}"
    "void main()"
    "{"
    "    int corner_type_temp = corner_type;"
    "    vec2 corner_dist;"
    "    bool border = false;"
    "    if (corner_type_temp >= 8) {"
    "        vec2 round_corner_center = vec2(top_left.x + radius, bottom_right.y - radius);"
    "        float dist;"
    "        vec2 sample_pos_left = round_corner_center + vec2( -radius - 1.0 - "
    "border_width, 0.0);"
    "        vec2 sample_pos_bottom = round_corner_center + vec2( 0.0, radius + 1.0 + "
    "border_width);"
    "        vec2 pos_ubr = fposition - round_corner_center;"
    "        if (pos_ubr.x < 0.0 && pos_ubr.y > 0.0) {" // left-bottom region
    "            dist = distance(fposition, round_corner_center);"
    "            if (dist > (radius + border_width)) {"
    "               if ((-pos_ubr.y / pos_ubr.x) < 1.0) {"
    "                   gl_FragColor = get_sample_pos_pixel(sample_pos_left, -(dist - radius), "
    "border_width);"
    "                } else"
    "                   gl_FragColor = get_sample_pos_pixel(sample_pos_bottom,  border_width, dist "
    "- radius);"
    "                return;"
    "            } else if (dist > radius && (dist <= (radius + border_width))) {"
    "                gl_FragColor = border_color;"
    "                return;"
    "            }"
    "        }"
    "        corner_type_temp -= 8;"
    "    }"
    "    if (corner_type_temp >= 4 && !border) {"
    "        vec2 round_corner_center = bottom_right - vec2(radius, radius);"
    "        float dist;"
    "        vec2 sample_pos_right = round_corner_center + vec2( radius + 1.0, 0);"
    "        vec2 sample_pos_bottom = round_corner_center + vec2( 0, radius + 1.0);"
    "        vec2 pos_ubr = fposition - round_corner_center;"
    "        if (pos_ubr.x > 0.0 && pos_ubr.y > 0.0) {" // right-bottom region
    "            dist = distance(fposition, round_corner_center);"
    "            if (dist > (radius + border_width)) {"
    "               if ((pos_ubr.y / pos_ubr.x) < 1.0)"
    "                   gl_FragColor = get_sample_pos_pixel(sample_pos_right, (dist - radius), "
    "0.0);"
    "                else"
    "                   gl_FragColor = get_sample_pos_pixel(sample_pos_bottom,  0.0, (dist - "
    "radius));"
    "                return;"
    "            } else if (dist > radius && (dist <= (radius + border_width))) {"
    "                gl_FragColor = border_color;"
    "                return;"
    "            }"
    "        }"
    "        corner_type_temp -= 4;"
    "    }"
    "    if (corner_type_temp >= 2 && !border) {"
    "        vec2 round_corner_center = vec2(bottom_right.x - radius, top_left.y + radius);"
    "        float dist;"
    "        vec2 sample_pos_top = round_corner_center + vec2( 0.0, -radius - 1.0);"
    "        vec2 sample_pos_right = round_corner_center + vec2( radius + 1.0, 0);"
    "        vec2 pos_ubr = fposition - round_corner_center;"
    "        if (pos_ubr.x > 0.0 && pos_ubr.y < 0.0) {" // right-top region
    "            dist = distance(fposition, round_corner_center);"
    "            if (dist > (radius + border_width)) {"
    "               if ((-pos_ubr.y / pos_ubr.x) < 1.0)"
    "                   gl_FragColor = get_sample_pos_pixel(sample_pos_right, (dist - radius), "
    "0.0);"
    "                else"
    "                   gl_FragColor = get_sample_pos_pixel(sample_pos_top,  0.0, -(dist - "
    "radius));"
    "                return;"
    "            } else if (dist > radius && (dist <= (radius + border_width))) {"
    "                gl_FragColor = border_color;"
    "                return;"
    "            }"
    "        }"
    "        corner_type_temp -= 2;"
    "    }"
    "    if ( corner_type_temp >= 1 && !border) {"
    "        vec2 round_corner_center = top_left + vec2(radius, radius);"
    "        float dist;"
    "        vec2 sample_pos_top = round_corner_center + vec2( 0.0, -radius - 1.0);"
    "        vec2 sample_pos_left = round_corner_center + vec2( -radius - 1.0, 0.0);"
    "        vec2 pos_ubr = fposition - round_corner_center;"
    "        if (pos_ubr.x < 0.0 && pos_ubr.y < 0.0) {" // left-top region
    "            dist = distance(fposition, round_corner_center);"
    "            if (dist > (radius + border_width)) {"
    "               if ((pos_ubr.y / pos_ubr.x) < 1.0)"
    "                   gl_FragColor = get_sample_pos_pixel(sample_pos_left, -(dist - radius), "
    "0.0);"
    "                else"
    "                   gl_FragColor = get_sample_pos_pixel(sample_pos_top,  0.0, -(dist - "
    "radius));"
    "                return;"
    "            } else if (dist > radius && (dist <= (radius + border_width))) {"
    "                gl_FragColor = border_color;"
    "                return;"
    "            }"
    "        }"
    "    }"
    "    vec2 uv = (fposition - full_top_left) / (full_bottom_right - full_top_left);"
    "    if (!border) {"
    "        gl_FragColor = get_pixel(uv);"
    "    }"
    "}";

static const char *effect_round_corner_class = "round_corner";

static struct kywc_gl_program round_corner_porgram;

static struct round_corner round_corner = {
    .default_conf.active_border_color = { 0.5f, 0.5f, 0.5f, 0.5f },
    .default_conf.inactive_border_color = { 0.5f, 0.5f, 0.5f, 0.5f },
    .default_conf.border_width = 3,
    .default_conf.radius = 25,
    .conf.active_border_color = { 0.5f, 0.5f, 0.5f, 0.5f },
    .conf.inactive_border_color = { 0.5f, 0.5f, 0.5f, 0.5f },
    .conf.border_width = 3,
    .conf.radius = 25,
    .effect_z_order = 49,
    .is_listening = false,
};

/********************round_corner_data*****************************/
static void data_destroy(struct wlr_addon *addon)
{
    struct round_corner_data *conf = wl_container_of(addon, conf, data_addon);
    kywc_effect_addon_finish(&conf->data_addon);
    /* Don't free in here. Create and release with view. */
}

const struct wlr_addon_interface conf_impl = {
    .name = "round_corner_data",
    .destroy = data_destroy,
};

static struct round_corner_data *node_get_cound_corner_data(struct kywc_texture_node *main_surface)
{
    if (!main_surface) {
        return NULL;
    }
    struct kywc_node *node = &main_surface->node;
    struct wlr_addon *addon = kywc_effect_addon_find(&node->addons, node, &conf_impl);
    if (!addon) {
        return NULL;
    }
    struct round_corner_data *conf = wl_container_of(addon, conf, data_addon);
    return conf;
}

static void round_corner_data_addon_finish(struct kywc_texture_node *main_surface,
                                           struct wlr_addon *data_addon)
{
    if (!main_surface) {
        return;
    }
    kywc_effect_addon_finish(data_addon);
}

static void round_corner_data_addon_init(struct kywc_texture_node *main_surface,
                                         struct wlr_addon *data_addon)
{
    if (!main_surface) {
        return;
    }
    kywc_effect_addon_init(data_addon, &main_surface->node.addons, main_surface, &conf_impl);
}

static void round_corner_data_destroy(struct round_corner_data *data)
{
    data->view = NULL;
    free(data);
}

static struct round_corner_data *round_corner_data_create(struct kywc_effect_view *view)
{
    struct round_corner_data *data = malloc(sizeof(*data));
    if (!data) {
        return NULL;
    }
    data->view = view;
    struct round_corner_conf *conf = &round_corner.conf;
    data->conf = conf;
    data->border_width = 0;
    data->type = ALL_ROUND_CORNER;

    for (int i = 0; i < 4; i++) {
        if (!view && view->kywc_view->activatable) {
            data->border_color[i] = data->conf->active_border_color[i];
        } else {
            data->border_color[i] = data->conf->inactive_border_color[i];
        }
    }
    return data;
}

/********************round_corner_node*****************************/
static void calc_corner_region(pixman_region32_t *corner_region, const struct wlr_box *bound,
                               struct round_corner_data *rc)
{
    if (!corner_region) {
        return;
    }
    pixman_region32_clear(corner_region);
    int r = rc->conf->radius;
    if (rc->type & TOP_LEFT_ROUND_CORNER) {
        pixman_region32_union_rect(corner_region, corner_region, bound->x, bound->y, r, r);
    }
    if (rc->type & TOP_RIGHT_ROUND_CORNER) {
        pixman_region32_union_rect(corner_region, corner_region, bound->x + bound->width - r,
                                   bound->y, r, r);
    }
    if (rc->type & BOTTOM_RIGHT_ROUND_CORNER) {
        pixman_region32_union_rect(corner_region, corner_region, bound->x + bound->width - r,
                                   bound->y + bound->height - r, r, r);
    }
    if (rc->type & BOTTOM_LEFT_ROUND_CORNER) {
        pixman_region32_union_rect(corner_region, corner_region, bound->x,
                                   bound->y + bound->height - r, r, r);
    }
    pixman_region32_intersect_rect(corner_region, corner_region, bound->x, bound->y, bound->width,
                                   bound->height);
}

static void calc_node_corner_region(pixman_region32_t *corner_region,
                                    struct effect_round_corner_node *rc_node)
{
    if (!rc_node) {
        return;
    }
    struct wlr_box texture_bound;
    struct kywc_texture_node *tex_node = rc_node->main_surface_node;
    tex_node->node.get_bounding_box(&tex_node->node, &texture_bound);
    calc_corner_region(corner_region, &texture_bound, rc_node->data);
}

static const char *effect_round_corner_name(void)
{
    return "kywc_round_corner_effect_node";
}

static void effect_round_corner_init(struct effect_round_corner_node *round_corner_node,
                                     struct round_corner_data *data)
{
    if (!round_corner_node || !data) {
        return;
    }
    kywc_group_node_init(&round_corner_node->node);
    pixman_region32_init(&round_corner_node->opaque_region);
    kywc_group_node_set_generate_func(&round_corner_node->node,
                                      effect_round_corner_generate_render_task);

    round_corner_node->data = data;
    round_corner_node->name = effect_round_corner_class;
    round_corner_node->node.node.node_name = effect_round_corner_name;
}

/* bound box start point may be not same as opaque_region. */
static void
effect_round_corner_update_opaque_region(int rc_lx, int rc_ly,
                                         struct effect_round_corner_node *round_corner_node)
{
    pixman_region32_clear(&round_corner_node->opaque_region);

    round_corner_node->node.node.get_opaque_region(&round_corner_node->node.node,
                                                   &round_corner_node->opaque_region);

    pixman_region32_t transparent_region;
    pixman_region32_init(&transparent_region);

    struct kywc_texture_node *tex_node = round_corner_node->main_surface_node;
    calc_node_corner_region(&transparent_region, round_corner_node);
    kywc_texture_node_set_corner_region(tex_node, &transparent_region);

    int tex_lx, tex_ly;
    kywc_node_coords(&tex_node->node, &tex_lx, &tex_ly);
    /* in rc_node coord, start point (0,0) is rc_node top_left */
    pixman_region32_translate(&transparent_region, tex_lx - rc_lx, tex_ly - rc_ly);
    pixman_region32_subtract(&round_corner_node->opaque_region, &round_corner_node->opaque_region,
                             &transparent_region);
    pixman_region32_fini(&transparent_region);
}

static struct kywc_gl_program *effect_round_corner_get_gl_program(void)
{
    if (kywc_gl_program_is_compiled(&round_corner_porgram)) {
        return &round_corner_porgram;
    }
    if (kywc_gl_program_compile(&round_corner_porgram, vertex_source, frag_source)) {
        return &round_corner_porgram;
    }
    kywc_log(KYWC_ERROR, "round corner shader compile error!");
    return NULL;
}

static struct effect_round_corner_node *
effect_round_corner_node_create(struct round_corner_data *data, struct kywc_group_node *view_node,
                                struct kywc_texture_node *node)
{
    struct effect_round_corner_node *rc_node = malloc(sizeof(*rc_node));
    if (!rc_node) {
        return NULL;
    }
    if (!effect_round_corner_get_gl_program()) {
        free(rc_node);
        return NULL;
    }

    effect_round_corner_init(rc_node, data);
    rc_node->view_node = view_node;
    rc_node->main_surface_node = node;

    return rc_node;
}

static void effect_round_corner_node_destroy(struct effect_round_corner_node *node)
{
    if (!node) {
        return;
    }
    node->view_node = NULL;
    node->main_surface_node = NULL;
    node->data = NULL;
    pixman_region32_fini(&node->opaque_region);
    free(node);
}

/***************************texture_round_corner_render*************************/
static void tex_round_corner_render(struct kywc_render_instance *instance,
                                    struct kywc_render_target *target, pixman_region32_t *damage)
{
    struct texture_node_round_corner_render_instance *render =
        wl_container_of(instance, render, base);
    if (!render || !render->main_surface_node || !render->gl_program) {
        return;
    }
    struct round_corner_data *data = render->round_corner_configure;
    float pad[4] = { 0 };
    if (data->view) {
        pad[0] = data->view->kywc_view->padding.top;
        pad[1] = data->view->kywc_view->padding.left;
        pad[2] = data->view->kywc_view->padding.right;
        pad[3] = data->view->kywc_view->padding.bottom;
    }

    struct kywc_gl_texture *tex = render->main_surface_node->texture;
    struct kywc_gl_program *tex_round_corner_program = render->gl_program;

    kywc_gl_program_use(tex_round_corner_program, tex->type);
    kywc_target_render_begin(target);
    kywc_gl_push_debug();

    mat4 transform_mat4, ortho_proj_mat4;
    kywc_target_get_transform_mat4(target, transform_mat4);
    kywc_target_get_orth(target, transform_mat4, ortho_proj_mat4);
    float x1 = 0, y1 = 0;
    float x2 = x1 + render->main_surface_node->geometry_width;
    float y2 = y1 + render->main_surface_node->geometry_height;

    GLfloat pos_vertex[8] = {
        x1, y2, x2, y2, x2, y1, x1, y1,
    };

    kywc_gl_program_set_active_texture(tex_round_corner_program, tex);
    kywc_gl_program_attrib_pointer(tex_round_corner_program, "position", 2, 0, pos_vertex,
                                   GL_FLOAT);
    kywc_gl_program_uniform_matrix4f(tex_round_corner_program, "matrix", ortho_proj_mat4);

    kywc_gl_program_uniform2f(tex_round_corner_program, "top_left", x1 + pad[2], y1 + pad[0]);
    kywc_gl_program_uniform2f(tex_round_corner_program, "bottom_right", x2 - pad[1], y2 - pad[3]);

    kywc_gl_program_uniform2f(tex_round_corner_program, "full_top_left", x1, y1);
    kywc_gl_program_uniform2f(tex_round_corner_program, "full_bottom_right", x2, y2);

    kywc_gl_program_uniform1f(tex_round_corner_program, "radius", data->conf->radius);

    kywc_gl_program_uniform1i(tex_round_corner_program, "corner_type", data->type);

    int border_width = data->border_width > -1 ? data->border_width : data->conf->border_width;
    kywc_gl_program_uniform1f(tex_round_corner_program, "border_width",
                              border_width / target->scale);

    float *color = data->border_color;
    kywc_gl_program_uniform4f(tex_round_corner_program, "border_color", color[0], color[1],
                              color[2], color[3]);

    kywc_target_draw_damage(target, tex, damage);

    kywc_gl_render_clear_cached(tex_round_corner_program);
    kywc_target_render_end(target);
}

const struct kywc_render_instance_interface texture_node_round_corner_render_instance_impl = {
    .render = tex_round_corner_render,
    .compute_visible = NULL,
    .try_direct_scanout = NULL,
};

static struct texture_node_round_corner_render_instance *
texture_node_render_instance_create(struct kywc_texture_node *tex_node,
                                    struct round_corner_data *data)
{
    struct texture_node_round_corner_render_instance *texture_rc_render =
        malloc(sizeof(*texture_rc_render));
    if (!texture_rc_render) {
        return NULL;
    }
    struct kywc_gl_program *program = effect_round_corner_get_gl_program();
    if (program == NULL) {
        free(texture_rc_render);
        return NULL;
    }
    texture_rc_render->round_corner_configure = data;
    texture_rc_render->gl_program = program;
    texture_rc_render->main_surface_node = tex_node;

    kywc_render_instance_init(&texture_rc_render->base,
                              &texture_node_round_corner_render_instance_impl,
                              kywc_render_instance_handle_destroy);

    return texture_rc_render;
}

/********************generate_render_task*******************************/
static bool children_generate_render_task(const struct kywc_group_node *node,
                                          struct kywc_texture_node *main_surface,
                                          pixman_region32_t *damage,
                                          struct kywc_render_target *target,
                                          struct wl_list *render_tasks,
                                          struct round_corner_data *data)
{
    kywc_node_generate_render_task_interface default_func = main_surface->node.generate_render_task;
    struct kywc_view *kywc_view = data->view->kywc_view;
    if (data->view && (kywc_view->fullscreen || kywc_view->maximized || kywc_view->tiled)) {
        kywc_group_node_children_generate_render(node, damage, target, render_tasks);
        return false;
    }
    main_surface->node.generate_render_task = tex_node_round_corner_generate_render_task;

    kywc_group_node_children_generate_render(node, damage, target, render_tasks);

    main_surface->node.generate_render_task = default_func;
    return true;
}

static void effect_round_corner_generate_render_task(const struct kywc_node *node,
                                                     pixman_region32_t *damage,
                                                     struct kywc_render_target *target,
                                                     struct wl_list *render_tasks)
{
    struct effect_round_corner_node *rc_node = wl_container_of(node, rc_node, node);
    if (!rc_node || !render_tasks || !damage || !node->enabled) {
        return;
    }

    if (!rc_node->view_node->node.enabled) {
        return;
    }

    bool node_enable = children_generate_render_task(&rc_node->node, rc_node->main_surface_node,
                                                     damage, target, render_tasks, rc_node->data);
    // when the correction is minimized, the code is in the disabled state,
    // and it should not be visible at this time
    // fix the damage of the entire client
    if (!node_enable) {
        return;
    }

    /***********/
    struct wlr_box bound_box;
    node->get_bounding_box(node, &bound_box);
    pixman_region32_t node_damage, node_opaque;
    pixman_region32_init(&node_damage);
    pixman_region32_init(&node_opaque);
    pixman_region32_intersect_rect(&node_damage, damage, bound_box.x, bound_box.y, bound_box.width,
                                   bound_box.height);

    if (!pixman_region32_not_empty(&node_damage)) {
        goto final;
        return;
    }

    effect_round_corner_update_opaque_region(target->lx, target->ly, rc_node);
    pixman_region32_intersect(&node_opaque, &node_damage, &rc_node->opaque_region);
    pixman_region32_subtract(damage, damage, &node_opaque);

    /* Don't create task, just change texture node render_instance. */
final:
    pixman_region32_fini(&node_damage);
    pixman_region32_fini(&node_opaque);
}

static void tex_node_round_corner_generate_render_task(const struct kywc_node *node,
                                                       pixman_region32_t *damage,
                                                       struct kywc_render_target *target,
                                                       struct wl_list *render_tasks)
{
    struct kywc_texture_node *tex_node = wl_container_of(node, tex_node, node);
    if (!render_tasks || !damage || !node->enabled) {
        return;
    }
    struct round_corner_data *conf = node_get_cound_corner_data(tex_node);

    if (!conf) {
        kywc_log(KYWC_ERROR, "Node: %s, texture node %p. Don't find corner data.",
                 node->node_name(), tex_node);
        return;
    }

    struct wlr_box bound_box;
    node->get_bounding_box(node, &bound_box);
    pixman_region32_t node_damage, node_opaque;
    pixman_region32_init(&node_damage);
    pixman_region32_init(&node_opaque);
    pixman_region32_intersect_rect(&node_damage, damage, bound_box.x, bound_box.y, bound_box.width,
                                   bound_box.height);

    if (!pixman_region32_not_empty(&node_damage)) {
        goto final;
        return;
    }

    struct texture_node_round_corner_render_instance *texture_render =
        texture_node_render_instance_create(tex_node, conf);
    if (!texture_render) {
        kywc_log(KYWC_ERROR, "Round corner generate render task failed!");
        return;
    }

    struct kywc_render_task *task =
        kywc_render_task_create(&texture_render->base, &node_damage, target);

    wl_list_insert(render_tasks, &task->link);

final:
    pixman_region32_fini(&node_damage);
    pixman_region32_fini(&node_opaque);
}

/*****************************************************************************/
static void handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct round_corner_data *conf = wl_container_of(listener, conf, handle_view_destroy);

    wl_list_remove(&conf->handle_view_activate.link);
    wl_list_remove(&conf->handle_view_decoration.link);
    wl_list_remove(&conf->handle_view_premap.link);
    wl_list_remove(&conf->handle_view_map.link);
    wl_list_remove(&conf->handle_view_unmap.link);
    wl_list_remove(&conf->handle_view_destroy.link);

    round_corner_data_destroy(conf);
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct round_corner_data *_data = wl_container_of(listener, _data, handle_view_unmap);

    struct kywc_effect_view *view = _data->view;

    view->kywc_view->has_round_corner = false;
    struct kywc_group_node *transform_node =
        kywc_effect_view_remove_transform(view, effect_round_corner_class);
    struct kywc_texture_node *transformed_node = NULL;
    if (transform_node) {
        struct effect_round_corner_node *rc_node = wl_container_of(transform_node, rc_node, node);
        transformed_node = rc_node->main_surface_node;
        pixman_region32_clear(&transformed_node->corner_region);
        effect_round_corner_node_destroy(rc_node);
    }
    round_corner_data_addon_finish(transformed_node, &_data->data_addon);
}

static void handle_view_map(struct wl_listener *listener, void *data)
{
    struct round_corner_data *_data = wl_container_of(listener, _data, handle_view_map);

    struct kywc_effect_view *view = _data->view;
    if (kywc_effect_view_get_transform(view, effect_round_corner_class)) {
        kywc_log(KYWC_INFO, "Get transform failed");
        goto final;
    }
    struct kywc_texture_node *view_texture_node = kywc_effect_view_get_texture_node(view);
    if (!view_texture_node) {
        kywc_log(KYWC_INFO, "Get view texture node is NULL.");
        goto final;
    }
    if (_data != node_get_cound_corner_data(view_texture_node)) {
        round_corner_data_addon_init(view_texture_node, &_data->data_addon);
    }

    struct effect_round_corner_node *transform_node =
        effect_round_corner_node_create(_data, view->view_node, view_texture_node);
    if (!transform_node) {
        kywc_log(KYWC_INFO, "Create transform node failed");
        goto final;
    }

    bool ret = kywc_effect_view_add_transform(
        view, &transform_node->node, round_corner.effect_z_order, effect_round_corner_class);
    if (!ret) {
        kywc_log(KYWC_INFO, "Add round corner transform failed.");
        effect_round_corner_node_destroy(transform_node);
        goto final;
    }
    return;

final:
    view->kywc_view->has_round_corner = false;
    return;
}

static void handle_view_premap(struct wl_listener *listener, void *data)
{
    struct round_corner_data *_data = wl_container_of(listener, _data, handle_view_premap);
    struct kywc_effect_view *view = _data->view;
    view->kywc_view->has_round_corner = true;
}

static void handle_view_activate(struct wl_listener *listener, void *data)
{
    struct round_corner_data *rc_data = wl_container_of(listener, rc_data, handle_view_activate);

    for (int i = 0; i < 4; i++) {
        if (rc_data->view->kywc_view->activatable) {
            rc_data->border_color[i] = rc_data->conf->active_border_color[i];
        } else {
            rc_data->border_color[i] = rc_data->conf->inactive_border_color[i];
        }
    }
}

static void handle_view_decoration(struct wl_listener *listener, void *data)
{
    struct round_corner_data *rc_data = wl_container_of(listener, rc_data, handle_view_decoration);
    if (rc_data->view->kywc_view->ssd != KYWC_SSD_ALL) {
        rc_data->type = ALL_ROUND_CORNER;
        rc_data->border_width = 0;
    } else {
        rc_data->type = BOTTOM_RIGHT_ROUND_CORNER | BOTTOM_LEFT_ROUND_CORNER;
        rc_data->border_width = -1;
    }
}

static void handle_view_create(struct wl_listener *listener, void *data)
{
    struct kywc_effect_view *effect_view = data;
    struct kywc_view *view = effect_view->kywc_view;

    struct round_corner_data *rc_data = round_corner_data_create(effect_view);

    rc_data->handle_view_premap.notify = handle_view_premap;
    wl_signal_add(&view->events.premap, &rc_data->handle_view_premap);

    rc_data->handle_view_map.notify = handle_view_map;
    wl_signal_add(&view->events.map, &rc_data->handle_view_map);

    rc_data->handle_view_unmap.notify = handle_view_unmap;
    wl_signal_add(&view->events.unmap, &rc_data->handle_view_unmap);

    rc_data->handle_view_activate.notify = handle_view_activate;
    wl_signal_add(&view->events.activate, &rc_data->handle_view_activate);

    rc_data->handle_view_decoration.notify = handle_view_decoration;
    wl_signal_add(&view->events.decoration, &rc_data->handle_view_decoration);

    rc_data->handle_view_destroy.notify = handle_view_destroy;
    wl_signal_add(&view->events.destroy, &rc_data->handle_view_destroy);
}

static void handle_theme_update(struct wl_listener *listener, void *data)
{
    struct round_corner *conf = wl_container_of(listener, conf, handle_theme_update);

    int *width = kywc_theme_manager_get_current("border_width");
    int *radius = kywc_theme_manager_get_current("corner_radius");
    float *active_color = kywc_theme_manager_get_current("active_border_color");
    float *inactive_color = kywc_theme_manager_get_current("inactive_border_color");

    conf->conf.border_width = *width;
    conf->conf.radius = *radius;
    for (int i = 0; i < 4; i++) {
        conf->conf.active_border_color[i] = active_color[i];
        conf->conf.inactive_border_color[i] = inactive_color[i];
    }
}

static bool round_corner_plugin_init(void *plugin, void **teardown_data)
{
    struct kywc_effect_server *server = kywc_effect_server();
    if (!server || !kywc_renderer_is_opengl(server)) {
        kywc_log(KYWC_ERROR, "Round_corner_plugin_init failed");
        return false;
    }
    if (!round_corner.is_listening) {
        round_corner.is_listening = true;
        round_corner.handle_view_new.notify = handle_view_create;
        wl_signal_add(&server->events.view_new, &round_corner.handle_view_new);

        round_corner.handle_theme_update.notify = handle_theme_update;
        kywc_theme_manager_add_update_listener(&round_corner.handle_theme_update);
        handle_theme_update(&round_corner.handle_theme_update, NULL);
    }

    return true;
}

static void round_corner_plugin_teardown(void *teardown_data)
{
    kywc_gl_program_delete(&round_corner_porgram);
    wl_list_remove(&round_corner.handle_view_new.link);
    wl_list_remove(&round_corner.handle_theme_update.link);
    round_corner.is_listening = false;
}

static struct kywc_plugin_info info = {
    .name = "kywc_round_corner_effect.so",
    .abi_version = 1,
    .version = 1,
    .class = "round_corner",
};

struct kywc_plugin_data libkywc_round_corner_effect_plugin_data = {
    .info = &info,
    .options = NULL,
    .option = NULL,
    .setup = round_corner_plugin_init,
    .teardown = round_corner_plugin_teardown,
};
