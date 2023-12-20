// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <drm_fourcc.h>
#include <stdlib.h>

#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output_layout.h>

#include <kywc/log.h>

#include "effect/screencopy.h"
#include "effect_p.h"
#include "render/renderer.h"

struct screencopy_output {
    struct wl_list link;
    struct wlr_output_layout_output *l_output;
    struct wl_listener output_commit;
    /* when output disabled or destroyed */
    struct wl_listener layout_destroy;

    struct wlr_fbox src_box;
    struct wlr_box dst_box;

    bool cursor_locked;
};

struct screencopy_manager {
    struct server *server;
    struct wl_listener destroy;
    struct wl_list outputs;

    struct wlr_buffer *buffer;
    int width, height;

    screencopy_done_func_t done;
    void *data;

    bool taking_screencopy;
};

static struct screencopy_manager *manager = NULL;

static float max_scale(void)
{
    float scale = 1.0;
    struct wlr_output_layout_output *l_output;
    wl_list_for_each(l_output, &manager->server->layout->outputs, link) {
        if (l_output->output->scale > scale) {
            scale = l_output->output->scale;
        }
    }
    return scale;
}

static struct wlr_output *output_from_name(const char *name)
{
    struct wlr_output_layout_output *l_output;
    wl_list_for_each(l_output, &manager->server->layout->outputs, link) {
        if (strcmp(l_output->output->name, name) == 0) {
            return l_output->output;
        }
    }
    return NULL;
}

static void screencopy_done(void)
{
    /* not finished yet */
    if (!wl_list_empty(&manager->outputs)) {
        return;
    }

    manager->done(manager->buffer, manager->width, manager->height, manager->data);

    manager->taking_screencopy = false;
}

static void screencopy_output_destroy(struct screencopy_output *s_output)
{
    if (s_output->l_output) {
        wlr_output_lock_attach_render(s_output->l_output->output, false);
        if (s_output->cursor_locked) {
            wlr_output_lock_software_cursors(s_output->l_output->output, false);
        }
    }

    wl_list_remove(&s_output->output_commit.link);
    wl_list_remove(&s_output->layout_destroy.link);
    wl_list_remove(&s_output->link);
    free(s_output);
}

static void screencopy_handle_layout_destroy(struct wl_listener *listener, void *data)
{
    struct screencopy_output *s_output = wl_container_of(listener, s_output, layout_destroy);
    s_output->l_output = NULL;
    screencopy_output_destroy(s_output);
}

static void screencopy_handle_output_commit(struct wl_listener *listener, void *data)
{
    struct screencopy_output *s_output = wl_container_of(listener, s_output, output_commit);
    struct wlr_output_event_commit *event = data;
    struct wlr_output *output = s_output->l_output->output;

    if (!(event->state->committed & WLR_OUTPUT_STATE_BUFFER)) {
        return;
    }

    struct wlr_render_pass *pass =
        wlr_renderer_begin_buffer_pass(output->renderer, manager->buffer, NULL);
    if (!pass) {
        kywc_log(KYWC_ERROR, "screencopy pass create failed");
        screencopy_output_destroy(s_output);
        screencopy_done();
        return;
    }

    struct wlr_texture *src_tex = wlr_texture_from_buffer(output->renderer, event->state->buffer);
    struct wlr_render_texture_options options = {
        .texture = src_tex,
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
        .src_box = s_output->src_box,
        .dst_box = s_output->dst_box,
        .transform = wlr_output_transform_invert(output->transform),
    };

    wlr_render_pass_add_texture(pass, &options);
    wlr_render_pass_submit(pass);
    wlr_texture_destroy(src_tex);

    kywc_log(KYWC_DEBUG, "screencopy output %s copy (%f, %f) %f x %f to (%d, %d) %d x %d",
             output->name, s_output->src_box.x, s_output->src_box.y, s_output->src_box.width,
             s_output->src_box.height, s_output->dst_box.x, s_output->dst_box.y,
             s_output->dst_box.width, s_output->dst_box.height);

    screencopy_output_destroy(s_output);
    screencopy_done();
}

