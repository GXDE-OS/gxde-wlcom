// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include "decoration_frag.h"
#include "decoration_vert.h"
#include "render/opengl.h"
#include "scene/decoration.h"
#include "scene_p.h"
#include "util/matrix.h"

enum deco_update_cause {
    DECO_UPDATE_CAUSE_NONE = 0,
    DECO_UPDATE_CAUSE_WINDOW_SIZE = 1 << 0,
    DECO_UPDATE_CAUSE_MARGIN = 1 << 1,
    DECO_UPDATE_CAUSE_MARGIN_COLOR = 1 << 2,
    DECO_UPDATE_CAUSE_SHADOW_MASK = 1 << 3,
    DECO_UPDATE_CAUSE_RESIZE_WIDTH = 1 << 4,
    DECO_UPDATE_CAUSE_CORNER_RADIUS = 1 << 5,
};

struct ky_scene_decoration {
    /* based on scene_rect */
    struct ky_scene_rect rect;
    ky_scene_node_destroy_func_t node_destroy;
    uint32_t pending_cause;

    /* window size */
    int window_width;
    int window_height;

    /* margin */
    int border_thickness;
    int title_height;
    int shadow_width;

    /* margin color */
    float border_color[4];
    float title_color[4];
    float shadow_color[4];

    /* region used to resize */
    int resize_width;

    // window round rect
    // 0=right-bottom, 1=right-top, 2=left-bottom, 3=left-top
    int round_corner_radius[4];
    /* shadow part need be shown */
    uint32_t shadow_mask;

    pixman_region32_t title_region;
    pixman_region32_t border_region;
    pixman_region32_t shadow_region;
    pixman_region32_t round_corner_region;
    /* clip region without round_corner_region */
    pixman_region32_t pure_clip_region;
};

// opengl render
static int32_t gl_shader = 0;

struct gl_shader_location {
    // vs
    GLint in_uv;
    GLint size;
    GLint uv2ndc;
    GLint inverse_transform;
    // fs
    GLint shadow_rect;
    GLint shadow_sigma;
    GLint shadow_color;
    GLint border_aa;
    GLint aspect;
    GLint window_rect;
    GLint rounded_corner_radius;
    GLint border_thickness;
    GLint border_color;
    GLint title_height;
    GLint title_color;
};
static struct gl_shader_location gl_locations;

static int scene_decoration_create_opengl_shader(void)
{
    GLint ok = GL_TRUE;
    GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
    const GLchar *vert_src = decoration_vert;
    glShaderSource(vert_shader, 1, &vert_src, NULL);
    glCompileShader(vert_shader);
    glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        kywc_log(KYWC_ERROR, "Failed to compile vert shader");
        glDeleteShader(vert_shader);
        return -1;
    }

    GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar *frag_src = decoration_frag;
    glShaderSource(frag_shader, 1, &frag_src, NULL);
    glCompileShader(frag_shader);
    glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        kywc_log(KYWC_ERROR, "Failed to compile frag shader");
        glDeleteShader(vert_shader);
        glDeleteShader(frag_shader);
        return -1;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert_shader);
    glAttachShader(prog, frag_shader);
    glLinkProgram(prog);

    glDetachShader(prog, vert_shader);
    glDetachShader(prog, frag_shader);
    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);

    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        kywc_log(KYWC_ERROR, "Failed to link shader program");
        glDeleteProgram(prog);
        glDeleteShader(frag_shader);
        glDeleteShader(vert_shader);
        return -1;
    }

    gl_locations.in_uv = glGetAttribLocation(prog, "inUV");
    gl_locations.uv2ndc = glGetUniformLocation(prog, "uv2ndc");
    gl_locations.inverse_transform = glGetUniformLocation(prog, "inverseTransform");
    gl_locations.size = glGetUniformLocation(prog, "size");
    gl_locations.shadow_sigma = glGetUniformLocation(prog, "shadowSigma");
    gl_locations.shadow_rect = glGetUniformLocation(prog, "shadowRect");
    gl_locations.shadow_color = glGetUniformLocation(prog, "shadowColor");
    gl_locations.border_aa = glGetUniformLocation(prog, "borderAA");
    gl_locations.aspect = glGetUniformLocation(prog, "aspect");
    gl_locations.window_rect = glGetUniformLocation(prog, "windowRect");
    gl_locations.rounded_corner_radius = glGetUniformLocation(prog, "roundedCornerRadius");
    gl_locations.border_thickness = glGetUniformLocation(prog, "borderThickness");
    gl_locations.border_color = glGetUniformLocation(prog, "borderColor");
    gl_locations.title_height = glGetUniformLocation(prog, "titleHeight");
    gl_locations.title_color = glGetUniformLocation(prog, "titleColor");

    return prog;
}

