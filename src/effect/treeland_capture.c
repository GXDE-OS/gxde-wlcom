/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <drm_fourcc.h>
#include <linux/input-event-codes.h>

#include <wayland-server-protocol.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/util/box.h>

#include <kywc/boxes.h>
#include <kywc/log.h>
#include <kywc/output.h>
#include <kywc/view.h>

#include "treeland-capture-unstable-v1-protocol.h"

#include "effect/capture.h"
#include "effect_p.h"
#include "input/cursor.h"
#include "input/event.h"
#include "input/input.h"
#include "input/seat.h"
#include "output.h"
#include "scene/scene.h"
#include "scene/thumbnail.h"
#include "server.h"
#include "view/view.h"

#define TCAP_MANAGER_VERSION 1

/* protocol source_type bitfield values */
#define TCAP_SOURCE_OUTPUT 0x1
#define TCAP_SOURCE_WINDOW 0x2
#define TCAP_SOURCE_REGION 0x4

struct tcap_manager {
    struct server *server;
    struct wl_global *global;

    /* only a single context can drive the interactive selector at a time */
    struct tcap_context *selecting;

    struct wl_listener server_destroy;
    struct wl_listener display_destroy;
};

enum tcap_sel_mode {
    TCAP_SEL_MODE_WINDOW,
    TCAP_SEL_MODE_OUTPUT,
    TCAP_SEL_MODE_REGION, /* region + window + output */
};

struct tcap_selector {
    struct tcap_context *context;
    struct tcap_manager *manager;
    struct seat *seat;

    struct seat_pointer_grab pointer_grab;
    struct seat_keyboard_grab keyboard_grab;

    enum tcap_sel_mode mode;
    bool destroying;

    struct ky_scene_tree *tree;
    struct ky_scene_rect *highlight;

    /* mask view whose input we temporarily bypass so we can hit windows below */
    struct view *mask_view;
    bool mask_bypassed;

    /* region drag state */
    bool pressed;
    bool dragging;
    double anchor_lx, anchor_ly;

    /* current hover target (in region/window/output picking) */
    struct kywc_view *hover_view;
    struct kywc_output *hover_output;
};

struct tcap_context {
    struct wl_resource *resource;
    struct tcap_manager *manager;

    /* select_source parameters */
    uint32_t source_hint;
    bool freeze;
    bool with_cursor;
    struct wlr_surface *mask;
    struct wl_listener mask_destroy;

    /* resolved source */
    bool source_ready;
    uint32_t source_type; /* protocol value */
    struct kywc_box region; /* capture region, layout (logical) coords */
    struct kywc_output *output; /* output to read / record */
    struct wl_listener output_destroy;
    struct kywc_view *view; /* window source, may be NULL */
    struct wl_listener view_destroy;

    struct tcap_selector *selector;
    struct tcap_frame *frame;
    struct tcap_session *session;
};

struct tcap_frame {
    struct wl_resource *resource;
    struct tcap_context *context;

    struct capture *capture;
    struct thumbnail *thumbnail;
    struct wlr_buffer *buffer;
    struct wl_listener buffer_update;
    struct wl_listener buffer_destroy;

    bool buffer_sent;
    bool failed;
    int width, height;
};

struct tcap_session {
    struct wl_resource *resource;
    struct tcap_context *context;

    bool started;
    struct capture *capture;
    struct wlr_buffer *buffer;
    struct wl_listener buffer_update;
    struct wl_listener buffer_destroy;

    bool awaiting_ack;
    int32_t crop_x, crop_y;
    struct {
        uint32_t sec_hi, sec_lo, usec;
    } ready;
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static void fill_time(uint32_t *sec_hi, uint32_t *sec_lo, uint32_t *usec)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t sec = (uint64_t)tv.tv_sec;
    *sec_hi = (uint32_t)(sec >> 32);
    *sec_lo = (uint32_t)(sec & 0xFFFFFFFF);
    *usec = (uint32_t)tv.tv_usec;
}

static void context_mask_origin(struct tcap_context *context, int *ox, int *oy)
{
    *ox = 0;
    *oy = 0;
    if (context->mask) {
        struct view *mask_view = view_try_from_wlr_surface(context->mask);
        if (mask_view) {
            *ox = mask_view->base.geometry.x;
            *oy = mask_view->base.geometry.y;
        }
    }
}

/* ------------------------------------------------------------------ */
/* interactive selector                                               */
/* ------------------------------------------------------------------ */

static void tcap_selector_destroy(struct tcap_selector *selector);

/* is this view the client mask (deepin's fullscreen selection overlay)? */
static bool view_is_mask(struct tcap_selector *selector, struct view *view,
                         struct wlr_surface *toplevel)
{
    struct tcap_context *context = selector->context;
    if (!context->mask) {
        return false;
    }
    if (selector->mask_view && view == selector->mask_view) {
        return true;
    }
    if (toplevel && toplevel == context->mask) {
        return true;
    }
    return false;
}

