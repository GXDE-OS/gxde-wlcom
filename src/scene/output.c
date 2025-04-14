// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <stdlib.h>

#include <wlr/render/swapchain.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/region.h>

#include "effect/effect.h"
#include "output.h"
#include "render/pass.h"
#include "scene/scene.h"
#include "scene/surface.h"
#include "scene_p.h"
#include "util/debug.h"

static void scene_output_set_position(struct ky_scene_output *scene_output, int lx, int ly);

struct ky_scene_output_layout {
    struct wlr_output_layout *layout;
    struct ky_scene *scene;

    struct wl_list outputs; // ky_scene_output_layout_output.link

    struct wl_listener layout_change;
    struct wl_listener layout_destroy;
    struct wl_listener scene_destroy;
};

struct ky_scene_output_layout_output {
    struct wlr_output_layout_output *layout_output;
    struct ky_scene_output *scene_output;

    struct wl_list link; // ky_scene_output_layout.outputs

    struct wl_listener layout_output_destroy;
    struct wl_listener scene_output_destroy;
};

static void scene_output_layout_output_destroy(struct ky_scene_output_layout_output *solo)
{
    wl_list_remove(&solo->layout_output_destroy.link);
    wl_list_remove(&solo->scene_output_destroy.link);
    wl_list_remove(&solo->link);
    free(solo);
}

static void scene_output_layout_output_handle_layout_output_destroy(struct wl_listener *listener,
                                                                    void *data)
{
    struct ky_scene_output_layout_output *solo =
        wl_container_of(listener, solo, layout_output_destroy);
    scene_output_layout_output_destroy(solo);
}

static void scene_output_layout_output_handle_scene_output_destroy(struct wl_listener *listener,
                                                                   void *data)
{
    struct ky_scene_output_layout_output *solo =
        wl_container_of(listener, solo, scene_output_destroy);
    scene_output_layout_output_destroy(solo);
}

static void scene_output_layout_destroy(struct ky_scene_output_layout *sol)
{
    struct ky_scene_output_layout_output *solo, *tmp;
    wl_list_for_each_safe(solo, tmp, &sol->outputs, link) {
        scene_output_layout_output_destroy(solo);
    }
    wl_list_remove(&sol->layout_change.link);
    wl_list_remove(&sol->layout_destroy.link);
    wl_list_remove(&sol->scene_destroy.link);
    free(sol);
}

static void scene_output_layout_handle_layout_change(struct wl_listener *listener, void *data)
{
    struct ky_scene_output_layout *sol = wl_container_of(listener, sol, layout_change);

    struct ky_scene_output_layout_output *solo;
    wl_list_for_each(solo, &sol->outputs, link) {
        scene_output_set_position(solo->scene_output, solo->layout_output->x,
                                  solo->layout_output->y);
    }
}

void ky_scene_output_layout_add_output(struct ky_scene_output_layout *sol,
                                       struct wlr_output_layout_output *lo,
                                       struct ky_scene_output *so)
{
    assert(lo->output == so->output);

    struct ky_scene_output_layout_output *solo;
    wl_list_for_each(solo, &sol->outputs, link) {
        assert(solo->scene_output != so);
    }

    solo = calloc(1, sizeof(*solo));
    if (!solo) {
        return;
    }

    solo->scene_output = so;
    solo->layout_output = lo;

    solo->layout_output_destroy.notify = scene_output_layout_output_handle_layout_output_destroy;
    wl_signal_add(&lo->events.destroy, &solo->layout_output_destroy);

    solo->scene_output_destroy.notify = scene_output_layout_output_handle_scene_output_destroy;
    wl_signal_add(&solo->scene_output->events.destroy, &solo->scene_output_destroy);

    wl_list_insert(&sol->outputs, &solo->link);

    scene_output_set_position(solo->scene_output, lo->x, lo->y);
}

void ky_scene_output_damage_whole(struct ky_scene_output *scene_output)
{
    wlr_damage_ring_add_whole(&scene_output->damage_ring);
    output_schedule_frame(scene_output->output);
}

static void scene_output_layout_handle_layout_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_output_layout *sol = wl_container_of(listener, sol, layout_destroy);
    scene_output_layout_destroy(sol);
}

