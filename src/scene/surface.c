// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_presentation_time.h>

#include "scene/surface.h"

static void handle_scene_buffer_outputs_update(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, outputs_update);
    if (surface->buffer->primary_output == NULL) {
        return;
    }
    double scale = surface->buffer->primary_output->output->scale;
    wlr_fractional_scale_v1_notify_scale(surface->surface, scale);
    wlr_surface_set_preferred_buffer_scale(surface->surface, ceil(scale));
}

static void handle_scene_buffer_output_enter(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, output_enter);
    struct ky_scene_output *output = data;

    wlr_surface_send_enter(surface->surface, output->output);
}

static void handle_scene_buffer_output_leave(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, output_leave);
    struct ky_scene_output *output = data;

    wlr_surface_send_leave(surface->surface, output->output);
}

static void handle_scene_buffer_output_sample(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, output_sample);
    const struct ky_scene_output_sample_event *event = data;
    struct ky_scene_output *scene_output = event->output;

    if (surface->buffer->primary_output != scene_output) {
        return;
    }

    struct ky_scene *root = ky_scene_from_node(&surface->buffer->node);
    if (!root->presentation) {
        return;
    }

    if (event->direct_scanout) {
        wlr_presentation_surface_scanned_out_on_output(root->presentation, surface->surface,
                                                       scene_output->output);
    } else {
        wlr_presentation_surface_textured_on_output(root->presentation, surface->surface,
                                                    scene_output->output);
    }
}

static void handle_scene_buffer_frame_done(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, frame_done);
    struct timespec *now = data;

    wlr_surface_send_frame_done(surface->surface, now);
}

static void scene_surface_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, surface_destroy);

    ky_scene_node_destroy(&surface->buffer->node);
}

// This is used for wlr_scene where it unconditionally locks buffers preventing
// reuse of the existing texture for shm clients. With the usage pattern of
// wlr_scene surface handling, we can mark its locked buffer as safe
// for mutation.
static void client_buffer_mark_next_can_damage(struct wlr_client_buffer *buffer)
{
    buffer->n_ignore_locks++;
}

static void scene_buffer_unmark_client_buffer(struct ky_scene_buffer *scene_buffer)
{
    if (!scene_buffer->buffer) {
        return;
    }

    struct wlr_client_buffer *buffer = wlr_client_buffer_get(scene_buffer->buffer);
    if (!buffer) {
        return;
    }

    assert(buffer->n_ignore_locks > 0);
    buffer->n_ignore_locks--;
}

static void set_buffer_with_surface_state(struct ky_scene_buffer *scene_buffer,
                                          struct wlr_surface *surface)
{
    struct wlr_surface_state *state = &surface->current;

    ky_scene_buffer_set_opaque_region(scene_buffer, &surface->opaque_region);

    struct wlr_fbox src_box;
    wlr_surface_get_buffer_source_box(surface, &src_box);
    ky_scene_buffer_set_source_box(scene_buffer, &src_box);

    ky_scene_buffer_set_dest_size(scene_buffer, state->width, state->height);
    ky_scene_buffer_set_transform(scene_buffer, state->transform);

    scene_buffer_unmark_client_buffer(scene_buffer);

    if (surface->buffer) {
        client_buffer_mark_next_can_damage(surface->buffer);

        ky_scene_buffer_set_buffer_with_damage(scene_buffer, &surface->buffer->base,
                                               &surface->buffer_damage);
    } else {
        ky_scene_buffer_set_buffer(scene_buffer, NULL);
    }
}

static void handle_scene_surface_surface_commit(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, surface_commit);
    struct ky_scene_buffer *scene_buffer = surface->buffer;

    set_buffer_with_surface_state(scene_buffer, surface->surface);

    // If the surface has requested a frame done event, honour that. The
    // frame_callback_list will be populated in this case. We should only
    // schedule the frame however if the node is enabled and there is an
    // output intersecting, otherwise the frame done events would never reach
    // the surface anyway.
    int lx, ly;
    bool enabled = ky_scene_node_coords(&scene_buffer->node, &lx, &ly);

    struct ky_scene_output *primary_output = surface->buffer->primary_output;
    if (!wl_list_empty(&surface->surface->current.frame_callback_list) && primary_output != NULL &&
        enabled) {
        wlr_output_schedule_frame(primary_output->output);
    }
}

static bool scene_buffer_point_accepts_input(struct ky_scene_buffer *scene_buffer, double *sx,
                                             double *sy)
{
    struct ky_scene_surface *scene_surface = ky_scene_surface_try_from_buffer(scene_buffer);

    return wlr_surface_point_accepts_input(scene_surface->surface, *sx, *sy);
}

static void surface_addon_destroy(struct wlr_addon *addon)
{
    struct ky_scene_surface *surface = wl_container_of(addon, surface, addon);

    scene_buffer_unmark_client_buffer(surface->buffer);

    wlr_addon_finish(&surface->addon);
    wlr_addon_finish(&surface->node_addon);

    wl_list_remove(&surface->outputs_update.link);
    wl_list_remove(&surface->output_enter.link);
    wl_list_remove(&surface->output_leave.link);
    wl_list_remove(&surface->output_sample.link);
    wl_list_remove(&surface->frame_done.link);
    wl_list_remove(&surface->surface_destroy.link);
    wl_list_remove(&surface->surface_commit.link);

    free(surface);
}

