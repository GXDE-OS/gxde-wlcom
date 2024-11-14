// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>

#include <drm_fourcc.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/region.h>

#include <kywc/log.h>

#include "effect/capture.h"
#include "effect_p.h"
#include "output.h"
#include "painter.h"
#include "render/renderer.h"
#include "security.h"
#include "server.h"
#include "util/wayland.h"

enum capture_type {
    CAPTURE_TYPE_OUTPUT = 0,
    CAPTURE_TYPE_AREA,
    CAPTURE_TYPE_FULLSCREEN,
};

struct capture {
    struct wl_list link;
    struct capture_buffer *buffer; // xxx_capture->base

    struct {
        struct wl_signal update; // capture_update_event
        struct wl_signal destroy;
    } events;

    bool force_update, wants_update;
};

struct capture_buffer {
    struct wlr_buffer *buffer;
    struct wl_list link;

    struct wl_list captures; // capture->link
    struct wl_list requests; // request->link

    enum capture_type type;
    uint32_t options;
    union {
        struct output *output;
        struct wlr_box area;
    };

    float scale;
    bool was_damaged, buffer_changed;
    bool need_destroy, can_destroy;
};

struct capture_request {
    struct wl_list link;
    struct wl_list output_link;
    struct capture_buffer *buffer;
    struct capture_output *output;

    struct wlr_fbox src_box;
    struct wlr_box dst_box;
    bool blit_done, cursor_locked;
};

struct capture_output {
    struct wl_list link;
    struct wl_list requests; // capture_request->output_link;

    struct output *output;
    struct wl_listener commit;
    struct wl_listener geometry;
    struct wl_listener disable;

    bool render_locked;
};

struct capture_manager {
    struct wl_list buffers; // capture_buffer->link
    struct wl_list outputs; // capture_output->link

    struct wl_listener new_enabled_output;

    struct server *server;
    struct wl_listener destroy;
};

static struct capture_manager *manager = NULL;

static struct capture_request *capture_request_create(struct output *output,
                                                      struct capture_buffer *buffer);

static void capture_request_destroy(struct capture_request *request);

static bool capture_buffer_clear(struct capture_buffer *buffer)
{
    struct wlr_render_pass *pass =
        wlr_renderer_begin_buffer_pass(manager->server->renderer, buffer->buffer, NULL);
    if (!pass) {
        return false;
    }

    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .color = { 0, 0, 0, 0 },
                                       .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                   });
    wlr_render_pass_submit(pass);
    /* mark buffer is damaged */
    buffer->was_damaged = true;

    return true;
}

static struct wlr_buffer *capture_buffer_allocate(struct capture_buffer *capture_buffer)
{
    assert(!wl_list_empty(&capture_buffer->requests));
    /* calc buffer size used in capture_buffer */
    int width = 0, height = 0;

    if (capture_buffer->type == CAPTURE_TYPE_OUTPUT ||
        capture_buffer->type == CAPTURE_TYPE_FULLSCREEN) {
        pixman_region32_t region;
        pixman_region32_init(&region);
        struct capture_request *request;
        wl_list_for_each(request, &capture_buffer->requests, link) {
            pixman_region32_union_rect(&region, &region, request->dst_box.x, request->dst_box.y,
                                       request->dst_box.width, request->dst_box.height);
        }
        width = region.extents.x2 - region.extents.x1;
        height = region.extents.y2 - region.extents.y1;
        pixman_region32_fini(&region);
    } else if (capture_buffer->type == CAPTURE_TYPE_AREA) {
        width = capture_buffer->area.width;
        height = capture_buffer->area.height;
    }

    bool need_create = !capture_buffer->buffer || (capture_buffer->buffer->width != width ||
                                                   capture_buffer->buffer->height != height);
    if (!need_create) {
        return capture_buffer->buffer;
    }

    struct wlr_buffer *buffer = ky_renderer_create_buffer(
        manager->server->renderer, manager->server->allocator, width, height, DRM_FORMAT_ARGB8888,
        capture_buffer->options & CAPTURE_NEED_SINGLE_PLANE);
    if (!buffer) {
        kywc_log(KYWC_ERROR, "failed to create wlr buffer");
        return NULL;
    }

