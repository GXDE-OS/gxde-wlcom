// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <cglm/affine.h>
#include <cglm/cam.h>
#include <pixman.h>

#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "kywc/kycom/scene.h"
#include "scene/kycom/opengl_p.h"

void kywc_target_init_output_target(struct kywc_render_target *target,
                                    struct kywc_scene_output *scene_output)
{
    struct wlr_output *output = scene_output->output;

    target->buffer.fb = kywc_gl_get_current_framebuffer();
    target->current_ofb = target->buffer.fb;
    target->scale = output->scale;
    target->wl_transform = output->transform;
    target->buffer.height = output->height;
    target->buffer.width = output->width;
    target->view_box.x = scene_output->x;
    target->view_box.y = scene_output->y;
    target->lx = 0;
    target->ly = 0;
    wlr_output_effective_resolution(output, &target->view_box.width, &target->view_box.height);
}

void kywc_target_cpy(struct kywc_render_target *dst, const struct kywc_render_target *src)
{
    if (!dst || !src) {
        return;
    }

    dst->buffer.fb = src->buffer.fb;
    dst->current_ofb = src->current_ofb;
    dst->scale = src->scale;
    dst->wl_transform = src->wl_transform;
    dst->buffer.height = src->buffer.height;
    dst->buffer.width = src->buffer.width;
    dst->lx = src->lx;
    dst->ly = src->ly;

    memcpy(&dst->view_box, &src->view_box, sizeof(dst->view_box));
}

void kywc_target_cpy_offset(struct kywc_render_target *dst, const struct kywc_render_target *src,
                            int x, int y)
{
    kywc_target_cpy(dst, src);
    kywc_target_offset(dst, x, y);
}

void kywc_target_offset(struct kywc_render_target *target, int x, int y)
{
    target->view_box.x -= x;
    target->view_box.y -= y;

    target->lx += x;
    target->ly += y;
}

void kywc_target_offset_revert(struct kywc_render_target *target, int x, int y)
{
    kywc_target_offset(target, -x, -y);
}

void kywc_target_render_begin(struct kywc_render_target *target)
{
    _kywc_gl_begin(target->buffer.width, target->buffer.height, target->buffer.fb);
}

void kywc_target_render_end(struct kywc_render_target *target)
{
    _kywc_gl_end(target->current_ofb);
}

void kywc_target_update_ofb(struct kywc_render_target *target)
{
    if (target) {
        target->current_ofb = kywc_gl_get_current_framebuffer();
    }
}

void kywc_target_update(struct kywc_render_target *target, int x, int y,
                        int width, int height)
{
    if (!target) {
        return;
    }

    kywc_gl_begin();
    int ofb = kywc_gl_get_current_framebuffer();
    struct kywc_gl_buffer *buffer = &target->buffer;
    kywc_gl_buffer_allocate(buffer, ofb, width, height);
    kywc_gl_end();

    target->lx = 0;
    target->ly = 0;
    target->scale = 1.0;
    target->current_ofb = ofb;
    target->wl_transform = 0;
    target->view_box.x = x;
    target->view_box.y = y;
    target->view_box.width = width;
    target->view_box.height = height;
}

static void get_output_mat4_from_transform(enum wl_output_transform transform,
                                           mat4 transform_matrix)
{
    mat4 flip_matrix;
    vec3 scale_vec3 = { -1, 1, 1 };
    vec3 z_vec3 = { 0, 0, 1 };

    glm_mat4_identity(flip_matrix);
    /* All _flipped transforms have values (regular_transfrom + 4). */
    if (transform >= 4) {
        glm_scale(flip_matrix, scale_vec3);
    }

    /* Remove the third bit if it's set. */
    uint32_t rotation = transform & (~4);
    mat4 rotation_matrix;
    glm_mat4_identity(rotation_matrix);
    switch (rotation) {
    case WL_OUTPUT_TRANSFORM_90:
        glm_rotate(rotation_matrix, glm_rad(-90.0f), z_vec3);
        break;
    case WL_OUTPUT_TRANSFORM_180:
        glm_rotate(rotation_matrix, glm_rad(-180.0f), z_vec3);
        break;
    case WL_OUTPUT_TRANSFORM_270:
        glm_rotate(rotation_matrix, glm_rad(-270.0f), z_vec3);
        break;
    default:
        break;
    }
    glm_mat4_mul(rotation_matrix, flip_matrix, transform_matrix);
}

bool kywc_target_get_transform_mat4(struct kywc_render_target *target, mat4 transform_matrix)
{
    if (!target) {
        return false;
    }

    get_output_mat4_from_transform(target->wl_transform, transform_matrix);
    return true;
}

void kywc_target_get_framebuffer_box(struct kywc_render_target *target,
                                     const struct wlr_box *logic_box,
                                     struct wlr_box *framebuffer_box)
{
    if (!target || !logic_box || !framebuffer_box) {
        return;
    }
    /* Framebuffer origion offset. */
    struct wlr_box src = {
        .x = logic_box->x - target->view_box.x,
        .y = logic_box->y - target->view_box.y,
        .width = logic_box->width,
        .height = logic_box->height,
    };