static void scene_output_layout_handle_scene_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_output_layout *sol = wl_container_of(listener, sol, scene_destroy);
    scene_output_layout_destroy(sol);
}

struct ky_scene_output_layout *
ky_scene_attach_output_layout(struct ky_scene *scene, struct wlr_output_layout *output_layout)
{
    struct ky_scene_output_layout *sol = calloc(1, sizeof(*sol));
    if (!sol) {
        return false;
    }

    sol->scene = scene;
    sol->layout = output_layout;

    wl_list_init(&sol->outputs);

    sol->layout_destroy.notify = scene_output_layout_handle_layout_destroy;
    wl_signal_add(&output_layout->events.destroy, &sol->layout_destroy);

    sol->layout_change.notify = scene_output_layout_handle_layout_change;
    wl_signal_add(&output_layout->events.change, &sol->layout_change);

    sol->scene_destroy.notify = scene_output_layout_handle_scene_destroy;
    wl_signal_add(&scene->tree.node.events.destroy, &sol->scene_destroy);

    return sol;
}

/**
 * scene output
 */

static void scene_output_update_geometry(struct ky_scene_output *scene_output, bool force_update)
{
    int width, height;
    wlr_output_effective_resolution(scene_output->output, &width, &height);
    wlr_damage_ring_set_bounds(&scene_output->damage_ring, width, height);

    wlr_damage_ring_add_whole(&scene_output->damage_ring);
    output_schedule_frame(scene_output->output);

    ky_scene_node_update_outputs(&scene_output->scene->tree.node, &scene_output->scene->outputs,
                                 NULL, force_update ? scene_output : NULL);
}

static void scene_output_set_position(struct ky_scene_output *scene_output, int lx, int ly)
{
    if (scene_output->x == lx && scene_output->y == ly) {
        return;
    }

    scene_output->x = lx;
    scene_output->y = ly;

    scene_output_update_geometry(scene_output, false);
}

static void scene_output_handle_destroy(struct wlr_addon *addon)
{
    struct ky_scene_output *scene_output = wl_container_of(addon, scene_output, addon);
    ky_scene_output_destroy(scene_output);
}

static const struct wlr_addon_interface output_addon_impl = {
    .name = "ky_scene_output",
    .destroy = scene_output_handle_destroy,
};

static void scene_output_handle_commit(struct wl_listener *listener, void *data)
{
    struct ky_scene_output *scene_output = wl_container_of(listener, scene_output, output_commit);
    struct wlr_output_event_commit *event = data;
    const struct wlr_output_state *state = event->state;

    bool force_update = state->committed & (WLR_OUTPUT_STATE_TRANSFORM | WLR_OUTPUT_STATE_SCALE |
                                            WLR_OUTPUT_STATE_SUBPIXEL);

    if (force_update || state->committed & (WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_ENABLED)) {
        scene_output_update_geometry(scene_output, force_update);
    }
}

static void scene_output_handle_damage(struct wl_listener *listener, void *data)
{
    struct ky_scene_output *scene_output = wl_container_of(listener, scene_output, output_damage);
    struct wlr_output_event_damage *event = data;

    pixman_region32_t damage;
    pixman_region32_init(&damage);
    wlr_region_scale(&damage, event->damage, 1 / event->output->scale);

    if (wlr_damage_ring_add(&scene_output->damage_ring, &damage)) {
        output_schedule_frame(scene_output->output);
    }

    pixman_region32_fini(&damage);
}

static void scene_output_handle_needs_frame(struct wl_listener *listener, void *data)
{
    struct ky_scene_output *scene_output =
        wl_container_of(listener, scene_output, output_needs_frame);
    output_schedule_frame(scene_output->output);
}

struct ky_scene_output *ky_scene_output_create(struct ky_scene *scene, struct wlr_output *output)
{
    struct ky_scene_output *scene_output = calloc(1, sizeof(*scene_output));
    if (!scene_output) {
        return NULL;
    }

    scene_output->output = output;
    scene_output->scene = scene;
    wlr_addon_init(&scene_output->addon, &output->addons, scene, &output_addon_impl);

    wlr_damage_ring_init(&scene_output->damage_ring);
    pixman_region32_init(&scene_output->collected_damage);