static void screencopy_output_create(struct wlr_output_layout_output *l_output,
                                     struct wlr_fbox *src, struct wlr_box *dst, bool cursor_locked)
{
    struct screencopy_output *s_output = calloc(1, sizeof(*s_output));
    if (!s_output) {
        return;
    }

    /* leave empty if src is NULL */
    if (src != NULL) {
        s_output->src_box = *src;
    }
    s_output->dst_box = *dst;
    s_output->l_output = l_output;
    s_output->cursor_locked = cursor_locked;
    wl_list_insert(&manager->outputs, &s_output->link);

    s_output->layout_destroy.notify = screencopy_handle_layout_destroy;
    wl_signal_add(&l_output->events.destroy, &s_output->layout_destroy);

    struct wlr_output *output = l_output->output;
    s_output->output_commit.notify = screencopy_handle_output_commit;
    wl_signal_add(&output->events.commit, &s_output->output_commit);

    wlr_output_schedule_frame(output);
    wlr_output_lock_attach_render(output, true);
    if (s_output->cursor_locked) {
        wlr_output_lock_software_cursors(output, true);
    }
}

static struct wlr_buffer *screencopy_create_buffer(int width, int height)
{
    manager->width = width;
    manager->height = height;

    bool need_create = !manager->buffer || // no buffer or smaller than source
                       (manager->buffer->width < width || manager->buffer->height < height);
    if (!need_create) {
        return manager->buffer;
    }

    const struct wlr_drm_format *format =
        ky_renderer_get_render_format(manager->server->renderer, DRM_FORMAT_ARGB8888);
    if (!format) {
        return NULL;
    }

    struct wlr_buffer *wlr_buffer =
        wlr_allocator_create_buffer(manager->server->allocator, width, height, format);
    if (!wlr_buffer) {
        return NULL;
    }

    wlr_buffer_drop(manager->buffer);
    manager->buffer = wlr_buffer;

    return wlr_buffer;
}

static bool screencopy_clear_buffer(struct wlr_buffer *buffer)
{
    struct wlr_render_pass *pass =
        wlr_renderer_begin_buffer_pass(manager->server->renderer, buffer, NULL);
    if (!pass) {
        return false;
    }

    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .color = { 0, 0, 0, 0 },
                                       .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                   });
    wlr_render_pass_submit(pass);
    return true;
}

bool screencopy_area(struct wlr_box *area, bool unscaled, bool cursor, screencopy_done_func_t done,
                     void *data)
{
    if (!manager || manager->taking_screencopy) {
        return false;
    }

    /* if no layout output in layout */
    if (wl_list_empty(&manager->server->layout->outputs)) {
        return false;
    }

    if (!screencopy_create_buffer(area->width, area->height)) {
        return false;
    }

    uint32_t layout_output_count = 0;
    float scale = unscaled ? max_scale() : 1.0;
    struct wlr_box box;
    struct wlr_fbox fbox;
    int width, height;
    float output_scale;

    /* calc dst box per output */
    struct wlr_output_layout_output *l_output;
    wl_list_for_each(l_output, &manager->server->layout->outputs, link) {
        wlr_output_effective_resolution(l_output->output, &width, &height);

        box.x = l_output->x;
        box.y = l_output->y;
        box.width = width;
        box.height = height;

        if (unscaled) {
            box.x *= scale;
            box.y *= scale;
            box.width *= scale;
            box.height *= scale;
        }

        if (!wlr_box_intersection(&box, &box, area)) {
            continue;
        }

        output_scale = l_output->output->scale;
        if (unscaled) {
            fbox.x = (box.x - l_output->x * scale) / scale * output_scale;
            fbox.y = (box.y - l_output->y * scale) / scale * output_scale;
            fbox.width = box.width / scale * output_scale;
            fbox.height = box.height / scale * output_scale;
        } else {
            fbox.x = (box.x - l_output->x) * output_scale;
            fbox.y = (box.y - l_output->y) * output_scale;
            fbox.width = box.width * output_scale;
            fbox.height = box.height * output_scale;
        }

        box.x -= area->x;
        box.y -= area->y;

        /* translate to buffer coord, otherwise assert failed in wlr_render_pass_add_texture */
        wlr_output_transformed_resolution(l_output->output, &width, &height);
        wlr_fbox_transform(&fbox, &fbox, wlr_output_transform_invert(l_output->output->transform),
                           width, height);

        screencopy_output_create(l_output, &fbox, &box, cursor);

        layout_output_count++;
    }

    /* clear buffer */
    if (layout_output_count > 1) {
        screencopy_clear_buffer(manager->buffer);
    }

    manager->done = done;
    manager->data = data;
    manager->taking_screencopy = true;

    return true;
}