    wlr_buffer_drop(capture_buffer->buffer);
    capture_buffer->buffer = buffer;
    capture_buffer->buffer_changed = true;

    /* re-blit all requests */
    struct capture_request *request;
    wl_list_for_each(request, &capture_buffer->requests, link) {
        request->blit_done = false;
    }

    capture_buffer_clear(capture_buffer);

    return buffer;
}

static bool capture_buffer_calc_box(struct capture_buffer *buffer, struct output *output,
                                    struct wlr_box *dst, struct wlr_fbox *src)
{
    float scale = buffer->options & CAPTURE_NEED_UNSCALED ? output_manager_get_scale() : 1.0;
    buffer->scale = scale;

    struct kywc_box *geo = &output->geometry;
    *dst = (struct wlr_box){ 0 };
    *src = (struct wlr_fbox){ 0 };

    if (buffer->type == CAPTURE_TYPE_OUTPUT) {
        /* calc the dst box in request */
        dst->width = geo->width * scale;
        dst->height = geo->height * scale;
    } else if (buffer->type == CAPTURE_TYPE_AREA) {
        *dst = (struct wlr_box){ geo->x * scale, geo->y * scale, geo->width * scale,
                                 geo->height * scale };
        if (!wlr_box_intersection(dst, dst, &buffer->area)) {
            return false;
        }

        float output_scale = output->wlr_output->scale;
        src->x = (dst->x - geo->x * scale) / scale * output_scale;
        src->y = (dst->y - geo->y * scale) / scale * output_scale;
        src->width = dst->width / scale * output_scale;
        src->height = dst->height / scale * output_scale;

        int width, height;
        /* translate to buffer coord, otherwise assert failed in wlr_render_pass_add_texture */
        wlr_output_transformed_resolution(output->wlr_output, &width, &height);
        wlr_fbox_transform(src, src, wlr_output_transform_invert(output->wlr_output->transform),
                           width, height);

        dst->x -= buffer->area.x;
        dst->y -= buffer->area.y;
    } else if (buffer->type == CAPTURE_TYPE_FULLSCREEN) {
        /* assume that we always have zero coord */
        *dst = (struct wlr_box){ geo->x * scale, geo->y * scale, geo->width * scale,
                                 geo->height * scale };
    }

    return true;
}

static void capture_manager_schedule_frame(void)
{
    struct capture_output *output;
    wl_list_for_each(output, &manager->outputs, link) {
        output_schedule_frame(output->output->wlr_output);
    }
}

static void capture_buffer_destroy(struct capture_buffer *buffer)
{
    /* don't destroy if still have captures */
    if (!buffer->need_destroy && !wl_list_empty(&buffer->captures)) {
        return;
    }
    /* mark need_destroy if cannot be destroyed current */
    if (!buffer->can_destroy) {
        buffer->need_destroy = true;
        return;
    }

    struct capture_request *request, *request_tmp;
    wl_list_for_each_safe(request, request_tmp, &buffer->requests, link) {
        request->buffer = NULL;
        capture_request_destroy(request);
    }

    /* force destroy all captures */
    struct capture *capture, *tmp;
    wl_list_for_each_safe(capture, tmp, &buffer->captures, link) {
        capture->buffer = NULL;
        capture_destroy(capture);
    }

    if (buffer->buffer) {
        wlr_buffer_drop(buffer->buffer);
    }

    wl_list_remove(&buffer->link);
    free(buffer);
}

static void capture_output_destroy(struct capture_output *output)
{
    if (output->render_locked) {
        wlr_output_lock_attach_render(output->output->wlr_output, false);
    }

    wl_list_remove(&output->commit.link);
    wl_list_remove(&output->geometry.link);
    wl_list_remove(&output->disable.link);
    wl_list_remove(&output->link);

    struct capture_request *request, *tmp;
    wl_list_for_each_safe(request, tmp, &output->requests, output_link) {
        capture_request_destroy(request);
    }

    free(output);
}