    int prev_output_index = -1;
    struct wl_list *prev_output_link = &scene->outputs;

    struct ky_scene_output *current_output;
    wl_list_for_each(current_output, &scene->outputs, link) {
        if (prev_output_index + 1 != current_output->index) {
            break;
        }

        prev_output_index = current_output->index;
        prev_output_link = &current_output->link;
    }

    char *env = getenv("KYWC_SCENE_OUTPUT_DISABLE_SCANOUT");
    scene_output->direct_scanout = !(env && strcmp(env, "true") == 0);
    scene_output->index = prev_output_index + 1;
    assert(scene_output->index < 64);
    wl_list_insert(prev_output_link, &scene_output->link);

    wl_signal_init(&scene_output->events.viewport);
    wl_signal_init(&scene_output->events.frame);
    wl_signal_init(&scene_output->events.destroy);

    scene_output->output_commit.notify = scene_output_handle_commit;
    wl_signal_add(&output->events.commit, &scene_output->output_commit);

    scene_output->output_damage.notify = scene_output_handle_damage;
    wl_signal_add(&output->events.damage, &scene_output->output_damage);

    scene_output->output_needs_frame.notify = scene_output_handle_needs_frame;
    wl_signal_add(&output->events.needs_frame, &scene_output->output_needs_frame);

    scene_output_update_geometry(scene_output, false);

    return scene_output;
}

void ky_scene_output_destroy(struct ky_scene_output *scene_output)
{
    if (!scene_output) {
        return;
    }

    wl_signal_emit_mutable(&scene_output->events.destroy, NULL);

    ky_scene_node_update_outputs(&scene_output->scene->tree.node, &scene_output->scene->outputs,
                                 scene_output, NULL);

    wlr_addon_finish(&scene_output->addon);
    wlr_damage_ring_finish(&scene_output->damage_ring);
    pixman_region32_fini(&scene_output->collected_damage);
    wlr_buffer_unlock(scene_output->buffer);

    wl_list_remove(&scene_output->link);
    wl_list_remove(&scene_output->output_commit.link);
    wl_list_remove(&scene_output->output_damage.link);
    wl_list_remove(&scene_output->output_needs_frame.link);

    free(scene_output);
}

struct ky_scene_output *ky_scene_get_scene_output(struct ky_scene *scene, struct wlr_output *output)
{
    struct wlr_addon *addon = wlr_addon_find(&output->addons, scene, &output_addon_impl);
    if (!addon) {
        return NULL;
    }
    struct ky_scene_output *scene_output = wl_container_of(addon, scene_output, addon);
    return scene_output;
}

void ky_scene_output_set_viewport_source_box(struct ky_scene_output *scene_output,
                                             const struct wlr_box *src_box)
{
    if (wlr_box_equal(&scene_output->viewport.src, src_box) || src_box == NULL) {
        return;
    }

    scene_output->viewport.has_src = true;
    scene_output->viewport.src = *src_box;

    wl_signal_emit_mutable(&scene_output->events.viewport, NULL);

    ky_scene_output_damage_whole(scene_output);
}

static void scene_node_send_frame_done(struct ky_scene_node *node,
                                       struct ky_scene_output *scene_output, struct timespec *now)
{
    if (!node->enabled && !node->force_damage_event) {
        return;
    }

    if (node->type == KY_SCENE_NODE_BUFFER) {
        struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);
        if (scene_buffer->primary_output == scene_output) {
            wl_signal_emit_mutable(&scene_buffer->events.frame_done, now);
        }
    } else if (node->type == KY_SCENE_NODE_TREE) {
        struct ky_scene_tree *scene_tree = ky_scene_tree_from_node(node);
        struct ky_scene_node *child;
        wl_list_for_each(child, &scene_tree->children, link) {
            if (child->type != KY_SCENE_NODE_RECT) {
                scene_node_send_frame_done(child, scene_output, now);
            }
        }
    }
}

void ky_scene_output_send_frame_done(struct ky_scene_output *scene_output, struct timespec *now)
{
    scene_node_send_frame_done(&scene_output->scene->tree.node, scene_output, now);
}