    kywc_box_scale_xy(&src, framebuffer_box, target->scale, target->scale);

    int width = target->buffer.width;
    int height = target->buffer.height;

    if (target->wl_transform & 1) {
        width = target->buffer.height;
        height = target->buffer.width;
    }

    struct wlr_box result;
    enum wl_output_transform transform = wlr_output_transform_invert(target->wl_transform);

    wlr_box_transform(&result, framebuffer_box, transform, width, height);
    memcpy(framebuffer_box, &result, sizeof(*framebuffer_box));
}

void kywc_target_get_frame_region(struct kywc_render_target *target,
                                  const pixman_region32_t *logic_region,
                                  pixman_region32_t *framebuffer_region)
{
    int nrects;
    struct wlr_box logic_box, frame_box;
    pixman_box32_t *rects = pixman_region32_rectangles(logic_region, &nrects);
    for (int i = 0; i < nrects; ++i) {
        logic_box.x = rects[i].x1;
        logic_box.y = rects[i].y1;
        logic_box.width = rects[i].x2 - rects[i].x1;
        logic_box.height = rects[i].y2 - rects[i].y1;

        kywc_target_get_framebuffer_box(target, &logic_box, &frame_box);
        pixman_region32_union_rect(framebuffer_region, framebuffer_region, frame_box.x, frame_box.y,
                                   frame_box.width, frame_box.height);
    }
}

void kywc_target_cpy_framebuffer_box(struct kywc_gl_buffer *dst, struct kywc_render_target *src,
                                     struct wlr_box *cpy_box)
{
    struct wlr_box dst_box = {
        .x = 0,
        .y = 0,
        .width = cpy_box->width,
        .height = cpy_box->height,
    };

    kywc_gl_buffer_blit_box(dst, &src->buffer, cpy_box, &dst_box);
}

void kywc_target_get_orth(struct kywc_render_target *framebuffer, mat4 transform_mat4,
                          mat4 ortho_proj_mat4)
{
    glm_ortho(framebuffer->view_box.x, framebuffer->view_box.x + framebuffer->view_box.width,
              framebuffer->view_box.y, framebuffer->view_box.y + framebuffer->view_box.height,
              -1.0f, 1.0f, ortho_proj_mat4);

    glm_mat4_mul(transform_mat4, ortho_proj_mat4, ortho_proj_mat4);
}

static void target_logic_scissor(struct kywc_render_target *target, struct wlr_box *geometry_damage)
{
    if (!geometry_damage) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    struct wlr_box frame_scissor;
    kywc_target_get_framebuffer_box(target, geometry_damage, &frame_scissor);
    glScissor(frame_scissor.x, frame_scissor.y, frame_scissor.width, frame_scissor.height);
}

void kywc_target_logic_damage_clear(struct kywc_render_target *target,
                                    pixman_region32_t *logic_damage)
{
    /* Clear damage region in the framebuffer coordinate system. */
    int nrects;
    struct wlr_box scissor_box;
    pixman_box32_t *rects = pixman_region32_rectangles(logic_damage, &nrects);
    for (int i = 0; i < nrects; ++i) {
        scissor_box.x = rects[i].x1;
        scissor_box.y = rects[i].y1;
        scissor_box.width = rects[i].x2 - rects[i].x1;
        scissor_box.height = rects[i].y2 - rects[i].y1;
        target_logic_scissor(target, &scissor_box);
        vec4 color = { 0.f, 0.f, 0.f, 0.f };
        kywc_gl_clear(color);
    }
    target_logic_scissor(target, NULL);
}

void kywc_target_draw_damage(struct kywc_render_target *target, struct kywc_gl_texture *tex,
                             pixman_region32_t *damage)
{
    if (!damage) {
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        return;
    }
    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);
    kywc_target_get_frame_region(target, damage, &frame_damage);
    glEnable(GL_SCISSOR_TEST);
    struct wlr_box frame_scissor;
    int nrects;
    pixman_box32_t *rects = pixman_region32_rectangles(&frame_damage, &nrects);

    for (int i = 0; i < nrects; ++i) {
        frame_scissor.x = rects[i].x1;
        frame_scissor.y = rects[i].y1;
        frame_scissor.width = rects[i].x2 - rects[i].x1;
        frame_scissor.height = rects[i].y2 - rects[i].y1;
        glScissor(frame_scissor.x, frame_scissor.y, frame_scissor.width, frame_scissor.height);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    glDisable(GL_SCISSOR_TEST);
    pixman_region32_fini(&frame_damage);
}

void kywc_target_render_rectangle(struct kywc_render_target *target, struct wlr_box *geometry,
                                  vec4 color, uint32_t cache_flag)
{
    mat4 transform_mat4, ortho_proj_mat4;
    kywc_target_get_transform_mat4(target, transform_mat4);
    kywc_target_get_orth(target, transform_mat4, ortho_proj_mat4);
    kywc_gl_render_rectangle(geometry, color, ortho_proj_mat4, cache_flag);
}