/*
 * Find the topmost normal window under the cursor, excluding the mask surface.
 *
 * The mask is deepin's fullscreen selection overlay which sits on top of the
 * windows we actually want to pick. We rely on input_bypass so ky_scene_node_at
 * skips it, but the mask view may not have been resolvable when the selector was
 * created (surface->data not yet set, differs per output/timing). So if a hit
 * ever lands on the mask, bypass it here and retry - this self-heals regardless
 * of when the mask became a mapped view.
 */
static struct kywc_view *selector_view_at(struct tcap_selector *selector, double lx, double ly)
{
    struct seat *seat = selector->seat;

    for (int attempt = 0; attempt < 8; attempt++) {
        double sx, sy;
        struct ky_scene_node *node = ky_scene_node_at(&seat->scene->tree.node, lx, ly, &sx, &sy);
        if (!node) {
            return NULL;
        }

        struct input_event_node *inode = input_event_node_from_node(node);
        struct wlr_surface *toplevel = inode ? input_event_node_toplevel(inode) : NULL;
        struct view *view = toplevel ? view_try_from_wlr_surface(toplevel) : NULL;

        /* landed on the mask: bypass it and look at what is behind */
        if (view_is_mask(selector, view, toplevel)) {
            struct view *mask = view ? view : view_try_from_wlr_surface(selector->context->mask);
            if (mask) {
                if (!selector->mask_bypassed) {
                    kywc_log(KYWC_INFO, "(treeland-capture) mask hit during hover, "
                                        "bypassing it to reach windows behind");
                }
                ky_scene_node_set_input_bypassed(&mask->tree->node, true);
                selector->mask_view = mask;
                selector->mask_bypassed = true;
                continue;
            }
            return NULL;
        }

        if (!view || !view->base.mapped) {
            return NULL;
        }
        /* only pick actual application windows, not wallpaper / panels */
        if (view->base.role != KYWC_VIEW_ROLE_NORMAL) {
            return NULL;
        }
        return &view->base;
    }
    return NULL;
}

static void selector_show_highlight(struct tcap_selector *selector, struct kywc_box *box)
{
    if (!selector->highlight) {
        return;
    }
    if (!box || box->width <= 0 || box->height <= 0) {
        ky_scene_node_set_enabled(&selector->highlight->node, false);
        return;
    }
    ky_scene_rect_set_size(selector->highlight, box->width, box->height);
    ky_scene_node_set_position(&selector->highlight->node, box->x, box->y);
    ky_scene_node_set_enabled(&selector->highlight->node, true);
}

static void selector_update_hover(struct tcap_selector *selector, double lx, double ly)
{
    selector->hover_view = NULL;
    selector->hover_output = NULL;
    struct kywc_box box = { 0, 0, 0, 0 };

    if (selector->mode == TCAP_SEL_MODE_OUTPUT) {
        struct kywc_output *output = kywc_output_at_point(lx, ly);
        if (output) {
            selector->hover_output = output;
            kywc_output_effective_geometry(output, &box);
        }
    } else if (selector->mode == TCAP_SEL_MODE_WINDOW) {
        struct kywc_view *view = selector_view_at(selector, lx, ly);
        if (view) {
            selector->hover_view = view;
            box = view->geometry;
        }
    } else { /* region mode: prefer window, fall back to output */
        struct kywc_view *view = selector_view_at(selector, lx, ly);
        if (view) {
            selector->hover_view = view;
            box = view->geometry;
        } else {
            struct kywc_output *output = kywc_output_at_point(lx, ly);
            if (output) {
                selector->hover_output = output;
                kywc_output_effective_geometry(output, &box);
            }
        }
    }

    selector_show_highlight(selector, &box);
}

static void selector_update_drag(struct tcap_selector *selector, double lx, double ly)
{
    struct kywc_box box;
    box.x = (int)(selector->anchor_lx < lx ? selector->anchor_lx : lx);
    box.y = (int)(selector->anchor_ly < ly ? selector->anchor_ly : ly);
    box.width = (int)(selector->anchor_lx < lx ? lx - selector->anchor_lx
                                               : selector->anchor_lx - lx);
    box.height = (int)(selector->anchor_ly < ly ? ly - selector->anchor_ly
                                                : selector->anchor_ly - ly);
    selector_show_highlight(selector, &box);
}

/* commit a resolved source and notify the client */
static void selector_commit(struct tcap_selector *selector, uint32_t source_type,
                            struct kywc_view *view, struct kywc_output *output,
                            struct kywc_box region)
{
    struct tcap_context *context = selector->context;