static void capture_request_destroy(struct capture_request *request)
{
    wl_list_remove(&request->link);
    wl_list_remove(&request->output_link);

    /* destroy capture_buffer if no requests */
    struct capture_buffer *buffer = request->buffer;
    if (buffer) {
        if (wl_list_empty(&buffer->requests)) {
            buffer->need_destroy = true;
            capture_buffer_destroy(buffer);
        } else if (buffer->type == CAPTURE_TYPE_AREA) {
            /* clear the buffer when area type */
            capture_buffer_clear(buffer);
        }
    }

    struct capture_output *output = request->output;
    if (request->cursor_locked) {
        wlr_output_lock_software_cursors(output->output->wlr_output, false);
    }

    free(request);
}

static void capture_output_handle_disable(struct wl_listener *listener, void *data)
{
    struct capture_output *capture_output = wl_container_of(listener, capture_output, disable);
    capture_output_destroy(capture_output);
    /* re-allocate buffer */
    struct capture_buffer *buffer;
    wl_list_for_each(buffer, &manager->buffers, link) {
        capture_buffer_allocate(buffer);
    }
    capture_manager_schedule_frame();
}

static void capture_output_handle_geometry(struct wl_listener *listener, void *data)
{
    struct capture_output *capture_output = wl_container_of(listener, capture_output, geometry);

    /* update requests src and dst box if output geometry changed */
    struct capture_request *request, *tmp;
    wl_list_for_each_safe(request, tmp, &capture_output->requests, output_link) {
        if (capture_buffer_calc_box(request->buffer, capture_output->output, &request->dst_box,
                                    &request->src_box)) {
            capture_buffer_allocate(request->buffer);
        } else {
            capture_request_destroy(request);
        }
    }

    /* update capture_buffer with area type */
    struct capture_buffer *buffer;
    wl_list_for_each(buffer, &manager->buffers, link) {
        if (buffer->type != CAPTURE_TYPE_AREA) {
            continue;
        }

        bool request_found = false;
        wl_list_for_each(request, &buffer->requests, link) {
            if (request->output == capture_output) {
                request_found = true;
                break;
            }
        }

        if (!request_found) {
            capture_request_create(capture_output->output, buffer);
            /* no need to re-allocate buffer when area type */
        }
    }

    capture_manager_schedule_frame();
}

static bool capture_request_do_blit(struct capture_request *request, struct wlr_buffer *src)
{
    struct capture_buffer *buffer = request->buffer;
    struct output *output = request->output->output;

    pixman_region32_t clip;
    if (buffer->type == CAPTURE_TYPE_AREA) {
        pixman_region32_init_rect(&clip, request->dst_box.x + buffer->area.x,
                                  request->dst_box.y + buffer->area.y, request->dst_box.width,
                                  request->dst_box.height);
    } else {
        pixman_region32_init_rect(&clip, output->geometry.x, output->geometry.y,
                                  output->geometry.width, output->geometry.height);
    }
    bool has_security = security_check_output(output, &clip);
    if (has_security && !pixman_region32_not_empty(&clip)) {
        /* output capture is not allowed */
        pixman_region32_fini(&clip);
        return true;
    }

    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_render_pass *render_pass =
        wlr_renderer_begin_buffer_pass(wlr_output->renderer, buffer->buffer, NULL);
    if (!render_pass) {
        pixman_region32_fini(&clip);
        return false;
    }

    if (has_security) {
        switch (buffer->type) {
        case CAPTURE_TYPE_OUTPUT:
            pixman_region32_translate(&clip, -output->geometry.x, -output->geometry.y);
            // fallthrough to fullscreen
        case CAPTURE_TYPE_FULLSCREEN:
            wlr_region_scale(&clip, &clip, buffer->scale);
            break;
        case CAPTURE_TYPE_AREA:
            wlr_region_scale(&clip, &clip, buffer->scale);
            pixman_region32_translate(&clip, -buffer->area.x, -buffer->area.y);
            break;
        }
    }

    struct wlr_texture *src_tex = wlr_texture_from_buffer(wlr_output->renderer, src);
    struct wlr_render_texture_options options = {
        .texture = src_tex,
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
        .src_box = request->src_box,
        .dst_box = request->dst_box,
        .clip = has_security ? &clip : NULL,
        .transform = wlr_output_transform_invert(wlr_output->transform),
    };

    wlr_render_pass_add_texture(render_pass, &options);
    wlr_texture_destroy(src_tex);
    wlr_render_pass_submit(render_pass);
    pixman_region32_fini(&clip);

    kywc_log(KYWC_DEBUG, "capture output %s copy (%f, %f) %f x %f to (%d, %d) %d x %d",
             wlr_output->name, request->src_box.x, request->src_box.y, request->src_box.width,
             request->src_box.height, request->dst_box.x, request->dst_box.y,
             request->dst_box.width, request->dst_box.height);

    return true;
}

