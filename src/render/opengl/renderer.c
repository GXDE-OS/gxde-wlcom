// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <kywc/log.h>
#include <wlr/types/wlr_matrix.h>

#include "render/opengl.h"

#include "common_vert_str.h"
#include "quad_frag_str.h"
#include "tex_external_frag_str.h"
#include "tex_rgba_frag_str.h"
#include "tex_rgbx_frag_str.h"

static const float transforms[][9] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = {
		1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_90] = {
		0.0f, 1.0f, 0.0f,
		-1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_180] = {
		-1.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_270] = {
		0.0f, -1.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED] = {
		-1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = {
		0.0f, 1.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = {
		1.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = {
		0.0f, -1.0f, 0.0f,
		-1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
	},
};

void ky_opengl_matrix_projection(float mat[static 9], int width, int height,
                                 enum wl_output_transform transform)
{
    memset(mat, 0, sizeof(*mat) * 9);

    const float *t = transforms[transform];
    float x = 2.0f / width;
    float y = 2.0f / height;

    // Rotation + reflection
    mat[0] = x * t[0];
    mat[1] = x * t[1];
    mat[3] = y * -t[3];
    mat[4] = y * -t[4];

    // Translation
    mat[2] = -copysign(1.0f, mat[0] + mat[1]);
    mat[5] = -copysign(1.0f, mat[3] + mat[4]);

    // Identity
    mat[8] = 1.0f;
}

static const GLfloat verts[] = {
    1, 0, // top right
    0, 0, // top left
    1, 1, // bottom right
    0, 1, // bottom left
};

static const struct wlr_renderer_impl renderer_impl;
static const struct wlr_render_timer_impl render_timer_impl;

bool wlr_renderer_is_opengl(struct wlr_renderer *wlr_renderer)
{
    return wlr_renderer->impl == &renderer_impl;
}

struct ky_opengl_renderer *ky_opengl_renderer_from_wlr_renderer(struct wlr_renderer *wlr_renderer)
{
    assert(wlr_renderer->impl == &renderer_impl);
    struct ky_opengl_renderer *renderer = wl_container_of(wlr_renderer, renderer, wlr_renderer);
    return renderer;
}

static struct ky_opengl_renderer *gl_get_renderer_in_context(struct wlr_renderer *wlr_renderer)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);
    assert(ky_egl_is_current(renderer->egl));
    assert(renderer->current_buffer != NULL);
    return renderer;
}

static struct ky_opengl_render_timer *gl_get_render_timer(struct wlr_render_timer *wlr_timer)
{
    assert(wlr_timer->impl == &render_timer_impl);
    struct ky_opengl_render_timer *timer = wl_container_of(wlr_timer, timer, base);
    return timer;
}

static void destroy_buffer(struct ky_opengl_buffer *buffer)
{
    wl_list_remove(&buffer->link);
    wlr_addon_finish(&buffer->addon);

    struct ky_egl_context prev_ctx;
    ky_egl_save_context(&prev_ctx);
    ky_egl_make_current(buffer->renderer->egl);

    ky_opengl_push_debug(buffer->renderer);

    glDeleteFramebuffers(1, &buffer->fbo);
    glDeleteRenderbuffers(1, &buffer->rbo);

    ky_opengl_pop_debug(buffer->renderer);

    ky_egl_destroy_image(buffer->renderer->egl, buffer->image);

    ky_egl_restore_context(&prev_ctx);

    free(buffer);
}

static void handle_buffer_destroy(struct wlr_addon *addon)
{
    struct ky_opengl_buffer *buffer = wl_container_of(addon, buffer, addon);
    destroy_buffer(buffer);
}

static const struct wlr_addon_interface buffer_addon_impl = {
    .name = "ky_opengl_buffer",
    .destroy = handle_buffer_destroy,
};

static struct ky_opengl_buffer *get_or_create_buffer(struct ky_opengl_renderer *renderer,
                                                     struct wlr_buffer *wlr_buffer)
{
    struct wlr_addon *addon = wlr_addon_find(&wlr_buffer->addons, renderer, &buffer_addon_impl);
    if (addon) {
        struct ky_opengl_buffer *buffer = wl_container_of(addon, buffer, addon);
        return buffer;
    }