static bool scene_output_render(struct ky_scene_output *scene_output,
                                struct wlr_output_state *state,
                                struct ky_scene_render_target *target)
{
    KY_PROFILE_ZONE(zone, __func__);

    struct wlr_output *output = scene_output->output;
    if (!wlr_output_configure_primary_swapchain(output, state, &output->swapchain)) {
        KY_PROFILE_ZONE_END(zone);
        return false;
    }

    int buffer_age;
    struct wlr_buffer *buffer = wlr_swapchain_acquire(output->swapchain, &buffer_age);
    if (buffer == NULL) {
        KY_PROFILE_ZONE_END(zone);
        return false;
    }

    struct wlr_render_pass *render_pass =
        wlr_renderer_begin_buffer_pass(output->renderer, buffer, NULL);
    if (render_pass == NULL) {
        wlr_buffer_unlock(buffer);

        KY_PROFILE_ZONE_END(zone);
        return false;
    }

    target->buffer = buffer;
    target->render_pass = render_pass;

    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);

    // target damage is accumulated damage in the output, but in layout coord
    wlr_damage_ring_get_buffer_damage(&scene_output->damage_ring, buffer_age, &target->damage);

    if (pixman_region32_not_empty(&target->damage)) {
        // translate to scene layout coord
        pixman_region32_translate(&target->damage, target->logical.x, target->logical.y);
        pixman_region32_copy(&frame_damage, &target->damage);

        // render all nodes and effects
        ky_scene_render_damage_in_target(scene_output->scene, target);

        // update target excluded_buffer_damage from excluded_damage
        pixman_region32_copy(&target->excluded_buffer_damage, &target->excluded_damage);
        pixman_region32_translate(&target->excluded_buffer_damage, -target->logical.x,
                                  -target->logical.y);
        ky_scene_render_region(&target->excluded_buffer_damage, target);

        // trying to render software cursors
        ky_scene_render_target_add_software_cursors(target);

        // damage extended in render, translate to output coord
        pixman_region32_subtract(&frame_damage, &target->damage, &frame_damage);
        // subtract the excluded damage
        pixman_region32_subtract(&frame_damage, &frame_damage, &target->excluded_damage);
        pixman_region32_translate(&frame_damage, -target->logical.x, -target->logical.y);

        wlr_damage_ring_add(&scene_output->damage_ring, &frame_damage);
        pixman_region32_copy(&frame_damage, &scene_output->damage_ring.current);
    }

    wlr_damage_ring_rotate(&scene_output->damage_ring);

    ky_scene_output_render_post(target);

    if (!ky_render_pass_submit(render_pass, output_from_wlr_output(output)->quirks)) {
        wlr_buffer_unlock(buffer);
        pixman_region32_fini(&frame_damage);
        wlr_damage_ring_add_whole(&scene_output->damage_ring);

        KY_PROFILE_ZONE_END(zone);
        return false;
    }

    if (pixman_region32_not_empty(&frame_damage)) {
        ky_scene_render_region(&frame_damage, target);
    }
    /* set damage even if frame damage is empty */
    wlr_output_state_set_damage(state, &frame_damage);
    wlr_output_state_set_buffer(state, buffer);

    wlr_buffer_unlock(buffer);
    pixman_region32_fini(&frame_damage);

    KY_PROFILE_ZONE_END(zone);
    return true;
}

static bool ky_scene_get_fullscreen_buffer(struct ky_scene_node *node, int lx, int ly,
                                           const struct wlr_box *box,
                                           struct ky_scene_buffer **buffer)
{
    if (!ky_scene_node_is_visible(node)) {
        return false;
    }

    if (node->type == KY_SCENE_NODE_TREE) {
        struct ky_scene_tree *tree = ky_scene_tree_from_node(node);
        struct ky_scene_node *child;
        wl_list_for_each_reverse(child, &tree->children, link) {
            if (ky_scene_get_fullscreen_buffer(child, lx + child->x, ly + child->y, box, buffer)) {
                return true;
            }
        }
        return false;
    }

    struct wlr_box bound = { 0 };
    node->impl.get_bounding_box(node, &bound);
    bound.x += lx, bound.y += ly;

    struct wlr_box dest;
    if (!wlr_box_intersection(&dest, &bound, box)) {
        return false;
    }