    if (region.width <= 0 || region.height <= 0) {
        /* nothing meaningful selected, keep selecting */
        return;
    }

    context->source_type = source_type;
    context->region = region;

    /* resolve output to use for reading/recording */
    if (output) {
        context->output = output;
    } else if (view) {
        struct view *v = view_from_kywc_view(view);
        context->output = v ? v->output : NULL;
    }
    if (!context->output) {
        context->output = kywc_output_at_point(region.x + region.width / 2.0,
                                               region.y + region.height / 2.0);
    }

    context->view = (source_type == TCAP_SOURCE_WINDOW) ? view : NULL;

    /* listen for source disappearing */
    if (context->view) {
        wl_signal_add(&context->view->events.destroy, &context->view_destroy);
    }
    if (context->output) {
        wl_signal_add(&context->output->events.destroy, &context->output_destroy);
    }

    context->source_ready = true;

    kywc_log(KYWC_INFO,
             "(treeland-capture) source selected: type=%s region=%d,%d %dx%d output=%s",
             source_type == TCAP_SOURCE_WINDOW ? "window"
                 : source_type == TCAP_SOURCE_OUTPUT ? "output" : "region",
             region.x, region.y, region.width, region.height,
             context->output ? context->output->name : "(none)");

    int ox, oy;
    context_mask_origin(context, &ox, &oy);
    treeland_capture_context_v1_send_source_ready(context->resource, region.x - ox, region.y - oy,
                                                  region.width, region.height, source_type);

    /* selection finished, tear the selector down */
    tcap_selector_destroy(selector);
}

static void selector_finish_pick(struct tcap_selector *selector)
{
    if (selector->mode == TCAP_SEL_MODE_OUTPUT) {
        if (selector->hover_output) {
            struct kywc_box box;
            kywc_output_effective_geometry(selector->hover_output, &box);
            selector_commit(selector, TCAP_SOURCE_OUTPUT, NULL, selector->hover_output, box);
        }
    } else if (selector->mode == TCAP_SEL_MODE_WINDOW) {
        if (selector->hover_view) {
            selector_commit(selector, TCAP_SOURCE_WINDOW, selector->hover_view, NULL,
                            selector->hover_view->geometry);
        }
    } else { /* region mode click -> pick the hovered window or output */
        if (selector->hover_view) {
            selector_commit(selector, TCAP_SOURCE_WINDOW, selector->hover_view, NULL,
                            selector->hover_view->geometry);
        } else if (selector->hover_output) {
            struct kywc_box box;
            kywc_output_effective_geometry(selector->hover_output, &box);
            selector_commit(selector, TCAP_SOURCE_OUTPUT, NULL, selector->hover_output, box);
        }
    }
}

static void selector_finish_region(struct tcap_selector *selector, double lx, double ly)
{
    struct kywc_box box;
    box.x = (int)(selector->anchor_lx < lx ? selector->anchor_lx : lx);
    box.y = (int)(selector->anchor_ly < ly ? selector->anchor_ly : ly);
    box.width = (int)(selector->anchor_lx < lx ? lx - selector->anchor_lx
                                               : selector->anchor_lx - lx);
    box.height = (int)(selector->anchor_ly < ly ? ly - selector->anchor_ly
                                                : selector->anchor_ly - ly);
    selector_commit(selector, TCAP_SOURCE_REGION, NULL, NULL, box);
}

static void selector_cancel(struct tcap_selector *selector)
{
    struct tcap_context *context = selector->context;
    treeland_capture_context_v1_send_source_failed(
        context->resource, TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_USER_CANCEL);
    tcap_selector_destroy(selector);
}

/* --- pointer grab --- */

static bool selector_pointer_motion(struct seat_pointer_grab *grab, uint32_t time, double lx,
                                    double ly)
{
    struct tcap_selector *selector = grab->data;

    if (selector->pressed && selector->mode == TCAP_SEL_MODE_REGION) {
        double dx = lx - selector->anchor_lx;
        double dy = ly - selector->anchor_ly;
        if (!selector->dragging && (dx * dx + dy * dy) > 9.0) {
            selector->dragging = true;
        }
        if (selector->dragging) {
            selector_update_drag(selector, lx, ly);
            return true;
        }
    }

    selector_update_hover(selector, lx, ly);
    return true;
}

static bool selector_pointer_button(struct seat_pointer_grab *grab, uint32_t time, uint32_t button,
                                    bool pressed)
{
    struct tcap_selector *selector = grab->data;
    struct seat *seat = selector->seat;

    if (button == BTN_RIGHT) {
        if (pressed) {
            selector_cancel(selector);
        }
        return true;
    }

    if (button != BTN_LEFT) {
        return true;
    }

    if (pressed) {
        selector->pressed = true;
        selector->dragging = false;
        selector->anchor_lx = seat->cursor->lx;
        selector->anchor_ly = seat->cursor->ly;
        return true;
    }

    /* released */
    bool was_dragging = selector->dragging;
    selector->pressed = false;
    selector->dragging = false;

    if (was_dragging && selector->mode == TCAP_SEL_MODE_REGION) {
        selector_finish_region(selector, seat->cursor->lx, seat->cursor->ly);
    } else {
        selector_finish_pick(selector);
    }
    return true;
}