    struct ky_opengl_buffer *buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        kywc_log_errno(KYWC_ERROR, "Allocation failed");
        return NULL;
    }
    buffer->buffer = wlr_buffer;
    buffer->renderer = renderer;

    struct wlr_dmabuf_attributes dmabuf = { 0 };
    if (!wlr_buffer_get_dmabuf(wlr_buffer, &dmabuf)) {
        goto error_buffer;
    }

    bool external_only;
    buffer->image = ky_egl_create_image_from_dmabuf(renderer->egl, &dmabuf, &external_only);
    if (buffer->image == EGL_NO_IMAGE_KHR) {
        goto error_buffer;
    }

    ky_opengl_push_debug(renderer);

    glGenRenderbuffers(1, &buffer->rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, buffer->rbo);
    glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, buffer->image);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &buffer->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, buffer->rbo);
    GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ky_opengl_pop_debug(renderer);

    if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
        kywc_log(KYWC_ERROR, "Failed to create FBO");
        goto error_image;
    }

    wlr_addon_init(&buffer->addon, &wlr_buffer->addons, renderer, &buffer_addon_impl);

    wl_list_insert(&renderer->buffers, &buffer->link);

    kywc_log(KYWC_DEBUG, "Created GL FBO for buffer %dx%d", wlr_buffer->width, wlr_buffer->height);

    return buffer;

error_image:
    ky_egl_destroy_image(renderer->egl, buffer->image);
error_buffer:
    free(buffer);
    return NULL;
}

static bool gl_bind_buffer(struct wlr_renderer *wlr_renderer, struct wlr_buffer *wlr_buffer)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);

    if (renderer->current_buffer != NULL) {
        assert(ky_egl_is_current(renderer->egl));

        ky_opengl_push_debug(renderer);
        glFlush();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ky_opengl_pop_debug(renderer);

        wlr_buffer_unlock(renderer->current_buffer->buffer);
        renderer->current_buffer = NULL;
    }

    if (wlr_buffer == NULL) {
        ky_egl_unset_current(renderer->egl);
        return true;
    }

    ky_egl_make_current(renderer->egl);

    struct ky_opengl_buffer *buffer = get_or_create_buffer(renderer, wlr_buffer);
    if (buffer == NULL) {
        return false;
    }

    wlr_buffer_lock(wlr_buffer);
    renderer->current_buffer = buffer;

    ky_opengl_push_debug(renderer);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->current_buffer->fbo);
    ky_opengl_pop_debug(renderer);

    return true;
}

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

static bool gl_begin(struct wlr_renderer *wlr_renderer, uint32_t width, uint32_t height)
{
    struct ky_opengl_renderer *renderer = gl_get_renderer_in_context(wlr_renderer);

    ky_opengl_push_debug(renderer);

    if (renderer->exts.KHR_robustness) {
        GLenum status = glGetGraphicsResetStatusKHR();
        if (status != GL_NO_ERROR) {
            kywc_log(KYWC_ERROR, "GPU reset (%s)", reset_status_str(status));
            wl_signal_emit_mutable(&wlr_renderer->events.lost, NULL);
            return false;
        }
    }

    glViewport(0, 0, width, height);
    renderer->viewport_width = width;
    renderer->viewport_height = height;

    // refresh projection matrix
    ky_opengl_matrix_projection(renderer->projection, width, height,
                                WL_OUTPUT_TRANSFORM_FLIPPED_180);

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // XXX: maybe we should save output projection and remove some of the need
    // for users to sling matricies themselves

    ky_opengl_pop_debug(renderer);

    return true;
}

static void gl_end(struct wlr_renderer *wlr_renderer)
{
    gl_get_renderer_in_context(wlr_renderer);
    // no-op
}