bool screencopy_output(const char *name, bool unscaled, bool cursor, screencopy_done_func_t done,
                       void *data)
{
    if (!manager || manager->taking_screencopy || !name) {
        return false;
    }

    struct wlr_output *output = output_from_name(name);
    if (!output) {
        return false;
    }

    struct wlr_output_layout_output *l_output =
        wlr_output_layout_get(manager->server->layout, output);
    if (!l_output) {
        return false;
    }

    int width, height;
    wlr_output_effective_resolution(output, &width, &height);

    struct wlr_box geo = {
        .width = width,
        .height = height,
    };

    if (unscaled) {
        float scale = max_scale();
        geo.width *= scale;
        geo.height *= scale;
    }

    if (!screencopy_create_buffer(geo.width, geo.height)) {
        return false;
    }

    screencopy_output_create(l_output, NULL, &geo, cursor);

    manager->done = done;
    manager->data = data;
    manager->taking_screencopy = true;

    return true;
}

bool screencopy_full(bool unscaled, bool cursor, screencopy_done_func_t done, void *data)
{
    if (!manager || manager->taking_screencopy) {
        return false;
    }

    /* if no layout output in layout */
    if (wl_list_empty(&manager->server->layout->outputs)) {
        return false;
    }

    struct wlr_box geo;
    wlr_output_layout_get_box(manager->server->layout, NULL, &geo);

    pixman_region32_t region;
    pixman_region32_init(&region);

    uint32_t layout_output_count = 0;
    float scale = unscaled ? max_scale() : 1.0;
    struct wlr_box box;
    int width, height;

    /* calc dst box per output */
    struct wlr_output_layout_output *l_output;
    wl_list_for_each(l_output, &manager->server->layout->outputs, link) {
        wlr_output_effective_resolution(l_output->output, &width, &height);

        box.x = l_output->x - geo.x;
        box.y = l_output->y - geo.y;
        box.width = width;
        box.height = height;

        if (unscaled) {
            box.x *= scale;
            box.y *= scale;
            box.width *= scale;
            box.height *= scale;
        }

        pixman_region32_union_rect(&region, &region, box.x, box.y, box.width, box.height);

        if (!screencopy_create_buffer(region.extents.x2 - region.extents.x1,
                                      region.extents.y2 - region.extents.y1)) {
            pixman_region32_fini(&region);
            return false;
        }

        screencopy_output_create(l_output, NULL, &box, cursor);

        layout_output_count++;
    }
    pixman_region32_fini(&region);

    /* clear buffer */
    if (layout_output_count > 1) {
        screencopy_clear_buffer(manager->buffer);
    }

    manager->done = done;
    manager->data = data;
    manager->taking_screencopy = true;

    return true;
}

void screencopy_read_buffer(struct wlr_buffer *buffer, uint32_t format, uint32_t stride,
                            struct wlr_box *box, void *data)
{
    if (!wlr_renderer_begin_with_buffer(manager->server->renderer, buffer)) {
        return;
    }
    wlr_renderer_read_pixels(manager->server->renderer, format, stride, box->width, box->height, 0,
                             0, box->x, box->y, data);
    wlr_renderer_end(manager->server->renderer);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->destroy.link);
    wlr_buffer_drop(manager->buffer);
    free(manager);
    manager = NULL;
}

bool screencopy_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->outputs);

    manager->server = server;
    manager->destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->destroy);

    return true;
}