static const struct wlr_addon_interface surface_addon_impl = {
    .name = "ky_scene_surface",
    .destroy = surface_addon_destroy,
};

struct ky_scene_surface *ky_scene_surface_try_from_buffer(struct ky_scene_buffer *scene_buffer)
{
    struct wlr_addon *addon =
        wlr_addon_find(&scene_buffer->node.addons, scene_buffer, &surface_addon_impl);
    if (!addon) {
        return NULL;
    }

    struct ky_scene_surface *surface = wl_container_of(addon, surface, addon);
    return surface;
}

static void surface_node_addon_destroy(struct wlr_addon *addon)
{
    /* do nothing, surface destroy singal emitted before surface addon_set finish
     * scene node destroy will call surface_addon_destroy.
     */
}

static const struct wlr_addon_interface surface_node_addon_impl = {
    .name = "ky_scene_surface_node",
    .destroy = surface_node_addon_destroy,
};

static struct ky_scene_surface *ky_scene_surface_try_from_surface(struct wlr_surface *wlr_surface)
{
    struct wlr_addon *node_addon =
        wlr_addon_find(&wlr_surface->addons, wlr_surface, &surface_node_addon_impl);
    if (!node_addon) {
        return NULL;
    }

    struct ky_scene_surface *surface = wl_container_of(node_addon, surface, node_addon);
    return surface;
}

struct ky_scene_surface *ky_scene_surface_create(struct ky_scene_tree *parent,
                                                 struct wlr_surface *wlr_surface)
{
    struct ky_scene_surface *surface = calloc(1, sizeof(*surface));
    if (surface == NULL) {
        return NULL;
    }

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_create(parent, NULL);
    if (!scene_buffer) {
        free(surface);
        return NULL;
    }

    surface->has_slide = false;
    surface->buffer = scene_buffer;
    surface->surface = wlr_surface;
    scene_buffer->point_accepts_input = scene_buffer_point_accepts_input;

    surface->outputs_update.notify = handle_scene_buffer_outputs_update;
    wl_signal_add(&scene_buffer->events.outputs_update, &surface->outputs_update);

    surface->output_enter.notify = handle_scene_buffer_output_enter;
    wl_signal_add(&scene_buffer->events.output_enter, &surface->output_enter);

    surface->output_leave.notify = handle_scene_buffer_output_leave;
    wl_signal_add(&scene_buffer->events.output_leave, &surface->output_leave);

    surface->output_sample.notify = handle_scene_buffer_output_sample;
    wl_signal_add(&scene_buffer->events.output_sample, &surface->output_sample);

    surface->frame_done.notify = handle_scene_buffer_frame_done;
    wl_signal_add(&scene_buffer->events.frame_done, &surface->frame_done);

    surface->surface_destroy.notify = scene_surface_handle_surface_destroy;
    wl_signal_add(&wlr_surface->events.destroy, &surface->surface_destroy);

    surface->surface_commit.notify = handle_scene_surface_surface_commit;
    wl_signal_add(&wlr_surface->events.commit, &surface->surface_commit);

    wlr_addon_init(&surface->addon, &scene_buffer->node.addons, scene_buffer, &surface_addon_impl);
    wlr_addon_init(&surface->node_addon, &wlr_surface->addons, wlr_surface,
                   &surface_node_addon_impl);

    set_buffer_with_surface_state(scene_buffer, wlr_surface);

    return surface;
}

struct wlr_surface *wlr_surface_try_from_node(struct ky_scene_node *node)
{
    if (node->type != KY_SCENE_NODE_BUFFER) {
        return NULL;
    }

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);
    if (!scene_buffer) {
        return NULL;
    }

    struct ky_scene_surface *scene_surface = ky_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    return scene_surface->surface;
}

struct ky_scene_buffer *ky_scene_buffer_try_from_surface(struct wlr_surface *wlr_surface)
{
    struct ky_scene_surface *scene_surface = ky_scene_surface_try_from_surface(wlr_surface);
    if (!scene_surface) {
        return NULL;
    }

    return scene_surface->buffer;
}

void ky_scene_surface_unset_slide(struct wlr_surface *wlr_surface)
{
    struct ky_scene_surface *scene_surface = ky_scene_surface_try_from_surface(wlr_surface);
    if (!scene_surface) {
        return;
    }
    scene_surface->has_slide = false;
}

void ky_scene_surface_set_slide(struct wlr_surface *wlr_surface, int location, int offset)
{
    struct ky_scene_surface *scene_surface = ky_scene_surface_try_from_surface(wlr_surface);
    if (!scene_surface) {
        return;
    }
    scene_surface->has_slide = true;
    scene_surface->slide.location = location;
    scene_surface->slide.offset = offset;
}

bool ky_scene_surface_get_slide(struct wlr_surface *wlr_surface, struct slide *slide)
{
    struct ky_scene_surface *scene_surface = ky_scene_surface_try_from_surface(wlr_surface);
    if (!scene_surface) {
        return false;
    }
    if (scene_surface->has_slide) {
        *slide = scene_surface->slide;
    }

    return scene_surface->has_slide;
}