static bool selector_pointer_axis(struct seat_pointer_grab *grab, uint32_t time, bool vertical,
                                  double value)
{
    return true;
}

static void selector_pointer_cancel(struct seat_pointer_grab *grab)
{
    struct tcap_selector *selector = grab->data;
    selector_cancel(selector);
}

static const struct seat_pointer_grab_interface selector_pointer_impl = {
    .motion = selector_pointer_motion,
    .button = selector_pointer_button,
    .axis = selector_pointer_axis,
    .cancel = selector_pointer_cancel,
};

/* --- keyboard grab --- */

static bool selector_keyboard_key(struct seat_keyboard_grab *grab, struct keyboard *keyboard,
                                  uint32_t time, uint32_t key, bool pressed, uint32_t modifiers)
{
    struct tcap_selector *selector = grab->data;
    if (key == KEY_ESC && pressed) {
        selector_cancel(selector);
    }
    /* swallow all keys while selecting */
    return true;
}

static void selector_keyboard_cancel(struct seat_keyboard_grab *grab)
{
    struct tcap_selector *selector = grab->data;
    selector_cancel(selector);
}

static const struct seat_keyboard_grab_interface selector_keyboard_impl = {
    .key = selector_keyboard_key,
    .cancel = selector_keyboard_cancel,
};

static enum tcap_sel_mode selector_mode_from_hint(uint32_t hint)
{
    /* an unset or region-inclusive hint gives the flexible region selector */
    if (hint == 0 || (hint & TCAP_SOURCE_REGION)) {
        return TCAP_SEL_MODE_REGION;
    }
    if (hint & TCAP_SOURCE_WINDOW) {
        return TCAP_SEL_MODE_WINDOW;
    }
    return TCAP_SEL_MODE_OUTPUT;
}

static struct tcap_selector *tcap_selector_create(struct tcap_context *context)
{
    struct seat *seat = input_manager_get_default_seat();
    if (!seat || !seat->scene) {
        return NULL;
    }

    struct tcap_selector *selector = calloc(1, sizeof(*selector));
    if (!selector) {
        return NULL;
    }

    selector->context = context;
    selector->manager = context->manager;
    selector->seat = seat;
    selector->mode = selector_mode_from_hint(context->source_hint);

    /* overlay tree with a translucent highlight rect */
    selector->tree = ky_scene_tree_create(&seat->scene->tree);
    if (selector->tree) {
        static const float color[4] = { 0.16f, 0.5f, 1.0f, 0.25f };
        selector->highlight = ky_scene_rect_create(selector->tree, 1, 1, color);
        if (selector->highlight) {
            ky_scene_node_set_input_bypassed(&selector->highlight->node, true);
            ky_scene_node_set_enabled(&selector->highlight->node, false);
        }
        ky_scene_node_raise_to_top(&selector->tree->node);
    }

    /* let hit-testing pass through the client mask to the windows below it */
    selector->mask_view = context->mask ? view_try_from_wlr_surface(context->mask) : NULL;
    if (selector->mask_view) {
        ky_scene_node_set_input_bypassed(&selector->mask_view->tree->node, true);
        selector->mask_bypassed = true;
    }

    kywc_log(KYWC_INFO,
             "(treeland-capture) selector start: mode=%d hint=0x%x freeze=%d cursor=%.0f,%.0f "
             "mask=%p mask_view=%p%s",
             selector->mode, context->source_hint, context->freeze, seat->cursor->lx,
             seat->cursor->ly, (void *)context->mask, (void *)selector->mask_view,
             (context->mask && !selector->mask_view) ? " (mask view unresolved!)" : "");

    selector->pointer_grab.interface = &selector_pointer_impl;
    selector->pointer_grab.data = selector;
    selector->keyboard_grab.interface = &selector_keyboard_impl;
    selector->keyboard_grab.data = selector;
    seat_start_pointer_grab(seat, &selector->pointer_grab);
    seat_start_keyboard_grab(seat, &selector->keyboard_grab);

    context->selector = selector;
    context->manager->selecting = context;

    /* initial highlight at the current cursor position */
    selector_update_hover(selector, seat->cursor->lx, seat->cursor->ly);

    return selector;
}