static void scene_decoration_opengl_render(struct ky_scene_decoration *deco, int lx, int ly,
                                           struct ky_scene_render_target *target,
                                           const struct wlr_box *box, const pixman_region32_t *clip)
{
    pixman_region32_t region;
    pixman_region32_init_rect(&region, box->x, box->y, box->width, box->height);
    pixman_region32_intersect(&region, &region, clip);

    int rects_len;
    const pixman_box32_t *rects = pixman_region32_rectangles(&region, &rects_len);
    if (rects_len == 0) {
        pixman_region32_fini(&region);
        return;
    }

    GLfloat verts[rects_len * 6 * 2];
    size_t vert_index = 0;
    for (int i = 0; i < rects_len; i++) {
        const pixman_box32_t *rect = &rects[i];
        verts[vert_index++] = (GLfloat)(rect->x1 - box->x) / box->width;
        verts[vert_index++] = (GLfloat)(rect->y1 - box->y) / box->height;
        verts[vert_index++] = (GLfloat)(rect->x2 - box->x) / box->width;
        verts[vert_index++] = (GLfloat)(rect->y1 - box->y) / box->height;
        verts[vert_index++] = (GLfloat)(rect->x1 - box->x) / box->width;
        verts[vert_index++] = (GLfloat)(rect->y2 - box->y) / box->height;
        verts[vert_index++] = (GLfloat)(rect->x2 - box->x) / box->width;
        verts[vert_index++] = (GLfloat)(rect->y1 - box->y) / box->height;
        verts[vert_index++] = (GLfloat)(rect->x2 - box->x) / box->width;
        verts[vert_index++] = (GLfloat)(rect->y2 - box->y) / box->height;
        verts[vert_index++] = (GLfloat)(rect->x1 - box->x) / box->width;
        verts[vert_index++] = (GLfloat)(rect->y2 - box->y) / box->height;
    }

    float scale = target->scale;
    float width = box->width;
    float height = box->height;
    if (target->transform & WL_OUTPUT_TRANSFORM_90) {
        width = box->height;
        height = box->width;
    }
    // keep border ceil scale. avoid non-integer scale different thickness
    int border_thickness = ceil(deco->border_thickness * scale);
    int border_thickness_2 = border_thickness * 2;
    int title_height = round(deco->title_height * scale);
    float half_height = height * 0.5f; // shader distance scale
    float shadow_width = deco->shadow_width * scale;
    float round_corner_radius[4] = { 0 };
    if (!(target->options & KY_SCENE_RENDER_DISABLE_ROUND_CORNER)) {
        round_corner_radius[0] = deco->round_corner_radius[0] > 0
                                     ? deco->round_corner_radius[0] * scale + border_thickness
                                     : 0.0f;
        round_corner_radius[1] = deco->round_corner_radius[1] > 0
                                     ? deco->round_corner_radius[0] * scale + border_thickness
                                     : 0.0f;
        round_corner_radius[2] = deco->round_corner_radius[2] > 0
                                     ? deco->round_corner_radius[0] * scale + border_thickness
                                     : 0.0f;
        round_corner_radius[3] = deco->round_corner_radius[3] > 0
                                     ? deco->round_corner_radius[0] * scale + border_thickness
                                     : 0.0f;
    }

    // full rect logic coord to framebuffer coord
    struct wlr_box window_box = {
        .x = lx + deco->shadow_width + deco->border_thickness - target->logical.x,
        .y = ly + deco->shadow_width + deco->border_thickness + deco->title_height -
             target->logical.y,
        .width = deco->window_width,
        .height = deco->window_height,
    };
    ky_scene_render_box(&window_box, target);
    // base by framebuffer coord window rect. avoid non-integer scale 1 pixel offset
    // framebuffer coord origin on monitor left-top. need rotation correct
    struct wlr_box window_frame = {};
    if (target->transform == WL_OUTPUT_TRANSFORM_90) {
        window_frame.x = window_box.y - border_thickness - box->y;
        window_frame.y = window_box.x - title_height - border_thickness - box->x;
        window_frame.width = window_box.height + border_thickness_2;
        window_frame.height = window_box.width + title_height + border_thickness_2;
        // rotation correct
        window_frame.x = box->height - window_box.height - border_thickness_2 - window_frame.x;
    } else if (target->transform == WL_OUTPUT_TRANSFORM_180) {
        window_frame.x = window_box.x - border_thickness - box->x;
        window_frame.y = window_box.y - border_thickness - box->y;
        window_frame.width = window_box.width + border_thickness_2;
        window_frame.height = window_box.height + title_height + border_thickness_2;
        // rotation correct
        window_frame.x = box->width - window_box.width - border_thickness_2 - window_frame.x;
        window_frame.y =
            box->height - window_box.height - border_thickness_2 - title_height - window_frame.y;
    } else if (target->transform == WL_OUTPUT_TRANSFORM_270) {
        window_frame.x = window_box.y - border_thickness - box->y;
        window_frame.y = window_box.x - border_thickness - box->x;
        window_frame.width = window_box.height + border_thickness_2;
        window_frame.height = window_box.width + title_height + border_thickness_2;
        // rotation correct
        window_frame.y =
            box->width - window_box.width - border_thickness_2 - title_height - window_frame.y;
    } else if (target->transform == WL_OUTPUT_TRANSFORM_FLIPPED) {
        window_frame.x = window_box.x - border_thickness - box->x;
        window_frame.y = window_box.y - title_height - border_thickness - box->y;
        window_frame.width = window_box.width + border_thickness_2;
        window_frame.height = window_box.height + title_height + border_thickness_2;
        // rotation correct
        window_frame.x = box->width - window_box.width - border_thickness_2 - window_frame.x;
    } else if (target->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90) {
        window_frame.x = window_box.y - border_thickness - box->y;
        window_frame.y = window_box.x - title_height - border_thickness - box->x;
        window_frame.width = window_box.height + border_thickness_2;
        window_frame.height = window_box.width + title_height + border_thickness_2;
    } else if (target->transform == WL_OUTPUT_TRANSFORM_FLIPPED_180) {
        window_frame.x = window_box.x - border_thickness - box->x;
        window_frame.y = window_box.y - border_thickness - box->y;
        window_frame.width = window_box.width + border_thickness_2;
        window_frame.height = window_box.height + title_height + border_thickness_2;
        // rotation correct
        window_frame.y =
            box->height - window_box.height - border_thickness_2 - title_height - window_frame.y;
    } else if (target->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
        window_frame.x = window_box.y - border_thickness - box->y;
        window_frame.y = window_box.x - border_thickness - box->x;
        window_frame.width = window_box.height + border_thickness_2;
        window_frame.height = window_box.width + title_height + border_thickness_2;
        // rotation correct
        window_frame.x = box->height - window_box.height - border_thickness_2 - window_frame.x;
        window_frame.y =
            box->width - window_box.width - border_thickness_2 - title_height - window_frame.y;
    } else {
        window_frame.x = window_box.x - border_thickness - box->x;
        window_frame.y = window_box.y - title_height - border_thickness - box->y;
        window_frame.width = window_box.width + border_thickness_2;
        window_frame.height = window_box.height + title_height + border_thickness_2;
    }

    struct ky_mat3 projection;
    ky_mat3_framebuffer_to_ndc(&projection, target->buffer->width, target->buffer->height);
    struct ky_mat3 uv2pos;
    ky_mat3_identity(&uv2pos);
    ky_mat3_scale(&uv2pos, box->width, box->height);
    ky_mat3_translate(&uv2pos, box->x, box->y);
    struct ky_mat3 uv2ndc;
    ky_mat3_multiply(&projection, &uv2pos, &uv2ndc);

    struct ky_mat3 inverseTransform;
    ky_mat3_invert_output_transform(&inverseTransform, target->transform);

    glEnable(GL_BLEND);
    glUseProgram(gl_shader);

    glEnableVertexAttribArray(gl_locations.in_uv);
    glVertexAttribPointer(gl_locations.in_uv, 2, GL_FLOAT, GL_FALSE, 0, verts);
    // vert shader param
    glUniformMatrix3fv(gl_locations.uv2ndc, 1, GL_FALSE, uv2ndc.matrix);
    glUniformMatrix3fv(gl_locations.inverse_transform, 1, GL_FALSE, inverseTransform.matrix);
    glUniform2f(gl_locations.size, width, height);
    // frag shader param
    // blur with to gaussian sigma. scale = 1.0 / (2.0 * sqrt(2.0 * log(2.0))) = 0.424660891
    glUniform1f(gl_locations.shadow_sigma, shadow_width * 0.424660891f);
    glUniform4f(gl_locations.shadow_rect, window_frame.x, window_frame.y,
                window_frame.x + window_frame.width, window_frame.y + window_frame.height);
    glUniform4fv(gl_locations.shadow_color, 1, deco->shadow_color);
    // 1 pixel border thickness keep 1 pixel less soft aa
    float one_pixel_distance = 1.0 / half_height;
    glUniform1f(gl_locations.border_aa,
                border_thickness > 1 ? one_pixel_distance * 0.7f : one_pixel_distance * 0.5f);
    glUniform1f(gl_locations.aspect, width / height);
    float width_distance = window_frame.width / height;
    float height_distance = window_frame.height / height;
    float offset_x_distance = width_distance * 0.5f + window_frame.x / height;
    float offset_y_distance = height_distance * 0.5f + window_frame.y / height;
    glUniform4f(gl_locations.window_rect, offset_x_distance, offset_y_distance, width_distance,
                height_distance);
    glUniform4f(
        gl_locations.rounded_corner_radius,
        deco->shadow_mask & SHADOW_MASK_BOTTOM_RIGHT ? round_corner_radius[0] / half_height : 0.0f,
        deco->shadow_mask & SHADOW_MASK_TOP_RIGHT ? round_corner_radius[1] / half_height : 0.0f,
        deco->shadow_mask & SHADOW_MASK_BOTTOM_LEFT ? round_corner_radius[2] / half_height : 0.0f,
        deco->shadow_mask & SHADOW_MASK_TOP_LEFT ? round_corner_radius[3] / half_height : 0.0f);
    glUniform1f(gl_locations.border_thickness, border_thickness / half_height);
    glUniform4fv(gl_locations.border_color, 1, deco->border_color);
    glUniform1f(gl_locations.title_height, title_height / height);
    glUniform4fv(gl_locations.title_color, 1, deco->title_color);

    glDrawArrays(GL_TRIANGLES, 0, rects_len * 6);
    glUseProgram(0);
    glDisableVertexAttribArray(gl_locations.in_uv);

    pixman_region32_fini(&region);
}