void kywc_target_render_texture(struct kywc_gl_texture *texture,
                                struct kywc_render_target *framebuffer,
                                const struct kywc_gl_geometry *geometry, vec4 color,
                                uint32_t cache_flag)
{
    mat4 transform_mat4, ortho_proj_mat4;
    kywc_target_get_transform_mat4(framebuffer, transform_mat4);
    kywc_target_get_orth(framebuffer, transform_mat4, ortho_proj_mat4);
    struct kywc_gl_geometry texture_coord = {
        .x1 = 0,
        .y1 = 0,
        .x2 = 1.0f,
        .y2 = 1.0f,
    };
    /* invert_y */
    texture_coord.y1 = 1.0f - texture_coord.y1;
    texture_coord.y2 = 1.0f - texture_coord.y2;
    kywc_gl_render_texture(texture, geometry, &texture_coord,
                           ortho_proj_mat4, color, cache_flag);
}

static void get_transformed_tex_vertex(enum kywc_gl_transform transform,
                                       GLfloat transformed_vertex[8], GLfloat normal_vertex[8])
{
    int start_i = 0;
    switch (transform) {
    case TRANSFORM_NORMAL:
        /**
         * Start at texture left_bottom.
         * Counter-clockwise direction.
         * GLfloat vertexData[] = {
         *     x1, y1,
         *     x2, y1,
         *     x2, y2,
         *     x1, y2,
         * };
         */
        break;
    case TRANSFORM_90:
        /**
         * Start at texture right_bottom.
         * Counter-clockwise direction.
         * GLfloat vertexData[] = {
         *     x2, y1,
         *     x2, y2,
         *     x1, y2,
         *     x1, y1,
         * };
         */
        start_i = 2;
        break;
    case TTRANSFORM_180:
        start_i = 4;
        break;
    case TRANSFORM_270:
        start_i = 6;
        break;
    case TRANSFORM_FLIPPED:
        start_i = 0;
        break;
    case TRANSFORM_FLIPPED_90:
        start_i = 2;
        break;
    case TRANSFORM_FLIPPED_180:
        start_i = 4;
        break;
    case TRANSFORM_FLIPPED_270:
        start_i = 6;
        break;
    default:
        break;
    }
    for (int i = start_i; i < 8 + start_i; i++) {
        transformed_vertex[i - start_i] = normal_vertex[i % 8];
    }

    if (transform > TRANSFORM_270) {
        for (int i = 0; i < 4; i++) {
            transformed_vertex[i * 2] = 1 - transformed_vertex[i * 2];
        }
    }
}

void kywc_target_render_texture_with_transform(struct kywc_gl_texture *texture,
                                               enum kywc_gl_transform transform,
                                               struct kywc_render_target *framebuffer,
                                               const struct kywc_gl_geometry *geometry, vec4 color,
                                               uint32_t cache_flag)
{
    mat4 transform_mat4, ortho_proj_mat4;
    kywc_target_get_transform_mat4(framebuffer, transform_mat4);
    kywc_target_get_orth(framebuffer, transform_mat4, ortho_proj_mat4);
    GLfloat pos_vertex[8];
    /**
     * Geometry vertex
     * Start at geometry left_bottom.
     * Counter-clockwise direction.
     * GLfloat vertexData[] = {
     *     x1, y2,
     *     x2, y2,
     *     x2, y1,
     *     x1, y1,
     * };
     */
    pos_vertex[0] = geometry->x1, pos_vertex[1] = geometry->y2;
    pos_vertex[2] = geometry->x2, pos_vertex[3] = geometry->y2;
    pos_vertex[4] = geometry->x2, pos_vertex[5] = geometry->y1;
    pos_vertex[6] = geometry->x1, pos_vertex[7] = geometry->y1;

    GLfloat tex_vertex[8];
    struct kywc_gl_geometry texture_coord;
    texture_coord.x1 = 0.0f;
    texture_coord.y1 = 0.0f;
    texture_coord.x2 = 1.0f;
    texture_coord.y2 = 1.0f;
    /* invert_y */
    texture_coord.y1 = 1.0f - texture_coord.y1;
    texture_coord.y2 = 1.0f - texture_coord.y2;
    /* NORMAL */
    /**
     * Texture vertex
     * Start at texture left_bottom.
     * Counter-clockwise direction.
     * GLfloat vertexData[] = {
     *     x1, y1,
     *     x2, y1,
     *     x2, y2,
     *     x1, y2,
     * };
     */
    tex_vertex[0] = texture_coord.x1, tex_vertex[1] = texture_coord.y1;
    tex_vertex[2] = texture_coord.x2, tex_vertex[3] = texture_coord.y1;
    tex_vertex[4] = texture_coord.x2, tex_vertex[5] = texture_coord.y2;
    tex_vertex[6] = texture_coord.x1, tex_vertex[7] = texture_coord.y2;

    GLfloat transformed_tex_vertex[8];
    get_transformed_tex_vertex(transform, transformed_tex_vertex, tex_vertex);

    kywc_gl_render_transformed_texture(texture, pos_vertex, transformed_tex_vertex, ortho_proj_mat4,
                                       color, cache_flag);
}