static void tcap_selector_destroy(struct tcap_selector *selector)
{
    if (!selector || selector->destroying) {
        return;
    }
    selector->destroying = true;

    struct tcap_context *context = selector->context;
    struct seat *seat = selector->seat;

    /* safe even if our grab was already replaced (no-op unless still ours) */
    seat_end_pointer_grab(seat, &selector->pointer_grab);
    seat_end_keyboard_grab(seat, &selector->keyboard_grab);

    if (selector->mask_bypassed && selector->mask_view) {
        ky_scene_node_set_input_bypassed(&selector->mask_view->tree->node, false);
    }

    if (selector->tree) {
        ky_scene_node_destroy(&selector->tree->node);
    }

    if (context) {
        context->selector = NULL;
    }
    if (selector->manager && selector->manager->selecting == context) {
        selector->manager->selecting = NULL;
    }

    free(selector);
}

/* ------------------------------------------------------------------ */
/* one-shot frame capture                                             */
/* ------------------------------------------------------------------ */

static void tcap_frame_teardown(struct tcap_frame *frame)
{
    wl_list_remove(&frame->buffer_update.link);
    wl_list_remove(&frame->buffer_destroy.link);
    wl_list_init(&frame->buffer_update.link);
    wl_list_init(&frame->buffer_destroy.link);

    if (frame->buffer) {
        wlr_buffer_unlock(frame->buffer);
        frame->buffer = NULL;
    }
    if (frame->capture) {
        capture_destroy(frame->capture);
        frame->capture = NULL;
    }
    if (frame->thumbnail) {
        thumbnail_destroy(frame->thumbnail);
        frame->thumbnail = NULL;
    }
}

static void frame_send_buffer_from(struct tcap_frame *frame, struct wlr_buffer *buffer)
{
    if (frame->buffer_sent || !buffer) {
        return;
    }

    wlr_buffer_unlock(frame->buffer);
    frame->buffer = wlr_buffer_lock(buffer);
    frame->width = buffer->width;
    frame->height = buffer->height;

    treeland_capture_frame_v1_send_buffer(frame->resource, WL_SHM_FORMAT_ARGB8888, frame->width,
                                          frame->height, (uint32_t)frame->width * 4);
    treeland_capture_frame_v1_send_buffer_done(frame->resource);
    frame->buffer_sent = true;

    /* we only need one frame, stop further updates */
    if (frame->capture) {
        capture_mark_wants_update(frame->capture, false, false);
    }
    if (frame->thumbnail) {
        thumbnail_mark_wants_update(frame->thumbnail, false);
    }
}

static void frame_handle_buffer_update(struct wl_listener *listener, void *data)
{
    struct tcap_frame *frame = wl_container_of(listener, frame, buffer_update);
    struct wlr_buffer *buffer = NULL;

    if (frame->capture) {
        struct capture_update_event *event = data;
        buffer = event->buffer;
    } else if (frame->thumbnail) {
        struct thumbnail_update_event *event = data;
        buffer = event->buffer;
    }
    frame_send_buffer_from(frame, buffer);
}

static void frame_handle_buffer_destroy(struct wl_listener *listener, void *data)
{
    struct tcap_frame *frame = wl_container_of(listener, frame, buffer_destroy);
    /* the engine is going away; forget it so teardown does not double free */
    frame->capture = NULL;
    frame->thumbnail = NULL;
    if (!frame->failed) {
        frame->failed = true;
        treeland_capture_frame_v1_send_failed(frame->resource);
    }
    tcap_frame_teardown(frame);
}

static void frame_handle_copy(struct wl_client *client, struct wl_resource *resource,
                              struct wl_resource *buffer_resource)
{
    struct tcap_frame *frame = wl_resource_get_user_data(resource);
    if (!frame) {
        return;
    }
    if (frame->failed || !frame->buffer) {
        treeland_capture_frame_v1_send_failed(resource);
        return;
    }

    struct wlr_buffer *dst = wlr_buffer_try_from_resource(buffer_resource);
    if (!dst) {
        treeland_capture_frame_v1_send_failed(resource);
        return;
    }

    void *data = NULL;
    uint32_t format = 0;
    size_t stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(dst, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format,
                                          &stride)) {
        treeland_capture_frame_v1_send_failed(resource);
        return;
    }

    struct wlr_box box = { 0, 0, frame->width, frame->height };
    if (box.width > dst->width) {
        box.width = dst->width;
    }
    if (box.height > dst->height) {
        box.height = dst->height;
    }
    capture_read_buffer(frame->buffer, format, (uint32_t)stride, &box, data);
    wlr_buffer_end_data_ptr_access(dst);

    treeland_capture_frame_v1_send_ready(resource);
}

static void frame_handle_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct treeland_capture_frame_v1_interface frame_impl = {
    .destroy = frame_handle_destroy_request,
    .copy = frame_handle_copy,
};

