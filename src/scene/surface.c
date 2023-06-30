#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_presentation_time.h>

#include "scene/surface.h"

static void handle_scene_buffer_outputs_update(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, outputs_update);

    struct ky_scene_output *primary_output = ky_scene_buffer_get_primary_output(surface->buffer);
    if (primary_output == NULL) {
        return;
    }
    double scale = ky_scene_output_get_output(primary_output)->scale;
    wlr_fractional_scale_v1_notify_scale(surface->surface, scale);
}

static void handle_scene_buffer_output_enter(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, output_enter);
    struct ky_scene_output *output = data;

    wlr_surface_send_enter(surface->surface, ky_scene_output_get_output(output));
}

static void handle_scene_buffer_output_leave(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, output_leave);
    struct ky_scene_output *output = data;

    wlr_surface_send_leave(surface->surface, ky_scene_output_get_output(output));
}

static void handle_scene_buffer_output_present(struct wl_listener *listener, void *data)
{
    struct ky_scene_surface *surface = wl_container_of(listener, surface, output_present);
    struct ky_scene_output *scene_output = data;

    struct ky_scene_output *primary_output = ky_scene_buffer_get_primary_output(surface->buffer);
    if (primary_output == scene_output) {
        struct ky_scene *root = ky_scene_from_node(ky_scene_node_from_buffer(surface->buffer));
        struct wlr_presentation *presentation = ky_scene_get_presentation(root);

        if (presentation) {
            wlr_presentation_surface_sampled_on_output(presentation, surface->surface,
                                                       ky_scene_output_get_output(scene_output));
        }
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

    ky_scene_node_destroy(ky_scene_node_from_buffer(surface->buffer));
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
    struct wlr_buffer *wlr_buffer = ky_scene_buffer_get_buffer(scene_buffer);
    if (!wlr_buffer) {
        return;
    }

    struct wlr_client_buffer *buffer = wlr_client_buffer_get(wlr_buffer);
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
    bool enabled = ky_scene_node_coords(ky_scene_node_from_buffer(scene_buffer), &lx, &ly);

    struct ky_scene_output *primary_output = ky_scene_buffer_get_primary_output(surface->buffer);
    if (!wl_list_empty(&surface->surface->current.frame_callback_list) && primary_output != NULL &&
        enabled) {
        wlr_output_schedule_frame(ky_scene_output_get_output(primary_output));
    }
}

static bool scene_buffer_point_accepts_input(struct ky_scene_buffer *scene_buffer, int sx, int sy)
{
    struct ky_scene_surface *scene_surface = ky_scene_surface_try_from_buffer(scene_buffer);

    return wlr_surface_point_accepts_input(scene_surface->surface, sx, sy);
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
    wl_list_remove(&surface->output_present.link);
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
    struct wlr_addon_set *addons =
        ky_scene_node_get_addon_set(ky_scene_node_from_buffer(scene_buffer));
    struct wlr_addon *addon = wlr_addon_find(addons, scene_buffer, &surface_addon_impl);
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

    surface->buffer = scene_buffer;
    surface->surface = wlr_surface;
    ky_scene_buffer_set_point_accepts_input(scene_buffer, scene_buffer_point_accepts_input);

    surface->outputs_update.notify = handle_scene_buffer_outputs_update;
    ky_scene_buffer_add_outputs_update_listener(scene_buffer, &surface->outputs_update);

    surface->output_enter.notify = handle_scene_buffer_output_enter;
    ky_scene_buffer_add_output_enter_listener(scene_buffer, &surface->output_enter);

    surface->output_leave.notify = handle_scene_buffer_output_leave;
    ky_scene_buffer_add_output_leave_listener(scene_buffer, &surface->output_leave);

    surface->output_present.notify = handle_scene_buffer_output_present;
    ky_scene_buffer_add_output_present_listener(scene_buffer, &surface->output_present);

    surface->frame_done.notify = handle_scene_buffer_frame_done;
    ky_scene_buffer_add_frame_done_listener(scene_buffer, &surface->frame_done);

    surface->surface_destroy.notify = scene_surface_handle_surface_destroy;
    wl_signal_add(&wlr_surface->events.destroy, &surface->surface_destroy);

    surface->surface_commit.notify = handle_scene_surface_surface_commit;
    wl_signal_add(&wlr_surface->events.commit, &surface->surface_commit);

    struct wlr_addon_set *addons =
        ky_scene_node_get_addon_set(ky_scene_node_from_buffer(scene_buffer));
    wlr_addon_init(&surface->addon, addons, scene_buffer, &surface_addon_impl);
    wlr_addon_init(&surface->node_addon, &wlr_surface->addons, wlr_surface,
                   &surface_node_addon_impl);

    set_buffer_with_surface_state(scene_buffer, wlr_surface);

    return surface;
}

struct wlr_surface *wlr_surface_try_from_node(struct ky_scene_node *node)
{
    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);
    struct ky_scene_surface *scene_surface = ky_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }
    return scene_surface->surface;
}

struct ky_scene_buffer *wlr_scene_buffer_try_from_surface(struct wlr_surface *wlr_surface)
{
    struct ky_scene_surface *scene_surface = ky_scene_surface_try_from_surface(wlr_surface);
    if (!scene_surface) {
        return NULL;
    }

    return scene_surface->buffer;
}
