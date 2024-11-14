// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <pixman.h>
#include <stdlib.h>
#include <time.h>

#include <wlr/types/wlr_matrix.h>

#include <kywc/boxes.h>
#include <kywc/log.h>

#include "render/opengl.h"
#include "render/profile.h"
#include "util/debug.h"
#include "util/matrix.h"
#include "util/quirks.h"

#define MAX_QUADS 86 // 4kb

static const struct wlr_render_pass_impl render_pass_impl;

bool wlr_render_pass_is_opengl(struct wlr_render_pass *render_pass)
{
    return render_pass->impl == &render_pass_impl;
}

struct ky_opengl_render_pass *
ky_opengl_render_pass_from_wlr_render_pass(struct wlr_render_pass *wlr_pass)
{
    assert(wlr_pass->impl == &render_pass_impl);
    struct ky_opengl_render_pass *pass = wl_container_of(wlr_pass, pass, base);
    return pass;
}

static bool _render_pass_submit(struct wlr_render_pass *wlr_pass, uint32_t quirks)
{
    KY_PROFILE_ZONE(zone, __func__);

    struct ky_opengl_render_pass *pass = ky_opengl_render_pass_from_wlr_render_pass(wlr_pass);
    struct ky_opengl_renderer *renderer = pass->buffer->renderer;
    struct ky_opengl_render_timer *timer = pass->timer;

    ky_opengl_push_debug(renderer);

    if (timer) {
        // clear disjoint flag
        GLint64 disjoint;
        glGetInteger64v(GL_GPU_DISJOINT_EXT, &disjoint);
        // set up the query
        glQueryCounterEXT(timer->id, GL_TIMESTAMP_EXT);
        // get end-of-CPU-work time in GL time domain
        glGetInteger64v(GL_TIMESTAMP_EXT, &timer->gl_cpu_end);
        // get end-of-CPU-work time in CPU time domain
        clock_gettime(CLOCK_MONOTONIC, &timer->cpu_end);
    }

    if (quirks & QUIRKS_MASK_EXPLICIT_SYNC) {
        glFinish();
    } else {
        glFlush();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ky_opengl_pop_debug(renderer);
    ky_egl_restore_context(&pass->prev_ctx);

    wlr_buffer_unlock(pass->buffer->buffer);
    free(pass);

    KY_PROFILE_ZONE_END(zone);
    return true;
}

static bool render_pass_submit(struct wlr_render_pass *wlr_pass)
{
    return _render_pass_submit(wlr_pass, 0);
}

static void render(struct wlr_renderer *renderer, const struct wlr_box *box, const pixman_region32_t *clip, GLint attrib)
{
    pixman_region32_t region;
    pixman_region32_init_rect(&region, box->x, box->y, box->width, box->height);

    if (clip) {
        pixman_region32_intersect(&region, &region, clip);
    }

    int rects_len;
    const pixman_box32_t *rects = pixman_region32_rectangles(&region, &rects_len);
    if (rects_len == 0) {
        pixman_region32_fini(&region);
        return;
    }

    KY_PROFILE_RENDER_ZONE(renderer, gzone_render, __func__);
    glEnableVertexAttribArray(attrib);

    for (int i = 0; i < rects_len;) {
        int batch = rects_len - i < MAX_QUADS ? rects_len - i : MAX_QUADS;
        int batch_end = batch + i;

        size_t vert_index = 0;
        GLfloat verts[MAX_QUADS * 6 * 2];
        for (; i < batch_end; i++) {
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

        glVertexAttribPointer(attrib, 2, GL_FLOAT, GL_FALSE, 0, verts);
        glDrawArrays(GL_TRIANGLES, 0, batch * 6);
    }

    glDisableVertexAttribArray(attrib);
    KY_PROFILE_RENDER_ZONE_END(renderer);
    pixman_region32_fini(&region);
}

static void setup_blending(enum wlr_render_blend_mode mode)
{
    switch (mode) {
    case WLR_RENDER_BLEND_MODE_PREMULTIPLIED:
        glEnable(GL_BLEND);
        break;
    case WLR_RENDER_BLEND_MODE_NONE:
        glDisable(GL_BLEND);
        break;
    }
}

static void render_pass_add_texture(struct wlr_render_pass *wlr_pass,
                                    const struct wlr_render_texture_options *options)
{
    struct ky_render_texture_options ky_options = {
        .base = *options,
        .radius = { 0 },
        .repeated = false,
        .rotation_angle = 0,
    };
    ky_opengl_render_pass_add_texture(wlr_pass, &ky_options);
}

static void render_pass_add_rect(struct wlr_render_pass *wlr_pass,
                                 const struct wlr_render_rect_options *options)
{
    struct ky_render_rect_options ky_options = {
        .base = *options,
        .radius = { 0 },
        .rotation_angle = 0,
    };
    ky_opengl_render_pass_add_rect(wlr_pass, &ky_options);
}

static const struct wlr_render_pass_impl render_pass_impl = {
    .submit = render_pass_submit,
    .add_texture = render_pass_add_texture,
    .add_rect = render_pass_add_rect,
};

static bool ky_opengl_render_pass_submit(struct wlr_render_pass *wlr_pass, uint32_t quirks)
{
    return _render_pass_submit(wlr_pass, quirks);
}

void ky_opengl_render_pass_add_texture(struct wlr_render_pass *wlr_pass,
                                       const struct ky_render_texture_options *options)
{
    struct ky_opengl_render_pass *pass = ky_opengl_render_pass_from_wlr_render_pass(wlr_pass);
    const struct wlr_render_texture_options *wlr_options = &options->base;
    struct ky_opengl_texture *texture = ky_opengl_texture_from_wlr_texture(wlr_options->texture);
    struct ky_opengl_renderer *renderer = pass->buffer->renderer;
    struct wlr_buffer *target_buffer = pass->buffer->buffer;
    bool has_radius = ky_render_pass_options_has_radius(&options->radius);

    struct ky_opengl_tex_ex_shader *shader = NULL;
    switch (texture->target) {
    case GL_TEXTURE_2D:
        if (texture->has_alpha) {
            shader = has_radius ? &renderer->shaders.tex_rgba_ex : &renderer->shaders.tex_rgba;
        } else {
            shader = has_radius ? &renderer->shaders.tex_rgbx_ex : &renderer->shaders.tex_rgbx;
        }
        break;
    case GL_TEXTURE_EXTERNAL_OES:
        assert(renderer->exts.OES_egl_image_external);
        shader = has_radius ? &renderer->shaders.tex_ext_ex : &renderer->shaders.tex_ext;
        break;
    default:
        abort();
    }

    struct wlr_box dst_box;
    struct wlr_fbox src_fbox;
    wlr_render_texture_options_get_src_box(wlr_options, &src_fbox);
    wlr_render_texture_options_get_dst_box(wlr_options, &dst_box);
    float alpha = wlr_render_texture_options_get_alpha(wlr_options);

    uint32_t tex_w = wlr_options->texture->width;
    uint32_t tex_h = wlr_options->texture->height;
    struct kywc_fbox src_kywcbox = {
        .x = src_fbox.x / tex_w,
        .y = src_fbox.y / tex_h,
    };

    int width = dst_box.width;
    int height = dst_box.height;
    if (wlr_options->transform & WL_OUTPUT_TRANSFORM_90) {
        width = dst_box.height;
        height = dst_box.width;
    }
    if (options->repeated) {
        src_kywcbox.width = width / src_fbox.width;
        src_kywcbox.height = height / src_fbox.height;
    } else {
        src_kywcbox.width = src_fbox.width / tex_w;
        src_kywcbox.height = src_fbox.height / tex_h;
    }
    struct kywc_box dst_kywcbox = {
        .x = dst_box.x,
        .y = dst_box.y,
        .width = dst_box.width,
        .height = dst_box.height,
    };

    struct ky_mat3 uv2ndc;
    struct ky_mat3 uv2tex;
    ky_mat3_uvofbox_to_ndc(&uv2ndc, target_buffer->width, target_buffer->height,
                           options->rotation_angle, &dst_kywcbox);
    ky_mat3_uvofbox_to_texture(&uv2tex, options->base.transform, &src_kywcbox);

    ky_opengl_push_debug(renderer);
    KY_PROFILE_RENDER_ZONE(&renderer->wlr_renderer, gzone, __func__);

    if (has_radius) {
        // radius clip always need blend
        setup_blending(WLR_RENDER_BLEND_MODE_PREMULTIPLIED);
    } else {
        setup_blending(!texture->has_alpha && alpha == 1.0 ? WLR_RENDER_BLEND_MODE_NONE
                                                           : options->base.blend_mode);
    }

    glUseProgram(shader->program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(texture->target, texture->tex);

    if (options->repeated) {
        glTexParameteri(texture->target, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(texture->target, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else {
        glTexParameteri(texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    switch (wlr_options->filter_mode) {
    case WLR_SCALE_FILTER_BILINEAR:
        glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        break;
    case WLR_SCALE_FILTER_NEAREST:
        glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        break;
    }

    // vert shader param
    glUniform1i(shader->tex, 0);
    glUniform1f(shader->alpha, alpha);

    glUniformMatrix3fv(shader->uv2ndc, 1, GL_FALSE, uv2ndc.matrix);
    glUniformMatrix3fv(shader->uv2tex, 1, GL_FALSE, uv2tex.matrix);

    if (has_radius) {
        // avoid texture alpha < 1
        bool force_opaque = (!texture->has_alpha && alpha == 1.0) ||
                            options->base.blend_mode == WLR_RENDER_BLEND_MODE_NONE;
        glUniform1i(shader->force_opaque, force_opaque);
        glUniform1f(shader->aspect, width / (float)height);
        float half_height = height * 0.5f;
        float one_pixel_distance = 1.0f / half_height; // shader distance scale
        glUniform1f(shader->anti_aliasing, one_pixel_distance * 0.5f);
        glUniform4f(shader->round_corner_radius, options->radius.rb * one_pixel_distance,
                    options->radius.rt * one_pixel_distance,
                    options->radius.lb * one_pixel_distance,
                    options->radius.lt * one_pixel_distance);
    }
    render(&renderer->wlr_renderer, &dst_box, options->base.clip, shader->uv_attrib);

    glBindTexture(texture->target, 0);

    KY_PROFILE_RENDER_ZONE_END(&renderer->wlr_renderer);
    ky_opengl_pop_debug(renderer);
}

void ky_opengl_render_pass_add_rect(struct wlr_render_pass *wlr_pass,
                                    const struct ky_render_rect_options *options)
{
    struct ky_opengl_render_pass *pass = ky_opengl_render_pass_from_wlr_render_pass(wlr_pass);
    struct ky_opengl_renderer *renderer = pass->buffer->renderer;
    struct wlr_buffer *target_buffer = pass->buffer->buffer;
    bool has_radius = ky_render_pass_options_has_radius(&options->radius);
    struct ky_opengl_rect_ex_shader *shader;
    if (has_radius) {
        shader = &renderer->shaders.quad_ex;
    } else {
        shader = &renderer->shaders.quad;
    }
    const struct wlr_render_color *color = &options->base.color;
    struct wlr_box box;
    struct kywc_fbox src_uvbox = { 0.0, 0.0, 1.0, 1.0 };
    wlr_render_rect_options_get_box(&options->base, pass->buffer->buffer, &box);

    ky_opengl_push_debug(renderer);
    KY_PROFILE_RENDER_ZONE(&renderer->wlr_renderer, gzone, __func__);

    if (has_radius) {
        setup_blending(WLR_RENDER_BLEND_MODE_PREMULTIPLIED);
    } else {
        setup_blending(color->a == 1.0 ? WLR_RENDER_BLEND_MODE_NONE : options->base.blend_mode);
    }

    glUseProgram(shader->program);

    struct kywc_box dst_box = {
        .x = box.x,
        .y = box.y,
        .width = box.width,
        .height = box.height,
    };
    struct ky_mat3 uv2ndc, uv2tex;
    ky_mat3_uvofbox_to_ndc(&uv2ndc, target_buffer->width, target_buffer->height,
                           options->rotation_angle, &dst_box);
    ky_mat3_uvofbox_to_texture(&uv2tex, WL_OUTPUT_TRANSFORM_NORMAL, &src_uvbox);

    glUniformMatrix3fv(shader->uv2ndc, 1, GL_FALSE, uv2ndc.matrix);
    glUniformMatrix3fv(shader->uv2tex, 1, GL_FALSE, uv2tex.matrix);

    glUniform4f(shader->color, color->r, color->g, color->b, color->a);

    if (has_radius) {
        glUniform1f(shader->aspect, box.width / (float)box.height);
        float half_height = box.height * 0.5f;
        float one_pixel_distance = 1.0f / half_height; // shader distance scale
        glUniform1f(shader->anti_aliasing, one_pixel_distance * 0.5f);
        glUniform4f(shader->round_corner_radius, options->radius.rb * one_pixel_distance,
                    options->radius.rt * one_pixel_distance,
                    options->radius.lb * one_pixel_distance,
                    options->radius.lt * one_pixel_distance);
    }
    render(&renderer->wlr_renderer, &box, options->base.clip, shader->uv_attrib);

    KY_PROFILE_RENDER_ZONE_END(&renderer->wlr_renderer);
    ky_opengl_pop_debug(renderer);
}

static const struct ky_render_pass_impl ky_render_pass_impl = {
    .submit = ky_opengl_render_pass_submit,
    .add_texture = ky_opengl_render_pass_add_texture,
    .add_rect = ky_opengl_render_pass_add_rect,
};

static const char *reset_status_str(GLenum status)
{
    switch (status) {
    case GL_GUILTY_CONTEXT_RESET_KHR:
        return "guilty";
    case GL_INNOCENT_CONTEXT_RESET_KHR:
        return "innocent";
    case GL_UNKNOWN_CONTEXT_RESET_KHR:
        return "unknown";
    default:
        return "<invalid>";
    }
}

struct ky_opengl_render_pass *ky_opengl_begin_buffer_pass(struct ky_opengl_buffer *buffer,
                                                          struct ky_egl_context *prev_ctx,
                                                          struct ky_opengl_render_timer *timer)
{
    struct ky_opengl_renderer *renderer = buffer->renderer;
    struct wlr_buffer *wlr_buffer = buffer->buffer;

    if (renderer->exts.KHR_robustness) {
        GLenum status = glGetGraphicsResetStatusKHR();
        if (status != GL_NO_ERROR) {
            kywc_log(KYWC_ERROR, "GPU reset (%s)", reset_status_str(status));
            wl_signal_emit_mutable(&renderer->wlr_renderer.events.lost, NULL);
            return NULL;
        }
    }

    struct ky_opengl_render_pass *pass = calloc(1, sizeof(*pass));
    if (pass == NULL) {
        return NULL;
    }

    pass->impl = &ky_render_pass_impl;
    wlr_render_pass_init(&pass->base, &render_pass_impl);
    pass->renderer = renderer;
    wlr_buffer_lock(wlr_buffer);
    pass->buffer = buffer;
    pass->timer = timer;
    pass->prev_ctx = *prev_ctx;

    ky_opengl_matrix_projection(pass->projection_matrix, wlr_buffer->width, wlr_buffer->height,
                                WL_OUTPUT_TRANSFORM_FLIPPED_180);

    ky_opengl_push_debug(renderer);
    glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);

    glViewport(0, 0, wlr_buffer->width, wlr_buffer->height);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_SCISSOR_TEST);
    ky_opengl_pop_debug(renderer);

    return pass;
}