static void gl_clear(struct wlr_renderer *wlr_renderer, const float color[static 4])
{
    struct ky_opengl_renderer *renderer = gl_get_renderer_in_context(wlr_renderer);

    ky_opengl_push_debug(renderer);
    glClearColor(color[0], color[1], color[2], color[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    ky_opengl_pop_debug(renderer);
}

static void gl_scissor(struct wlr_renderer *wlr_renderer, struct wlr_box *box)
{
    struct ky_opengl_renderer *renderer = gl_get_renderer_in_context(wlr_renderer);

    ky_opengl_push_debug(renderer);
    if (box != NULL) {
        glScissor(box->x, box->y, box->width, box->height);
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    ky_opengl_pop_debug(renderer);
}

static bool gl_render_subtexture_with_matrix(struct wlr_renderer *wlr_renderer,
                                             struct wlr_texture *wlr_texture,
                                             const struct wlr_fbox *box,
                                             const float matrix[static 9], float alpha)
{
    struct ky_opengl_renderer *renderer = gl_get_renderer_in_context(wlr_renderer);
    struct ky_opengl_texture *texture = ky_opengl_texture_from_wlr_texture(wlr_texture);
    assert(texture->renderer == renderer);

    struct ky_opengl_tex_shader *shader = NULL;

    switch (texture->target) {
    case GL_TEXTURE_2D:
        if (texture->has_alpha) {
            shader = &renderer->shaders.tex_rgba;
        } else {
            shader = &renderer->shaders.tex_rgbx;
        }
        break;
    case GL_TEXTURE_EXTERNAL_OES:
        // EGL_EXT_image_dma_buf_import_modifiers requires
        // GL_OES_EGL_image_external
        assert(renderer->exts.OES_egl_image_external);
        shader = &renderer->shaders.tex_ext;
        break;
    default:
        abort();
    }

    float gl_matrix[9];
    wlr_matrix_multiply(gl_matrix, renderer->projection, matrix);

    ky_opengl_push_debug(renderer);

    if (!texture->has_alpha && alpha == 1.0) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(texture->target, texture->tex);

    glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glUseProgram(shader->program);

    glUniformMatrix3fv(shader->proj, 1, GL_FALSE, gl_matrix);
    glUniform1i(shader->tex, 0);
    glUniform1f(shader->alpha, alpha);

    float tex_matrix[9];
    wlr_matrix_identity(tex_matrix);
    wlr_matrix_translate(tex_matrix, box->x / texture->wlr_texture.width,
                         box->y / texture->wlr_texture.height);
    wlr_matrix_scale(tex_matrix, box->width / texture->wlr_texture.width,
                     box->height / texture->wlr_texture.height);
    glUniformMatrix3fv(shader->tex_proj, 1, GL_FALSE, tex_matrix);

    glVertexAttribPointer(shader->pos_attrib, 2, GL_FLOAT, GL_FALSE, 0, verts);

    glEnableVertexAttribArray(shader->pos_attrib);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(shader->pos_attrib);

    glBindTexture(texture->target, 0);

    ky_opengl_pop_debug(renderer);
    return true;
}

static void gl_render_quad_with_matrix(struct wlr_renderer *wlr_renderer,
                                       const float color[static 4], const float matrix[static 9])
{
    struct ky_opengl_renderer *renderer = gl_get_renderer_in_context(wlr_renderer);

    float gl_matrix[9];
    wlr_matrix_multiply(gl_matrix, renderer->projection, matrix);

    ky_opengl_push_debug(renderer);

    if (color[3] == 1.0) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
    }

    glUseProgram(renderer->shaders.quad.program);

    glUniformMatrix3fv(renderer->shaders.quad.proj, 1, GL_FALSE, gl_matrix);
    glUniform4f(renderer->shaders.quad.color, color[0], color[1], color[2], color[3]);

    glVertexAttribPointer(renderer->shaders.quad.pos_attrib, 2, GL_FLOAT, GL_FALSE, 0, verts);

    glEnableVertexAttribArray(renderer->shaders.quad.pos_attrib);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(renderer->shaders.quad.pos_attrib);

    ky_opengl_pop_debug(renderer);
}

static const uint32_t *gl_get_shm_texture_formats(struct wlr_renderer *wlr_renderer, size_t *len)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);
    return ky_opengl_get_shm_formats(renderer, len);
}

static const struct wlr_drm_format_set *
gl_get_dmabuf_texture_formats(struct wlr_renderer *wlr_renderer)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);
    return &renderer->egl->dmabuf_texture_formats;
}

static const struct wlr_drm_format_set *gl_get_render_formats(struct wlr_renderer *wlr_renderer)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);
    return &renderer->egl->dmabuf_render_formats;
}