static void scene_decoration_update_round_corner_region(struct ky_scene_decoration *deco)
{
    pixman_region32_clear(&deco->round_corner_region);

    if (gl_shader < 0) {
        return;
    }

    int width = deco->window_width;
    int height = deco->window_height;

    // include border round_corner
    float round_corner_radius[4] = {
        deco->round_corner_radius[0] + deco->border_thickness,
        deco->round_corner_radius[1] + deco->border_thickness,
        deco->round_corner_radius[2] + deco->border_thickness,
        deco->round_corner_radius[3] + deco->border_thickness,
    };
    int x1 = deco->shadow_width;
    int x2 = deco->shadow_width + width + 2 * deco->border_thickness;
    int y1 = deco->shadow_width;
    int y2 = deco->shadow_width + height + deco->title_height + 2 * deco->border_thickness;
    if ((deco->shadow_mask & SHADOW_MASK_TOP_LEFT) && round_corner_radius[3] > 0) {
        pixman_region32_union_rect(&deco->round_corner_region, &deco->round_corner_region, x1, y1,
                                   round_corner_radius[3], round_corner_radius[3]);
    }
    if ((deco->shadow_mask & SHADOW_MASK_TOP_RIGHT) && round_corner_radius[1] > 0) {
        pixman_region32_union_rect(&deco->round_corner_region, &deco->round_corner_region,
                                   x2 - round_corner_radius[1], y1, round_corner_radius[1],
                                   round_corner_radius[1]);
    }
    if ((deco->shadow_mask & SHADOW_MASK_BOTTOM_LEFT) && round_corner_radius[2] > 0) {
        pixman_region32_union_rect(&deco->round_corner_region, &deco->round_corner_region, x1,
                                   y2 - round_corner_radius[2], round_corner_radius[2],
                                   round_corner_radius[2]);
    }
    if ((deco->shadow_mask & SHADOW_MASK_BOTTOM_RIGHT) && round_corner_radius[0] > 0) {
        pixman_region32_union_rect(&deco->round_corner_region, &deco->round_corner_region,
                                   x2 - round_corner_radius[0], y2 - round_corner_radius[0],
                                   round_corner_radius[0], round_corner_radius[0]);
    }
}