static void capture_request_mark_done(struct capture_request *request)
{
    struct capture_buffer *buffer = request->buffer;
    request->blit_done = true;

    struct capture_request *req;
    wl_list_for_each(req, &buffer->requests, link) {
        if (!req->blit_done) {
            return;
        }
    }

    struct capture *capture, *tmp;
    if (!buffer->was_damaged) {
        /* must have a buffer because was_damaged == false */
        struct capture_update_event event = {
            .buffer = buffer->buffer,
            .buffer_changed = true,
        };
        buffer->can_destroy = false;
        wl_list_for_each_safe(capture, tmp, &buffer->captures, link) {
            if (capture->wants_update && capture->force_update) {
                capture->force_update = false;
                wl_signal_emit_oneshot(&capture->events.update, &event);
            }
        }
        /* capture may need be destroyed in update */
        buffer->can_destroy = true;
        capture_buffer_destroy(buffer);
        return;
    }

    /* now all requests are done, emit update signal for all captures */
    struct capture_update_event event = { .buffer = buffer->buffer };
    buffer->can_destroy = false;
    wl_list_for_each_safe(capture, tmp, &buffer->captures, link) {
        if (capture->wants_update) {
            event.buffer_changed = buffer->buffer_changed || capture->force_update;
            capture->force_update = false;
            wl_signal_emit_oneshot(&capture->events.update, &event);
        } else {
            capture->force_update |= buffer->buffer_changed;
        }
    }

    wl_list_for_each(req, &buffer->requests, link) {
        req->blit_done = false;
    }

    buffer->buffer_changed = false;
    buffer->was_damaged = false;
    buffer->can_destroy = true;
    capture_buffer_destroy(buffer);
}

static bool capture_buffer_need_update(struct capture_buffer *buffer)
{
    struct capture *capture;
    wl_list_for_each(capture, &buffer->captures, link) {
        if (capture->wants_update) {
            return true;
        }
    }
    return false;
}

static void capture_buffer_update_damage(struct capture_request *request, bool has_damage,
                                         const pixman_region32_t *damage)
{
    struct capture_buffer *buffer = request->buffer;
    if (!has_damage || buffer->was_damaged) {
        return;
    }

    if (buffer->type != CAPTURE_TYPE_AREA) {
        buffer->was_damaged = true;
        return;
    }
    /* check damage region and src_box */
    pixman_region32_t box;
    pixman_region32_init_rect(&box, request->src_box.x, request->src_box.y, request->src_box.width,
                              request->src_box.height);
    pixman_region32_intersect(&box, &box, damage);
    if (pixman_region32_not_empty(&box)) {
        buffer->was_damaged = true;
    }
    pixman_region32_fini(&box);
}