static void frame_handle_resource_destroy(struct wl_resource *resource)
{
    struct tcap_frame *frame = wl_resource_get_user_data(resource);
    if (!frame) {
        return;
    }
    tcap_frame_teardown(frame);
    if (frame->context && frame->context->frame == frame) {
        frame->context->frame = NULL;
    }
    free(frame);
}

static void context_handle_capture(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t id)
{
    struct tcap_context *context = wl_resource_get_user_data(resource);

    struct tcap_frame *frame = calloc(1, sizeof(*frame));
    if (!frame) {
        wl_client_post_no_memory(client);
        return;
    }
    frame->resource =
        wl_resource_create(client, &treeland_capture_frame_v1_interface,
                           wl_resource_get_version(resource), id);
    if (!frame->resource) {
        free(frame);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(frame->resource, &frame_impl, frame,
                                   frame_handle_resource_destroy);
    wl_list_init(&frame->buffer_update.link);
    wl_list_init(&frame->buffer_destroy.link);
    frame->buffer_update.notify = frame_handle_buffer_update;
    frame->buffer_destroy.notify = frame_handle_buffer_destroy;

    if (!context) {
        frame->failed = true;
        treeland_capture_frame_v1_send_failed(frame->resource);
        return;
    }
    frame->context = context;
    context->frame = frame;

    if (!context->source_ready) {
        frame->failed = true;
        treeland_capture_frame_v1_send_failed(frame->resource);
        return;
    }

    uint32_t options = context->with_cursor ? CAPTURE_NEED_CURSOR : CAPTURE_NEED_NONE;

    if (context->source_type == TCAP_SOURCE_WINDOW && context->view) {
        struct view *view = view_from_kywc_view(context->view);
        if (view) {
            frame->thumbnail =
                thumbnail_create_from_view(view, THUMBNAIL_DISABLE_SHADOW, 1.0);
        }
        if (!frame->thumbnail) {
            frame->failed = true;
            treeland_capture_frame_v1_send_failed(frame->resource);
            return;
        }
        thumbnail_add_update_listener(frame->thumbnail, &frame->buffer_update);
        thumbnail_add_destroy_listener(frame->thumbnail, &frame->buffer_destroy);
    } else {
        struct wlr_box area = { context->region.x, context->region.y, context->region.width,
                                context->region.height };
        frame->capture = capture_create_from_area(&area, options);
        if (!frame->capture) {
            frame->failed = true;
            treeland_capture_frame_v1_send_failed(frame->resource);
            return;
        }
        capture_add_update_listener(frame->capture, &frame->buffer_update);
        capture_add_destroy_listener(frame->capture, &frame->buffer_destroy);
    }
}

/* ------------------------------------------------------------------ */
/* recording session                                                  */
/* ------------------------------------------------------------------ */

static void tcap_session_teardown(struct tcap_session *session)
{
    wl_list_remove(&session->buffer_update.link);
    wl_list_remove(&session->buffer_destroy.link);
    wl_list_init(&session->buffer_update.link);
    wl_list_init(&session->buffer_destroy.link);

    if (session->buffer) {
        wlr_buffer_unlock(session->buffer);
        session->buffer = NULL;
    }
    if (session->capture) {
        capture_destroy(session->capture);
        session->capture = NULL;
    }
}

static void session_send_frame(struct tcap_session *session, struct wlr_buffer *buffer)
{
    struct wlr_dmabuf_attributes dmabuf;
    if (!wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        /* recording requires a dmabuf source */
        treeland_capture_session_v1_send_cancel(
            session->resource, TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
        return;
    }

    wlr_buffer_unlock(session->buffer);
    session->buffer = wlr_buffer_lock(buffer);

    union {
        uint64_t modifier;
        struct {
            uint32_t lo;
            uint32_t hi;
        };
    } mod = { .modifier = dmabuf.modifier };

    treeland_capture_session_v1_send_frame(session->resource, session->crop_x, session->crop_y,
                                           buffer->width, buffer->height, 0, 0, dmabuf.format,
                                           mod.hi, mod.lo, dmabuf.n_planes);
    for (int i = 0; i < dmabuf.n_planes; i++) {
        treeland_capture_session_v1_send_object(session->resource, i, dmabuf.fd[i],
                                                dmabuf.stride[i] * buffer->height, dmabuf.offset[i],
                                                dmabuf.stride[i], i);
    }

    fill_time(&session->ready.sec_hi, &session->ready.sec_lo, &session->ready.usec);
    session->awaiting_ack = true;
    treeland_capture_session_v1_send_ready(session->resource, session->ready.sec_hi,
                                           session->ready.sec_lo, session->ready.usec);

    /* wait for the client's frame_done before producing the next frame */
    capture_mark_wants_update(session->capture, false, false);
}

static void session_handle_buffer_update(struct wl_listener *listener, void *data)
{
    struct tcap_session *session = wl_container_of(listener, session, buffer_update);
    if (session->awaiting_ack) {
        return;
    }
    struct capture_update_event *event = data;
    session_send_frame(session, event->buffer);
}

static void session_handle_buffer_destroy(struct wl_listener *listener, void *data)
{
    struct tcap_session *session = wl_container_of(listener, session, buffer_destroy);
    session->capture = NULL;
    treeland_capture_session_v1_send_cancel(
        session->resource, TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
    tcap_session_teardown(session);
}

static void session_handle_start(struct wl_client *client, struct wl_resource *resource)
{
    struct tcap_session *session = wl_resource_get_user_data(resource);
    if (!session || session->started) {
        return;
    }
    struct tcap_context *context = session->context;
    if (!context || !context->source_ready || !context->output) {
        treeland_capture_session_v1_send_cancel(
            resource, TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
        return;
    }

    struct output *output = output_from_kywc_output(context->output);
    if (!output) {
        treeland_capture_session_v1_send_cancel(
            resource, TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
        return;
    }

    uint32_t options = context->with_cursor ? CAPTURE_NEED_CURSOR : CAPTURE_NEED_NONE;
    session->capture = capture_create_from_output(output, options);
    if (!session->capture) {
        treeland_capture_session_v1_send_cancel(
            resource, TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
        return;
    }

    /* crop offset relative to the client mask origin */
    int ox, oy;
    context_mask_origin(context, &ox, &oy);
    session->crop_x = context->region.x - ox;
    session->crop_y = context->region.y - oy;

    session->started = true;
    session->awaiting_ack = false;
    capture_add_update_listener(session->capture, &session->buffer_update);
    capture_add_destroy_listener(session->capture, &session->buffer_destroy);
    capture_mark_wants_update(session->capture, true, true);
}

static void session_handle_frame_done(struct wl_client *client, struct wl_resource *resource,
                                      uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_usec)
{
    struct tcap_session *session = wl_resource_get_user_data(resource);
    if (!session) {
        return;
    }
    if (session->ready.sec_hi == tv_sec_hi && session->ready.sec_lo == tv_sec_lo &&
        session->ready.usec == tv_usec) {
        session->awaiting_ack = false;
        if (session->capture) {
            /* request the next frame; force so a static screen still streams */
            capture_mark_wants_update(session->capture, true, true);
        }
    } else {
        kywc_log(KYWC_DEBUG, "(treeland-capture) stale frame_done ignored");
    }
}

static void session_handle_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct treeland_capture_session_v1_interface session_impl = {
    .destroy = session_handle_destroy_request,
    .start = session_handle_start,
    .frame_done = session_handle_frame_done,
};

static void session_handle_resource_destroy(struct wl_resource *resource)
{
    struct tcap_session *session = wl_resource_get_user_data(resource);
    if (!session) {
        return;
    }
    tcap_session_teardown(session);
    if (session->context && session->context->session == session) {
        session->context->session = NULL;
    }
    free(session);
}

static void context_handle_create_session(struct wl_client *client, struct wl_resource *resource,
                                          uint32_t id)
{
    struct tcap_context *context = wl_resource_get_user_data(resource);

    struct tcap_session *session = calloc(1, sizeof(*session));
    if (!session) {
        wl_client_post_no_memory(client);
        return;
    }
    session->resource =
        wl_resource_create(client, &treeland_capture_session_v1_interface,
                           wl_resource_get_version(resource), id);
    if (!session->resource) {
        free(session);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(session->resource, &session_impl, session,
                                   session_handle_resource_destroy);
    wl_list_init(&session->buffer_update.link);
    wl_list_init(&session->buffer_destroy.link);
    session->buffer_update.notify = session_handle_buffer_update;
    session->buffer_destroy.notify = session_handle_buffer_destroy;

    session->context = context;
    if (context) {
        context->session = session;
    }
}

static void context_source_lost(struct tcap_context *context, uint32_t reason)
{
    if (context->session) {
        treeland_capture_session_v1_send_cancel(context->session->resource, reason);
        tcap_session_teardown(context->session);
    }
    if (context->frame && !context->frame->failed && !context->frame->buffer_sent) {
        context->frame->failed = true;
        treeland_capture_frame_v1_send_failed(context->frame->resource);
        tcap_frame_teardown(context->frame);
    }
}

static void context_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct tcap_context *context = wl_container_of(listener, context, view_destroy);
    wl_list_remove(&context->view_destroy.link);
    wl_list_init(&context->view_destroy.link);
    context->view = NULL;
    context->source_ready = false;
    context_source_lost(context, TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
}

static void context_handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct tcap_context *context = wl_container_of(listener, context, output_destroy);
    wl_list_remove(&context->output_destroy.link);
    wl_list_init(&context->output_destroy.link);
    context->output = NULL;
    context->source_ready = false;
    context_source_lost(context, TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
}

static void context_handle_mask_destroy(struct wl_listener *listener, void *data)
{
    struct tcap_context *context = wl_container_of(listener, context, mask_destroy);
    wl_list_remove(&context->mask_destroy.link);
    wl_list_init(&context->mask_destroy.link);
    context->mask = NULL;
}

static void context_reset_source(struct tcap_context *context)
{
    wl_list_remove(&context->view_destroy.link);
    wl_list_init(&context->view_destroy.link);
    wl_list_remove(&context->output_destroy.link);
    wl_list_init(&context->output_destroy.link);
    context->view = NULL;
    context->output = NULL;
    context->source_ready = false;
}

static void context_handle_select_source(struct wl_client *client, struct wl_resource *resource,
                                         uint32_t source_hint, uint32_t freeze,
                                         uint32_t with_cursor, struct wl_resource *mask_resource)
{
    struct tcap_context *context = wl_resource_get_user_data(resource);

    if (context->manager->selecting && context->manager->selecting != context) {
        treeland_capture_context_v1_send_source_failed(
            resource, TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_SELECTOR_BUSY);
        return;
    }
    if (context->selector) {
        /* already selecting for this context */
        return;
    }

    context_reset_source(context);

    context->source_hint = source_hint;
    context->freeze = freeze;
    context->with_cursor = with_cursor;

    if (context->mask) {
        wl_list_remove(&context->mask_destroy.link);
        wl_list_init(&context->mask_destroy.link);
        context->mask = NULL;
    }
    if (mask_resource) {
        context->mask = wlr_surface_from_resource(mask_resource);
        if (context->mask) {
            wl_signal_add(&context->mask->events.destroy, &context->mask_destroy);
        }
    }

    if (!tcap_selector_create(context)) {
        treeland_capture_context_v1_send_source_failed(
            resource, TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_OTHER);
    }
}

static void context_handle_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct treeland_capture_context_v1_interface context_impl = {
    .destroy = context_handle_destroy_request,
    .select_source = context_handle_select_source,
    .capture = context_handle_capture,
    .create_session = context_handle_create_session,
};

static void context_handle_resource_destroy(struct wl_resource *resource)
{
    struct tcap_context *context = wl_resource_get_user_data(resource);
    if (!context) {
        return;
    }

    if (context->selector) {
        tcap_selector_destroy(context->selector);
    }
    if (context->manager->selecting == context) {
        context->manager->selecting = NULL;
    }

    /* detach any live frame/session so their handlers stop touching us */
    if (context->frame) {
        tcap_frame_teardown(context->frame);
        context->frame->context = NULL;
        context->frame = NULL;
    }
    if (context->session) {
        tcap_session_teardown(context->session);
        context->session->context = NULL;
        context->session = NULL;
    }

    wl_list_remove(&context->mask_destroy.link);
    wl_list_remove(&context->output_destroy.link);
    wl_list_remove(&context->view_destroy.link);

    free(context);
}

static void manager_handle_get_context(struct wl_client *client, struct wl_resource *resource,
                                       uint32_t id)
{
    struct tcap_manager *manager = wl_resource_get_user_data(resource);

    struct tcap_context *context = calloc(1, sizeof(*context));
    if (!context) {
        wl_client_post_no_memory(client);
        return;
    }
    context->resource =
        wl_resource_create(client, &treeland_capture_context_v1_interface,
                           wl_resource_get_version(resource), id);
    if (!context->resource) {
        free(context);
        wl_client_post_no_memory(client);
        return;
    }
    context->manager = manager;
    wl_list_init(&context->mask_destroy.link);
    wl_list_init(&context->output_destroy.link);
    wl_list_init(&context->view_destroy.link);
    context->mask_destroy.notify = context_handle_mask_destroy;
    context->output_destroy.notify = context_handle_output_destroy;
    context->view_destroy.notify = context_handle_view_destroy;

    wl_resource_set_implementation(context->resource, &context_impl, context,
                                   context_handle_resource_destroy);
}

static void manager_handle_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct treeland_capture_manager_v1_interface manager_impl = {
    .destroy = manager_handle_destroy_request,
    .get_context = manager_handle_get_context,
};

static void manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct tcap_manager *manager = data;
    struct wl_resource *resource =
        wl_resource_create(client, &treeland_capture_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &manager_impl, manager, NULL);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct tcap_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct tcap_manager *manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

bool treeland_capture_manager_create(struct server *server)
{
    struct tcap_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->server = server;
    manager->global = wl_global_create(server->display, &treeland_capture_manager_v1_interface,
                                       TCAP_MANAGER_VERSION, manager, manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "treeland capture manager create failed");
        free(manager);
        return false;
    }

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}