static uint32_t gl_preferred_read_format(struct wlr_renderer *wlr_renderer)
{
    struct ky_opengl_renderer *renderer = gl_get_renderer_in_context(wlr_renderer);

    ky_opengl_push_debug(renderer);

    GLint gl_format = -1, gl_type = -1, alpha_size = -1;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &gl_format);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &gl_type);
    glGetIntegerv(GL_ALPHA_BITS, &alpha_size);

    ky_opengl_pop_debug(renderer);

    const struct ky_opengl_pixel_format *fmt =
        ky_opengl_pixel_format_from_gl(gl_format, gl_type, alpha_size > 0);
    if (fmt != NULL) {
        return fmt->drm_format;
    }

    if (renderer->exts.EXT_read_format_bgra) {
        return DRM_FORMAT_XRGB8888;
    }
    return DRM_FORMAT_XBGR8888;
}

static bool gl_read_pixels(struct wlr_renderer *wlr_renderer, uint32_t drm_format, uint32_t stride,
                           uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
                           uint32_t dst_x, uint32_t dst_y, void *data)
{
    struct ky_opengl_renderer *renderer = gl_get_renderer_in_context(wlr_renderer);

    const struct ky_opengl_pixel_format *fmt = ky_opengl_pixel_format_from_drm(drm_format);
    if (fmt == NULL || !ky_opengl_pixel_format_is_supported(renderer, fmt)) {
        kywc_log(KYWC_ERROR, "Cannot read pixels: unsupported pixel format 0x%" PRIX32, drm_format);
        return false;
    }

    if (fmt->gl_format == GL_BGRA_EXT && !renderer->exts.EXT_read_format_bgra) {
        kywc_log(KYWC_ERROR, "Cannot read pixels: missing GL_EXT_read_format_bgra extension");
        return false;
    }

    if (ky_opengl_pixel_format_pixels_per_block(fmt) != 1) {
        kywc_log(KYWC_ERROR, "Cannot read pixels: block formats are not supported");
        return false;
    }

    ky_opengl_push_debug(renderer);

    // Make sure any pending drawing is finished before we try to read it
    glFinish();

    glGetError(); // Clear the error flag

    unsigned char *p = (unsigned char *)data + dst_y * stride;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    uint32_t pack_stride = ky_opengl_pixel_format_min_stride(fmt, width);
    if (pack_stride == stride && dst_x == 0) {
        // Under these particular conditions, we can read the pixels with only
        // one glReadPixels call

        glReadPixels(src_x, src_y, width, height, fmt->gl_format, fmt->gl_type, p);
    } else {
        // Unfortunately GLES2 doesn't support GL_PACK_ROW_LENGTH, so we have to read
        // the lines out row by row
        for (size_t i = 0; i < height; ++i) {
            uint32_t y = src_y + i;
            glReadPixels(src_x, y, width, 1, fmt->gl_format, fmt->gl_type,
                         p + i * stride + dst_x * fmt->bytes_per_block);
        }
    }

    ky_opengl_pop_debug(renderer);

    return glGetError() == GL_NO_ERROR;
}

static int gl_get_drm_fd(struct wlr_renderer *wlr_renderer)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);

    if (renderer->drm_fd < 0) {
        renderer->drm_fd = ky_egl_dup_drm_fd(renderer->egl);
    }

    return renderer->drm_fd;
}

static uint32_t gl_get_render_buffer_caps(struct wlr_renderer *wlr_renderer)
{
    return WLR_BUFFER_CAP_DMABUF;
}

struct ky_egl *ky_opengl_renderer_get_egl(struct wlr_renderer *wlr_renderer)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);
    return renderer->egl;
}