static void capture_output_handle_commit(struct wl_listener *listener, void *data)
{
    struct capture_output *capture_output = wl_container_of(listener, capture_output, commit);
    struct wlr_output_event_commit *event = data;

    if (wl_list_empty(&capture_output->requests)) {
        return;
    }
    if (!(event->state->committed & WLR_OUTPUT_STATE_BUFFER)) {
        return;
    }

    bool has_damage = event->state->committed & WLR_OUTPUT_STATE_DAMAGE &&
                      pixman_region32_not_empty(&event->state->damage);

    /* do blit for every request */
    struct capture_buffer *buffer;
    struct capture_request *request, *tmp;
    wl_list_for_each_safe(request, tmp, &capture_output->requests, output_link) {
        buffer = request->buffer;
        capture_buffer_update_damage(request, has_damage, &event->state->damage);
        /* skip this request if all captures don't want update */
        if (!capture_buffer_need_update(buffer)) {
            continue;
        }
        if (!buffer->was_damaged) {
            capture_request_mark_done(request);
            continue;
        }

        if (capture_request_do_blit(request, event->state->buffer)) {
            /* mark this request in capture_buffer is done */
            capture_request_mark_done(request);
        } else {
            /* destroy capture_buffer */
            buffer->need_destroy = true;
            capture_buffer_destroy(buffer);
        }
    }
}

static struct capture_output *capture_output_get(struct output *output)
{
    struct capture_output *capture_output;
    wl_list_for_each(capture_output, &manager->outputs, link) {
        if (capture_output->output == output) {
            /* Locks the output to only use rendering instead of direct scan-out */
            if (!capture_output->render_locked) {
                wlr_output_lock_attach_render(output->wlr_output, true);
                capture_output->render_locked = true;
            }
            return capture_output;
        }
    }
    return NULL;
}

static struct capture_output *capture_output_create(struct output *output)
{
    struct capture_output *capture_output = calloc(1, sizeof(*capture_output));
    if (!capture_output) {
        return NULL;
    }

    wl_list_init(&capture_output->requests);
    wl_list_insert(&manager->outputs, &capture_output->link);

    capture_output->output = output;
    capture_output->commit.notify = capture_output_handle_commit;
    wl_signal_add(&output->wlr_output->events.commit, &capture_output->commit);
    capture_output->geometry.notify = capture_output_handle_geometry;
    wl_signal_add(&output->events.geometry, &capture_output->geometry);
    capture_output->disable.notify = capture_output_handle_disable;
    wl_signal_add(&output->events.disable, &capture_output->disable);

    return capture_output;
}

static struct capture_request *capture_request_create(struct output *output,
                                                      struct capture_buffer *buffer)
{
    /* create capture requests according to capture type */
    struct wlr_fbox fbox;
    struct wlr_box box;

    if (!capture_buffer_calc_box(buffer, output, &box, &fbox)) {
        return NULL;
    }

    struct capture_output *capture_output = capture_output_get(output);
    if (!capture_output) {
        return NULL;
    }

    struct capture_request *request = calloc(1, sizeof(*request));
    if (!request) {
        return NULL;
    }

    request->buffer = buffer;
    request->output = capture_output;
    request->src_box = fbox;
    request->dst_box = box;
    request->cursor_locked = buffer->options & CAPTURE_NEED_CURSOR;
    wl_list_insert(&buffer->requests, &request->link);
    wl_list_insert(&capture_output->requests, &request->output_link);

    if (request->cursor_locked) {
        wlr_output_lock_software_cursors(output->wlr_output, true);
    }

    return request;
}

static bool create_request(struct kywc_output *kywc_output, int index, void *data)
{
    struct output *output = output_from_kywc_output(kywc_output);
    struct capture_buffer *buffer = data;
    capture_request_create(output, buffer);
    return false;
}

static struct capture_buffer *capture_buffer_get_or_create(enum capture_type type, uint32_t options,
                                                           struct output *output,
                                                           struct wlr_box *area)
{
    struct capture_buffer *buffer;
    wl_list_for_each(buffer, &manager->buffers, link) {
        /* skip cursor option check */
        if (buffer->type != type ||
            (buffer->options ^ options) & (CAPTURE_NEED_UNSCALED | CAPTURE_NEED_SINGLE_PLANE)) {
            continue;
        }
        if (type == CAPTURE_TYPE_FULLSCREEN) {
            return buffer;
        } else if (type == CAPTURE_TYPE_OUTPUT && buffer->output == output) {
            return buffer;
        } else if (type == CAPTURE_TYPE_AREA && wlr_box_equal(&buffer->area, area)) {
            return buffer;
        }
    }

    buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        return NULL;
    }

    buffer->type = type;
    buffer->options = options;
    buffer->was_damaged = true;
    buffer->can_destroy = true;

    wl_list_init(&buffer->captures);
    wl_list_init(&buffer->requests);

    if (type == CAPTURE_TYPE_OUTPUT) {
        buffer->output = output;
        capture_request_create(buffer->output, buffer);
    } else if (type == CAPTURE_TYPE_AREA) {
        buffer->area = *area;
        output_manager_for_each_output(create_request, true, buffer);
    } else if (type == CAPTURE_TYPE_FULLSCREEN) {
        output_manager_for_each_output(create_request, true, buffer);
    }

    if (wl_list_empty(&buffer->requests) || !capture_buffer_allocate(buffer)) {
        free(buffer);
        return NULL;
    }

    wl_list_insert(&manager->buffers, &buffer->link);
    return buffer;
}

struct capture *capture_create_from_output(struct output *output, uint32_t options)
{
    if (!manager || !output->scene_output) {
        return NULL;
    }

    struct capture *capture = calloc(1, sizeof(*capture));
    if (!capture) {
        return NULL;
    }

    struct capture_buffer *buffer =
        capture_buffer_get_or_create(CAPTURE_TYPE_OUTPUT, options, output, NULL);
    if (!buffer) {
        free(capture);
        return NULL;
    }

    capture->buffer = buffer;
    wl_list_insert(&buffer->captures, &capture->link);
    wl_signal_init(&capture->events.update);
    wl_signal_init(&capture->events.destroy);
    capture->force_update = capture->wants_update = true;
    /* buffer update is needed */
    capture_manager_schedule_frame();

    return capture;
}

struct capture *capture_create_from_area(struct wlr_box *rect, uint32_t options)
{
    if (!manager || wlr_box_empty(rect)) {
        return NULL;
    }

    struct capture *capture = calloc(1, sizeof(*capture));
    if (!capture) {
        return NULL;
    }

    struct capture_buffer *buffer =
        capture_buffer_get_or_create(CAPTURE_TYPE_AREA, options, NULL, rect);
    if (!buffer) {
        free(capture);
        return NULL;
    }

    capture->buffer = buffer;
    wl_list_insert(&buffer->captures, &capture->link);
    wl_signal_init(&capture->events.update);
    wl_signal_init(&capture->events.destroy);
    capture->force_update = capture->wants_update = true;
    /* buffer update is needed */
    capture_manager_schedule_frame();

    return capture;
}

struct capture *capture_create_from_fullscreen(uint32_t options)
{
    if (!manager) {
        return NULL;
    }

    struct capture *capture = calloc(1, sizeof(*capture));
    if (!capture) {
        return NULL;
    }

    struct capture_buffer *buffer =
        capture_buffer_get_or_create(CAPTURE_TYPE_FULLSCREEN, options, NULL, NULL);
    if (!buffer) {
        free(capture);
        return NULL;
    }

    capture->buffer = buffer;
    wl_list_insert(&buffer->captures, &capture->link);
    wl_signal_init(&capture->events.update);
    wl_signal_init(&capture->events.destroy);
    capture->force_update = capture->wants_update = true;
    /* buffer update is needed */
    capture_manager_schedule_frame();

    return capture;
}

void capture_destroy(struct capture *capture)
{
    if (!capture) {
        return;
    }

    wl_signal_emit_mutable(&capture->events.destroy, NULL);
    wl_list_remove(&capture->link);

    /* capture_buffer may not be destroyed caused by can_destroy == false */
    if (capture->buffer) {
        capture_buffer_destroy(capture->buffer);
    }

    free(capture);
}

void capture_add_update_listener(struct capture *capture, struct wl_listener *listener)
{
    assert(wl_list_empty(&capture->events.update.listener_list));
    wl_signal_add(&capture->events.update, listener);
}