static void scene_decoration_update_clip_region(struct ky_scene_decoration *deco)
{
    pixman_region32_clear(&deco->title_region);
    pixman_region32_clear(&deco->border_region);
    pixman_region32_clear(&deco->shadow_region);
    pixman_region32_clear(&deco->pure_clip_region);

    int width = deco->window_width;
    int height = deco->window_height;
    int shadow = deco->shadow_width;
    int title = deco->title_height;
    int border = deco->border_thickness;

    pixman_region32_init_rect(&deco->pure_clip_region, 0, 0, deco->rect.width, deco->rect.height);

    int x = shadow + border;
    int y = shadow + border + title;
    pixman_region32_init_rect(&deco->title_region, x, x, width, title);

    pixman_region32_t reg1;
    pixman_region32_init_rect(&reg1, shadow, shadow, deco->rect.width - 2 * shadow,
                              deco->rect.height - 2 * shadow);
    pixman_region32_subtract(&deco->shadow_region, &deco->pure_clip_region, &reg1);

    pixman_region32_t reg2;
    pixman_region32_init_rect(&reg2, x, x, width, title + height);
    pixman_region32_subtract(&deco->border_region, &reg1, &reg2);
    pixman_region32_fini(&reg1);
    pixman_region32_fini(&reg2);

    pixman_region32_t region;
    pixman_region32_init_rect(&region, x, y, width, height);
    pixman_region32_subtract(&deco->pure_clip_region, &deco->pure_clip_region, &region);
    pixman_region32_fini(&region);
}