    if (node->type == KY_SCENE_NODE_RECT) {
        return true;
    }

    if (dest.width != box->width || dest.height != box->height) {
        return true;
    }

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_from_node(node);
    if (scene_buffer->opacity != 1 || !pixman_region32_not_empty(&scene_buffer->opaque_region)) {
        return true;
    }

    if (pixman_region32_contains_rectangle(&scene_buffer->opaque_region,
                                           &(pixman_box32_t){ 0, 0, box->width, box->height }) ==
        PIXMAN_REGION_IN) {
        *buffer = scene_buffer;
    }

    return true;
}

static struct ky_scene_buffer *
scene_output_get_fullscreen_buffer(struct ky_scene_output *scene_output)
{
    struct ky_scene_node *root = &scene_output->scene->tree.node;
    int lx = root->x, ly = root->y;

    int width, height;
    wlr_output_effective_resolution(scene_output->output, &width, &height);
    struct wlr_box box = { scene_output->x, scene_output->y, width, height };

    struct ky_scene_buffer *buffer = NULL;
    ky_scene_get_fullscreen_buffer(root, lx, ly, &box, &buffer);
    return buffer;
}

static bool ky_scene_try_direct_scanout(struct ky_scene_output *scene_output,
                                        struct wlr_output_state *state,
                                        struct ky_scene_buffer *scene_buffer)
{
    if (!scene_output->direct_scanout) {
        return false;
    }

    struct wlr_output *output = scene_output->output;
    if (!wlr_output_is_direct_scanout_allowed(output)) {
        return false;
    }

    if (!output_use_hardware_gamma(output_from_wlr_output(output))) {
        return false;
    }

    if (!scene_buffer) {
        return false;
    }

    if (scene_buffer->transform != output->transform) {
        return false;
    }

    struct wlr_fbox default_box = { 0 };
    if (scene_buffer->transform & WL_OUTPUT_TRANSFORM_90) {
        default_box.width = scene_buffer->buffer->height;
        default_box.height = scene_buffer->buffer->width;
    } else {
        default_box.width = scene_buffer->buffer->width;
        default_box.height = scene_buffer->buffer->height;
    }

    if (!wlr_fbox_empty(&scene_buffer->src_box) &&
        !wlr_fbox_equal(&scene_buffer->src_box, &default_box)) {
        return false;
    }

    if (scene_buffer->buffer->width != output->width ||
        scene_buffer->buffer->height != output->height) {
        kywc_log(KYWC_DEBUG, "scene buffer size mismatch");
        return false;
    }

    if (scene_buffer->primary_output == scene_output) {
        struct wlr_linux_dmabuf_feedback_v1_init_options options = {
            .main_renderer = scene_output->output->renderer,
            .scanout_primary_output = scene_output->output,
        };

        ky_scene_buffer_send_dmabuf_feedback(scene_output->scene, scene_buffer, &options);
        scene_buffer->node.sent_dmabuf_feedback = true;
    }

    struct wlr_output_state pending;
    wlr_output_state_init(&pending);
    if (!wlr_output_state_copy(&pending, state)) {
        return false;
    }

    struct wlr_buffer *wlr_buffer = scene_buffer->buffer;
    struct wlr_client_buffer *client_buffer = wlr_client_buffer_get(wlr_buffer);
    if (client_buffer != NULL && client_buffer->source != NULL &&
        client_buffer->source->n_locks > 0) {
        wlr_buffer = client_buffer->source;
    }
    wlr_output_state_set_buffer(&pending, wlr_buffer);

    if (!wlr_output_test_state(scene_output->output, &pending)) {
        wlr_output_state_finish(&pending);
        return false;
    }

    wlr_output_state_copy(state, &pending);
    wlr_output_state_finish(&pending);

    struct ky_scene_output_sample_event sample_event = {
        .output = scene_output,
        .direct_scanout = true,
    };
    wl_signal_emit_mutable(&scene_buffer->events.output_sample, &sample_event);

    return true;
}

static bool is_tearing_allowed(struct ky_scene *scene, struct ky_scene_buffer *buffer)
{
    if (!buffer) {
        return false;
    }

    struct wlr_surface *surface = wlr_surface_try_from_node(&buffer->node);
    if (!surface) {
        return false;
    }

    return ky_scene_surface_is_tearing_allowed(scene, surface);
}