static void gl_destroy(struct wlr_renderer *wlr_renderer)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);

    ky_egl_make_current(renderer->egl);

    struct ky_opengl_buffer *buffer, *buffer_tmp;
    wl_list_for_each_safe(buffer, buffer_tmp, &renderer->buffers, link) {
        destroy_buffer(buffer);
    }

    struct ky_opengl_texture *tex, *tex_tmp;
    wl_list_for_each_safe(tex, tex_tmp, &renderer->textures, link) {
        ky_opengl_texture_destroy(tex);
    }

    ky_opengl_push_debug(renderer);
    glDeleteProgram(renderer->shaders.quad.program);
    glDeleteProgram(renderer->shaders.tex_rgba.program);
    glDeleteProgram(renderer->shaders.tex_rgbx.program);
    glDeleteProgram(renderer->shaders.tex_ext.program);
    ky_opengl_pop_debug(renderer);

    if (renderer->exts.KHR_debug) {
        glDisable(GL_DEBUG_OUTPUT_KHR);
        glDebugMessageCallbackKHR(NULL, NULL);
    }

    ky_egl_unset_current(renderer->egl);
    ky_egl_destroy(renderer->egl);

    if (renderer->drm_fd >= 0) {
        close(renderer->drm_fd);
    }

    free(renderer);
}

static struct wlr_render_pass *gl_begin_buffer_pass(struct wlr_renderer *wlr_renderer,
                                                    struct wlr_buffer *wlr_buffer,
                                                    const struct wlr_buffer_pass_options *options)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);
    if (!ky_egl_make_current(renderer->egl)) {
        return NULL;
    }

    struct ky_opengl_render_timer *timer = NULL;
    if (options->timer) {
        timer = gl_get_render_timer(options->timer);
        clock_gettime(CLOCK_MONOTONIC, &timer->cpu_start);
    }

    struct ky_opengl_buffer *buffer = get_or_create_buffer(renderer, wlr_buffer);
    if (!buffer) {
        return NULL;
    }

    struct ky_opengl_render_pass *pass = ky_opengl_begin_buffer_pass(buffer, timer);
    if (!pass) {
        return NULL;
    }
    return &pass->base;
}

static struct wlr_render_timer *gl_render_timer_create(struct wlr_renderer *wlr_renderer)
{
    struct ky_opengl_renderer *renderer = ky_opengl_renderer_from_wlr_renderer(wlr_renderer);
    if (!renderer->exts.EXT_disjoint_timer_query) {
        kywc_log(KYWC_ERROR, "can't create timer, EXT_disjoint_timer_query not available");
        return NULL;
    }

    struct ky_opengl_render_timer *timer = calloc(1, sizeof(*timer));
    if (!timer) {
        return NULL;
    }
    timer->base.impl = &render_timer_impl;
    timer->renderer = renderer;

    struct ky_egl_context prev_ctx;
    ky_egl_save_context(&prev_ctx);
    ky_egl_make_current(renderer->egl);
    glGenQueriesEXT(1, &timer->id);
    ky_egl_restore_context(&prev_ctx);

    return &timer->base;
}

static int64_t timespec_to_nsec(const struct timespec *a)
{
    return (int64_t)a->tv_sec * 1000000000 + a->tv_nsec;
}

static int gl_get_render_time(struct wlr_render_timer *wlr_timer)
{
    struct ky_opengl_render_timer *timer = gl_get_render_timer(wlr_timer);
    struct ky_opengl_renderer *renderer = timer->renderer;

    struct ky_egl_context prev_ctx;
    ky_egl_save_context(&prev_ctx);
    ky_egl_make_current(renderer->egl);

    GLint64 disjoint;
    glGetInteger64v(GL_GPU_DISJOINT_EXT, &disjoint);
    if (disjoint) {
        kywc_log(KYWC_ERROR, "a disjoint operation occurred and the render timer is invalid");
        ky_egl_restore_context(&prev_ctx);
        return -1;
    }

    GLint available;
    glGetQueryObjectivEXT(timer->id, GL_QUERY_RESULT_AVAILABLE_EXT, &available);
    if (!available) {
        kywc_log(KYWC_ERROR, "timer was read too early, gpu isn't done!");
        ky_egl_restore_context(&prev_ctx);
        return -1;
    }

    GLuint64 gl_render_end;
    glGetQueryObjectui64vEXT(timer->id, GL_QUERY_RESULT_EXT, &gl_render_end);

    int64_t cpu_nsec_total =
        timespec_to_nsec(&timer->cpu_end) - timespec_to_nsec(&timer->cpu_start);

    ky_egl_restore_context(&prev_ctx);
    return gl_render_end - timer->gl_cpu_end + cpu_nsec_total;
}