static void scene_decoration_update_input_region(struct ky_scene_decoration *scene_decoration)
{
    pixman_region32_t input;
    pixman_region32_init(&input);

    int shadow = scene_decoration->shadow_width;
    int border = scene_decoration->border_thickness;
    int resize = scene_decoration->resize_width;

    int off = resize > 0 ? shadow - resize : shadow + border;
    int w = scene_decoration->rect.width - 2 * off;
    int h = scene_decoration->rect.height - 2 * off;
    pixman_region32_init_rect(&input, off, off, w, h);

    ky_scene_node_set_input_region(&scene_decoration->rect.node, &input);
    pixman_region32_fini(&input);
}

static void scene_decoration_update(struct ky_scene_decoration *deco, uint32_t cause)
{
    int width = deco->window_width;
    int height = deco->window_height;
    int shadow = deco->shadow_width;
    int title = deco->title_height;
    int border = deco->border_thickness;

    /* first check we have enough data to calc all regions */
    if (width <= 0 || height <= 0 || (shadow <= 0 && border <= 0 && title <= 0)) {
        deco->pending_cause |= cause;
        return;
    }

    uint32_t pending_cause = deco->pending_cause | cause;

    if (pending_cause & (DECO_UPDATE_CAUSE_WINDOW_SIZE | DECO_UPDATE_CAUSE_MARGIN)) {
        int margin = 2 * (border + shadow);
        int rect_width = width + margin;
        int rect_height = height + title + margin;
        if (rect_width != deco->rect.width || rect_height != deco->rect.height) {
            ky_scene_rect_set_size(&deco->rect, rect_width, rect_height);
        }
        scene_decoration_update_clip_region(deco);
    }

    if (pending_cause & (DECO_UPDATE_CAUSE_WINDOW_SIZE | DECO_UPDATE_CAUSE_MARGIN |
                         DECO_UPDATE_CAUSE_CORNER_RADIUS | DECO_UPDATE_CAUSE_SHADOW_MASK)) {
        scene_decoration_update_round_corner_region(deco);
        /* update clip region with corner region */
        pixman_region32_t clip;
        pixman_region32_init(&clip);
        pixman_region32_copy(&clip, &deco->pure_clip_region);
        pixman_region32_union(&clip, &clip, &deco->round_corner_region);
        ky_scene_node_set_clip_region(&deco->rect.node, &clip);
        pixman_region32_fini(&clip);
    }

    if (pending_cause & (DECO_UPDATE_CAUSE_WINDOW_SIZE | DECO_UPDATE_CAUSE_MARGIN |
                         DECO_UPDATE_CAUSE_RESIZE_WIDTH)) {
        scene_decoration_update_input_region(deco);
    }

    if (pending_cause == DECO_UPDATE_CAUSE_SHADOW_MASK) {
        ky_scene_node_push_damage(&deco->rect.node, KY_SCENE_DAMAGE_HARMFUL, &deco->shadow_region);
    }

    if (pending_cause == DECO_UPDATE_CAUSE_MARGIN_COLOR) {
        ky_scene_node_push_damage(&deco->rect.node, KY_SCENE_DAMAGE_HARMFUL,
                                  &deco->rect.node.clip_region);
    }

    deco->pending_cause = DECO_UPDATE_CAUSE_NONE;
}