bool ky_scene_output_commit(struct ky_scene_output *scene_output,
                            const struct ky_scene_output_state_options *options)
{
    /* make sure something is done before commit */
    wl_signal_emit_mutable(&scene_output->events.frame, NULL);

    struct wlr_output *output = scene_output->output;
    struct ky_scene_render_target target = {
        .transform = output->transform,
        .scale = output->scale,
        .logical = { .x = scene_output->x, .y = scene_output->y },
        .output = scene_output,
        .options = KY_SCENE_RENDER_ENABLE_PRESENTATION,
    };
    wlr_output_transformed_resolution(output, &target.trans_width, &target.trans_height);
    target.logical.width = target.trans_width / output->scale;
    target.logical.height = target.trans_height / output->scale;

    ky_scene_output_render_pre(&target);

    pixman_region32_t damage;
    // current scene damage in the output box
    pixman_region32_init_rect(&damage, target.logical.x, target.logical.y, target.logical.width,
                              target.logical.height);
    ky_scene_collect_damage(scene_output->scene);
    pixman_region32_intersect(&damage, &damage, &scene_output->collected_damage);
    pixman_region32_clear(&scene_output->collected_damage);

    // union all damage in the output layout box
    pixman_region32_translate(&damage, -target.logical.x, -target.logical.y);
    if (floor(target.scale) != target.scale) {
        wlr_region_expand(&damage, &damage, 1);
    }
    /* union damage from output damage event */
    wlr_damage_ring_add(&scene_output->damage_ring, &damage);
    pixman_region32_fini(&damage);

    if (!scene_output->output->needs_frame &&
        !pixman_region32_not_empty(&scene_output->damage_ring.current)) {
        return false;
    }

    struct ky_scene_buffer *fullscreen = scene_output_get_fullscreen_buffer(scene_output);
    bool is_tearing = is_tearing_allowed(scene_output->scene, fullscreen);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    output_state_attempt_gamma(output_from_wlr_output(output), &state);
    output_state_attempt_vrr(output_from_wlr_output(output), &state, fullscreen != NULL);

    bool scanout = ky_scene_try_direct_scanout(scene_output, &state, fullscreen);
    if (scene_output->prev_scanout != scanout) {
        scene_output->prev_scanout = scanout;
        kywc_log(KYWC_INFO, "%s: Direct scan-out %s", scene_output->output->name,
                 scanout ? "enabled" : "disabled");
        if (!scanout) {
            // When exiting direct scan-out, damage everything
            wlr_damage_ring_add_whole(&scene_output->damage_ring);
        }
    }
    if (scanout) {
        wlr_output_state_set_damage(&state, &scene_output->damage_ring.current);
        output_state_attempt_tearing(output_from_wlr_output(output), &state, is_tearing);
        wlr_damage_ring_rotate(&scene_output->damage_ring);
        bool ok = wlr_output_commit_state(scene_output->output, &state);
        wlr_output_state_finish(&state);
        return ok;
    }

    pixman_region32_init(&target.damage);
    pixman_region32_init(&target.excluded_damage);
    pixman_region32_init(&target.excluded_buffer_damage);
    // ky_scene_log_region(KYWC_ERROR, "frame damage", &scene_output->damage_ring.current);

    bool ok = false;
    if (scene_output_render(scene_output, &state, &target)) {
        output_state_attempt_tearing(output_from_wlr_output(output), &state, is_tearing);
        ok = wlr_output_commit_state(scene_output->output, &state);
        if (ok) {
            wlr_buffer_unlock(scene_output->buffer);
            scene_output->buffer = wlr_buffer_lock(target.buffer);
        } else if (!scene_output->commit_failed) {
            /* damage whole if output commit failed */
            ky_scene_output_damage_whole(scene_output);
        }
        scene_output->commit_failed = !ok;
    }

    wlr_output_state_finish(&state);
    pixman_region32_fini(&target.damage);
    pixman_region32_fini(&target.excluded_damage);
    pixman_region32_fini(&target.excluded_buffer_damage);

    return ok;
}