static void gl_render_timer_destroy(struct wlr_render_timer *wlr_timer)
{
    struct ky_opengl_render_timer *timer = wl_container_of(wlr_timer, timer, base);
    struct ky_opengl_renderer *renderer = timer->renderer;

    struct ky_egl_context prev_ctx;
    ky_egl_save_context(&prev_ctx);
    ky_egl_make_current(renderer->egl);
    glDeleteQueriesEXT(1, &timer->id);
    ky_egl_restore_context(&prev_ctx);
    free(timer);
}

static const struct wlr_renderer_impl renderer_impl = {
    .destroy = gl_destroy,
    .bind_buffer = gl_bind_buffer,
    .begin = gl_begin,
    .end = gl_end,
    .clear = gl_clear,
    .scissor = gl_scissor,
    .render_subtexture_with_matrix = gl_render_subtexture_with_matrix,
    .render_quad_with_matrix = gl_render_quad_with_matrix,
    .get_shm_texture_formats = gl_get_shm_texture_formats,
    .get_dmabuf_texture_formats = gl_get_dmabuf_texture_formats,
    .get_render_formats = gl_get_render_formats,
    .preferred_read_format = gl_preferred_read_format,
    .read_pixels = gl_read_pixels,
    .get_drm_fd = gl_get_drm_fd,
    .get_render_buffer_caps = gl_get_render_buffer_caps,
    .texture_from_buffer = ky_opengl_texture_from_buffer,
    .begin_buffer_pass = gl_begin_buffer_pass,
    .render_timer_create = gl_render_timer_create,
};

static const struct wlr_render_timer_impl render_timer_impl = {
    .get_duration_ns = gl_get_render_time,
    .destroy = gl_render_timer_destroy,
};

// https://www.khronos.org/opengl/wiki/Debug_Output
void ky_opengl_push_debug_(struct ky_opengl_renderer *renderer, const char *file, const char *func)
{
    if (!renderer->exts.KHR_debug) {
        return;
    }

    int len = snprintf(NULL, 0, "%s:%s", file, func) + 1;
    char str[len];
    snprintf(str, len, "%s:%s", file, func);
    glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 1, -1, str);
}

void ky_opengl_pop_debug(struct ky_opengl_renderer *renderer)
{
    if (renderer->exts.KHR_debug) {
        glPopDebugGroupKHR();
    }
}

static enum kywc_log_level gl_log_level_to_kywc(GLenum type)
{
    switch (type) {
    case GL_DEBUG_TYPE_ERROR_KHR:
        return KYWC_ERROR;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR:
        return KYWC_DEBUG;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR:
        return KYWC_ERROR;
    case GL_DEBUG_TYPE_PORTABILITY_KHR:
        return KYWC_DEBUG;
    case GL_DEBUG_TYPE_PERFORMANCE_KHR:
        return KYWC_DEBUG;
    case GL_DEBUG_TYPE_OTHER_KHR:
        return KYWC_DEBUG;
    case GL_DEBUG_TYPE_MARKER_KHR:
        return KYWC_DEBUG;
    case GL_DEBUG_TYPE_PUSH_GROUP_KHR:
        return KYWC_DEBUG;
    case GL_DEBUG_TYPE_POP_GROUP_KHR:
        return KYWC_DEBUG;
    default:
        return KYWC_DEBUG;
    }
}

static void gl_log(GLenum src, GLenum type, GLuint id, GLenum severity, GLsizei len,
                   const GLchar *msg, const void *user)
{
    kywc_log(gl_log_level_to_kywc(type), "[GL] %s", msg);
}

static GLuint compile_shader(struct ky_opengl_renderer *renderer, GLenum type, const GLchar *src)
{
    ky_opengl_push_debug(renderer);

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        kywc_log(KYWC_ERROR, "Failed to compile shader");
        glDeleteShader(shader);
        shader = 0;
    }

    ky_opengl_pop_debug(renderer);
    return shader;
}