struct ky_scene_decoration *ky_scene_decoration_from_node(struct ky_scene_node *node)
{
    struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
    struct ky_scene_decoration *scene_decoration = wl_container_of(rect, scene_decoration, rect);
    return scene_decoration;
}

struct ky_scene_node *ky_scene_node_from_decoration(struct ky_scene_decoration *scene_decoration)
{
    return &scene_decoration->rect.node;
}

static void scene_decoration_collect_damage(struct ky_scene_node *node, int lx, int ly,
                                            bool parent_enabled, uint32_t damage_type,
                                            pixman_region32_t *damage, pixman_region32_t *invisible,
                                            pixman_region32_t *affected)
{
    bool node_enabled = parent_enabled && node->enabled;
    /* node is still disabled, skip it */
    if (!node_enabled && !node->last_enabled) {
        node->damage_type = KY_SCENE_DAMAGE_NONE;
        return;
    }

    struct ky_scene_decoration *deco = ky_scene_decoration_from_node(node);
    // if node state is changed, it must in the affected region
    if (deco->rect.width > 0 && deco->rect.height > 0 &&
        pixman_region32_contains_rectangle(
            affected, &(pixman_box32_t){ lx, ly, lx + deco->rect.width, ly + deco->rect.height }) ==
            PIXMAN_REGION_OUT) {
        return;
    }

    // no damage if node state is not changed
    bool no_damage = node->last_enabled && node_enabled && damage_type == KY_SCENE_DAMAGE_NONE;
    if (!no_damage) {
        /* node last visible region is added to damgae */
        if (node->last_enabled && (!node_enabled || (damage_type & KY_SCENE_DAMAGE_HARMFUL))) {
            pixman_region32_union(damage, damage, &node->visible_region);
        }
    }

    // update node visible region always
    pixman_region32_clear(&node->visible_region);

    if (node_enabled) {
        // always have clip region
        pixman_region32_intersect_rect(&node->visible_region, &node->clip_region, 0, 0,
                                       deco->rect.width, deco->rect.height);
        pixman_region32_translate(&node->visible_region, lx, ly);
        pixman_region32_subtract(&node->visible_region, &node->visible_region, invisible);

        if (!no_damage) {
            pixman_region32_union(damage, damage, &node->visible_region);
        }

        bool border_is_opaque = deco->border_thickness > 0 && deco->border_color[3] == 1;
        bool title_is_opaque = deco->title_height > 0 && deco->title_color[3] == 1;
        bool has_opaque_region = border_is_opaque || title_is_opaque;
        if (has_opaque_region) {
            pixman_region32_t region;
            pixman_region32_init(&region);
            if (border_is_opaque) {
                pixman_region32_union(&region, &region, &deco->border_region);
            }
            if (title_is_opaque) {
                pixman_region32_union(&region, &region, &deco->title_region);
            }
            pixman_region32_subtract(&region, &region, &deco->round_corner_region);
            pixman_region32_translate(&region, lx, ly);
            pixman_region32_union(invisible, invisible, &region);
            pixman_region32_fini(&region);
        }
    }

    node->last_enabled = node_enabled;
    node->damage_type = KY_SCENE_DAMAGE_NONE;
}