void capture_add_destroy_listener(struct capture *capture, struct wl_listener *listener)
{
    assert(wl_list_empty(&capture->events.destroy.listener_list));
    wl_signal_add(&capture->events.destroy, listener);
}

void capture_mark_wants_update(struct capture *capture, bool wants, bool force)
{
    if (capture->wants_update == wants) {
        return;
    }

    capture->wants_update = wants;

    /* should send update if buffer was damaged */
    if (wants) {
        capture->force_update |= force;
        capture_manager_schedule_frame();
    }
}

/**
 * helpers to read buffer, and write to png file in another thread.
 */
void capture_read_buffer(struct wlr_buffer *buffer, uint32_t format, uint32_t stride,
                         struct wlr_box *box, void *data)
{
    if (!wlr_renderer_begin_with_buffer(manager->server->renderer, buffer)) {
        return;
    }
    wlr_renderer_read_pixels(manager->server->renderer, format, stride, box->width, box->height, 0,
                             0, box->x, box->y, data);
    wlr_renderer_end(manager->server->renderer);
}

struct capture_data {
    struct wlr_buffer *buffer;
    void (*done)(const char *path, void *data);
    char *path;
    void *user_data;
};

static void capture_write_image(struct wlr_buffer *buffer, const char *path,
                                void (*done)(const char *path, void *data), void *user_data)
{
    painter_buffer_to_file(buffer, path);
    wlr_buffer_drop(buffer);

    if (done) {
        done(path, user_data);
    }
}

static void write_image(void *job, void *gdata, int index)
{
    kywc_log(KYWC_DEBUG, "%s: in thread %d", __func__, index);
    struct capture_data *data = job;
    capture_write_image(data->buffer, data->path, data->done, data->user_data);
    free(data->path);
    free(data);
}

void capture_write_file(struct wlr_buffer *buffer, int width, int height, const char *path,
                        void (*done)(const char *path, void *data), void *user_data)
{
    uint32_t format;
    size_t stride;
    void *dst_ptr;

    struct wlr_buffer *dst_buf = painter_create_buffer(width, height, 1.0);
    wlr_buffer_begin_data_ptr_access(dst_buf, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &dst_ptr, &format,
                                     &stride);
    capture_read_buffer(buffer, format, stride,
                        &(struct wlr_box){ 0, 0, dst_buf->width, dst_buf->height }, dst_ptr);
    wlr_buffer_end_data_ptr_access(dst_buf);
    kywc_log(KYWC_DEBUG, "capture copy buffer to memory");

    struct capture_data *data = malloc(sizeof(*data));
    data->buffer = dst_buf;
    data->path = strdup(path);
    data->done = done;
    data->user_data = user_data;

    if (!queue_add_job(&manager->server->queue, data, write_image, NULL)) {
        free(data->path);
        free(data);
        capture_write_image(dst_buf, path, done, user_data);
    }
}

static void handle_new_enabled_output(struct wl_listener *listener, void *data)
{
    struct kywc_output *kywc_output = data;
    struct output *output = output_from_kywc_output(kywc_output);

    if (!capture_output_create(output)) {
        return;
    }

    struct capture_buffer *buffer, *tmp;
    wl_list_for_each_safe(buffer, tmp, &manager->buffers, link) {
        if (buffer->type == CAPTURE_TYPE_OUTPUT) {
            continue;
        }
        if (capture_request_create(output, buffer)) {
            capture_buffer_allocate(buffer);
        }
    }
    /* blit done is reset, must blit again */
    capture_manager_schedule_frame();
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->destroy.link);
    wl_list_remove(&manager->new_enabled_output.link);
    free(manager);
    manager = NULL;
}

bool capture_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->buffers);
    wl_list_init(&manager->outputs);

    manager->new_enabled_output.notify = handle_new_enabled_output;
    output_manager_add_new_enabled_listener(&manager->new_enabled_output);

    manager->server = server;
    manager->destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->destroy);

    return true;
}