static GLuint link_program(struct ky_opengl_renderer *renderer, const GLchar *vert_src,
                           const GLchar *frag_src)
{
    ky_opengl_push_debug(renderer);

    GLuint vert = compile_shader(renderer, GL_VERTEX_SHADER, vert_src);
    if (!vert) {
        goto error;
    }

    GLuint frag = compile_shader(renderer, GL_FRAGMENT_SHADER, frag_src);
    if (!frag) {
        glDeleteShader(vert);
        goto error;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    glDetachShader(prog, vert);
    glDetachShader(prog, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        kywc_log(KYWC_ERROR, "Failed to link shader");
        glDeleteProgram(prog);
        goto error;
    }

    ky_opengl_pop_debug(renderer);
    return prog;

error:
    ky_opengl_pop_debug(renderer);
    return 0;
}

static struct wlr_renderer *ky_opengl_renderer_create(struct ky_egl *egl)
{
    if (!ky_egl_make_current(egl)) {
        return NULL;
    }

    struct ky_opengl_renderer *renderer = calloc(1, sizeof(*renderer));
    if (renderer == NULL) {
        return NULL;
    }
    wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);

    wl_list_init(&renderer->buffers);
    wl_list_init(&renderer->textures);

    renderer->egl = egl;
    renderer->drm_fd = -1;

    kywc_log(KYWC_INFO, "Creating OpenGL renderer");
    kywc_log(KYWC_INFO, "Using %s", glGetString(GL_VERSION));
    kywc_log(KYWC_INFO, "GL vendor: %s", glGetString(GL_VENDOR));
    kywc_log(KYWC_INFO, "GL renderer: %s", glGetString(GL_RENDERER));

    if (!renderer->egl->exts.EXT_image_dma_buf_import) {
        kywc_log(KYWC_ERROR, "EGL_EXT_image_dma_buf_import not supported");
        free(renderer);
        return NULL;
    }
    if (egl->is_gles && !epoxy_has_gl_extension("GL_EXT_texture_format_BGRA8888")) {
        kywc_log(KYWC_ERROR, "BGRA8888 format not supported by GLES");
        free(renderer);
        return NULL;
    }
    if (egl->is_gles && epoxy_gl_version() < 30 &&
        !epoxy_has_gl_extension("GL_EXT_unpack_subimage")) {
        kywc_log(KYWC_ERROR, "GL_EXT_unpack_subimage not supported");
        free(renderer);
        return NULL;
    }

    renderer->exts.EXT_read_format_bgra =
        !egl->is_gles || epoxy_has_gl_extension("GL_EXT_read_format_bgra");

    renderer->exts.EXT_texture_type_2_10_10_10_REV =
        epoxy_has_gl_extension("GL_EXT_texture_type_2_10_10_10_REV");

    renderer->exts.OES_texture_half_float_linear =
        epoxy_has_gl_extension("GL_OES_texture_half_float_linear");

    renderer->exts.EXT_texture_norm16 = epoxy_has_gl_extension("GL_EXT_texture_norm16");

    renderer->exts.OES_egl_image_external = epoxy_has_gl_extension("GL_OES_EGL_image_external");

    renderer->exts.OES_egl_image = epoxy_has_gl_extension("GL_OES_EGL_image");

    renderer->exts.KHR_robustness = epoxy_has_gl_extension("GL_KHR_robustness");
    if (renderer->exts.KHR_robustness) {
        GLint notif_strategy = 0;
        glGetIntegerv(GL_RESET_NOTIFICATION_STRATEGY_KHR, &notif_strategy);
        switch (notif_strategy) {
        case GL_LOSE_CONTEXT_ON_RESET_KHR:
            kywc_log(KYWC_DEBUG, "GPU reset notifications are enabled");
            break;
        case GL_NO_RESET_NOTIFICATION_KHR:
            kywc_log(KYWC_DEBUG, "GPU reset notifications are disabled");
            break;
        }
    }

    renderer->exts.EXT_disjoint_timer_query = epoxy_has_gl_extension("GL_EXT_disjoint_timer_query");

    renderer->exts.KHR_debug = epoxy_has_gl_extension("GL_KHR_debug");
    if (renderer->exts.KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT_KHR);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
        glDebugMessageCallbackKHR(gl_log, NULL);
        // Silence unwanted message types
        glDebugMessageControlKHR(GL_DONT_CARE, GL_DEBUG_TYPE_POP_GROUP_KHR, GL_DONT_CARE, 0, NULL,
                                 GL_FALSE);
        glDebugMessageControlKHR(GL_DONT_CARE, GL_DEBUG_TYPE_PUSH_GROUP_KHR, GL_DONT_CARE, 0, NULL,
                                 GL_FALSE);
    }

    ky_opengl_push_debug(renderer);

    GLuint prog;
    renderer->shaders.quad.program = prog = link_program(renderer, common_vert_str, quad_frag_str);
    if (!renderer->shaders.quad.program) {
        goto error;
    }
    renderer->shaders.quad.proj = glGetUniformLocation(prog, "proj");
    renderer->shaders.quad.color = glGetUniformLocation(prog, "color");
    renderer->shaders.quad.pos_attrib = glGetAttribLocation(prog, "pos");

    renderer->shaders.tex_rgba.program = prog =
        link_program(renderer, common_vert_str, tex_rgba_frag_str);
    if (!renderer->shaders.tex_rgba.program) {
        goto error;
    }
    renderer->shaders.tex_rgba.proj = glGetUniformLocation(prog, "proj");
    renderer->shaders.tex_rgba.tex_proj = glGetUniformLocation(prog, "tex_proj");
    renderer->shaders.tex_rgba.tex = glGetUniformLocation(prog, "tex");
    renderer->shaders.tex_rgba.alpha = glGetUniformLocation(prog, "alpha");
    renderer->shaders.tex_rgba.pos_attrib = glGetAttribLocation(prog, "pos");

    renderer->shaders.tex_rgbx.program = prog =
        link_program(renderer, common_vert_str, tex_rgbx_frag_str);
    if (!renderer->shaders.tex_rgbx.program) {
        goto error;
    }
    renderer->shaders.tex_rgbx.proj = glGetUniformLocation(prog, "proj");
    renderer->shaders.tex_rgbx.tex_proj = glGetUniformLocation(prog, "tex_proj");
    renderer->shaders.tex_rgbx.tex = glGetUniformLocation(prog, "tex");
    renderer->shaders.tex_rgbx.alpha = glGetUniformLocation(prog, "alpha");
    renderer->shaders.tex_rgbx.pos_attrib = glGetAttribLocation(prog, "pos");

    if (renderer->exts.OES_egl_image_external) {
        renderer->shaders.tex_ext.program = prog =
            link_program(renderer, common_vert_str, tex_external_frag_str);
        if (!renderer->shaders.tex_ext.program) {
            goto error;
        }
        renderer->shaders.tex_ext.proj = glGetUniformLocation(prog, "proj");
        renderer->shaders.tex_ext.tex_proj = glGetUniformLocation(prog, "tex_proj");
        renderer->shaders.tex_ext.tex = glGetUniformLocation(prog, "tex");
        renderer->shaders.tex_ext.alpha = glGetUniformLocation(prog, "alpha");
        renderer->shaders.tex_ext.pos_attrib = glGetAttribLocation(prog, "pos");
    }

    ky_opengl_pop_debug(renderer);

    ky_egl_unset_current(renderer->egl);

    return &renderer->wlr_renderer;

error:
    glDeleteProgram(renderer->shaders.quad.program);
    glDeleteProgram(renderer->shaders.tex_rgba.program);
    glDeleteProgram(renderer->shaders.tex_rgbx.program);
    glDeleteProgram(renderer->shaders.tex_ext.program);

    ky_opengl_pop_debug(renderer);

    if (renderer->exts.KHR_debug) {
        glDisable(GL_DEBUG_OUTPUT_KHR);
        glDebugMessageCallbackKHR(NULL, NULL);
    }

    ky_egl_unset_current(renderer->egl);

    free(renderer);
    return NULL;
}

struct wlr_renderer *ky_opengl_renderer_create_with_drm_fd(int drm_fd)
{
    struct ky_egl *egl = ky_egl_create_with_drm_fd(drm_fd);
    if (!egl) {
        kywc_log(KYWC_ERROR, "Could not initialize EGL");
        return NULL;
    }

    struct wlr_renderer *renderer = ky_opengl_renderer_create(egl);
    if (!renderer) {
        kywc_log(KYWC_ERROR, "Failed to create OpenGL renderer");
        ky_egl_destroy(egl);
        return NULL;
    }

    return renderer;
}