static void scene_decoration_render(struct ky_scene_node *node, int lx, int ly,
                                    struct ky_scene_render_target *target)
{
    if (!node->enabled) {
        return;
    }

    bool render_with_visibility = !(target->options & KY_SCENE_RENDER_DISABLE_VISIBILITY);
    if (render_with_visibility && !pixman_region32_not_empty(&node->visible_region)) {
        return;
    }

    pixman_region32_t render_region;
    pixman_region32_init(&render_region);
    if (render_with_visibility) {
        pixman_region32_intersect(&render_region, &node->visible_region, &target->damage);
    } else {
        pixman_region32_copy(&render_region, &target->damage);
    }

    if (!pixman_region32_not_empty(&render_region)) {
        pixman_region32_fini(&render_region);
        return;
    }

    struct ky_scene_decoration *deco = ky_scene_decoration_from_node(node);

    struct wlr_box dst_box = {
        .x = lx - target->logical.x,
        .y = ly - target->logical.y,
        .width = deco->rect.width,
        .height = deco->rect.height,
    };
    ky_scene_render_box(&dst_box, target);

    pixman_region32_translate(&render_region, -target->logical.x, -target->logical.y);

    // try opengl render if opengl is used
    if (gl_shader >= 0 && wlr_renderer_is_opengl(target->output->output->renderer)) {
        if (gl_shader == 0) {
            gl_shader = scene_decoration_create_opengl_shader();
        }
        if (gl_shader > 0) {
            ky_scene_render_region(&render_region, target);
            scene_decoration_opengl_render(deco, lx, ly, target, &dst_box, &render_region);
            pixman_region32_fini(&render_region);
            return;
        } else {
            gl_shader = -1;
        }
    }

    /* draw border with border color */
    if (deco->border_thickness > 0) {
        pixman_region32_t border;
        pixman_region32_init(&border);
        pixman_region32_copy(&border, &deco->border_region);
        pixman_region32_translate(&border, lx - target->logical.x, ly - target->logical.y);
        pixman_region32_intersect(&border, &border, &render_region);
        ky_scene_render_region(&border, target);

        wlr_render_pass_add_rect(target->render_pass, &(struct wlr_render_rect_options){
			.box = dst_box,
			.color = {
				.r = deco->border_color[0],
				.g = deco->border_color[1],
				.b = deco->border_color[2],
				.a = deco->border_color[3],
			},
            .clip = &border,
            .blend_mode = deco->border_color[3] != 1 ? 
                WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
		});
        pixman_region32_fini(&border);
    }

    /* draw title with title color */
    if (deco->title_height > 0) {
        pixman_region32_t title;
        pixman_region32_init(&title);
        pixman_region32_copy(&title, &deco->title_region);
        pixman_region32_translate(&title, lx - target->logical.x, ly - target->logical.y);
        pixman_region32_intersect(&title, &title, &render_region);
        ky_scene_render_region(&title, target);

        wlr_render_pass_add_rect(target->render_pass, &(struct wlr_render_rect_options){
			.box = dst_box,
			.color = {
				.r = deco->title_color[0],
				.g = deco->title_color[1],
				.b = deco->title_color[2],
				.a = deco->title_color[3],
			},
            .clip = &title,
            .blend_mode = deco->title_color[3] != 1 ? 
                WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
		});
        pixman_region32_fini(&title);
    }

    pixman_region32_fini(&render_region);
}

static void scene_decoration_destroy(struct ky_scene_node *node)
{
    if (!node) {
        return;
    }

    struct ky_scene_decoration *scene_decoration = ky_scene_decoration_from_node(node);
    pixman_region32_fini(&scene_decoration->title_region);
    pixman_region32_fini(&scene_decoration->border_region);
    pixman_region32_fini(&scene_decoration->shadow_region);
    pixman_region32_fini(&scene_decoration->round_corner_region);
    pixman_region32_fini(&scene_decoration->pure_clip_region);

    scene_decoration->node_destroy(node);
}

struct ky_scene_decoration *ky_scene_decoration_create(struct ky_scene_tree *parent)
{
    struct ky_scene_decoration *scene_decoration = calloc(1, sizeof(struct ky_scene_decoration));
    if (!scene_decoration) {
        return NULL;
    }

    ky_scene_rect_init(&scene_decoration->rect, parent, 0, 0, (float[4]){ 0, 0, 0, 0.5 });

    scene_decoration->node_destroy = scene_decoration->rect.node.impl.destroy;
    scene_decoration->rect.node.impl.destroy = scene_decoration_destroy;
    scene_decoration->rect.node.impl.collect_damage = scene_decoration_collect_damage;
    scene_decoration->rect.node.impl.render = scene_decoration_render;
    /* no need to update_region and push_damage, it is invisible */

    pixman_region32_init(&scene_decoration->title_region);
    pixman_region32_init(&scene_decoration->border_region);
    pixman_region32_init(&scene_decoration->shadow_region);
    pixman_region32_init(&scene_decoration->round_corner_region);
    pixman_region32_init(&scene_decoration->pure_clip_region);

    return scene_decoration;
}

void ky_scene_decoration_set_window_size(struct ky_scene_decoration *scene_decoration, int width,
                                         int height)
{
    if (scene_decoration->window_width == width && scene_decoration->window_height == height) {
        return;
    }

    scene_decoration->window_width = width;
    scene_decoration->window_height = height;
    scene_decoration_update(scene_decoration, DECO_UPDATE_CAUSE_WINDOW_SIZE);
}

void ky_scene_decoration_set_round_corner_radius(struct ky_scene_decoration *scene_decoration,
                                                 const int round_corner_radius[static 4])
{
    if (memcmp(scene_decoration->round_corner_radius, round_corner_radius,
               sizeof(scene_decoration->round_corner_radius)) == 0) {
        return;
    }

    memcpy(scene_decoration->round_corner_radius, round_corner_radius,
           sizeof(scene_decoration->round_corner_radius));
    scene_decoration_update(scene_decoration, DECO_UPDATE_CAUSE_CORNER_RADIUS);
}

void ky_scene_decoration_set_margin(struct ky_scene_decoration *scene_decoration, int title_height,
                                    int border_thickness, int shadow_width)
{
    if (scene_decoration->title_height == title_height &&
        scene_decoration->border_thickness == border_thickness &&
        scene_decoration->shadow_width == shadow_width) {
        return;
    }

    scene_decoration->title_height = title_height;
    scene_decoration->border_thickness = border_thickness;
    scene_decoration->shadow_width = shadow_width;
    scene_decoration_update(scene_decoration, DECO_UPDATE_CAUSE_MARGIN);
}

void ky_scene_decoration_set_margin_color(struct ky_scene_decoration *scene_decoration,
                                          const float title_color[static 4],
                                          const float border_color[static 4],
                                          const float shadow_color[static 4])
{
    if (memcmp(scene_decoration->title_color, title_color, sizeof(scene_decoration->title_color)) ==
            0 &&
        memcmp(scene_decoration->border_color, border_color,
               sizeof(scene_decoration->border_color)) == 0 &&
        memcmp(scene_decoration->shadow_color, shadow_color,
               sizeof(scene_decoration->shadow_color)) == 0) {
        return;
    }

    memcpy(scene_decoration->title_color, title_color, sizeof(scene_decoration->title_color));
    memcpy(scene_decoration->border_color, border_color, sizeof(scene_decoration->border_color));
    memcpy(scene_decoration->shadow_color, shadow_color, sizeof(scene_decoration->shadow_color));
    scene_decoration_update(scene_decoration, DECO_UPDATE_CAUSE_MARGIN_COLOR);
}

void ky_scene_decoration_set_shadow_mask(struct ky_scene_decoration *scene_decoration,
                                         uint32_t shadow_mask)
{
    if (scene_decoration->shadow_mask == shadow_mask) {
        return;
    }

    scene_decoration->shadow_mask = shadow_mask;
    scene_decoration_update(scene_decoration, DECO_UPDATE_CAUSE_SHADOW_MASK);
}

void ky_scene_decoration_set_resize_width(struct ky_scene_decoration *scene_decoration,
                                          int resize_with)
{
    if (scene_decoration->resize_width == resize_with) {
        return;
    }

    scene_decoration->resize_width = resize_with;
    scene_decoration_update(scene_decoration, DECO_UPDATE_CAUSE_RESIZE_WIDTH);
}
