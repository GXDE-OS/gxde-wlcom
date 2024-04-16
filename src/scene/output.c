// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>

#include <wlr/render/swapchain.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/region.h>

#include "output.h"
#include "scene_p.h"

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
    wlr_output_schedule_frame(scene_output->output);

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
        wlr_output_schedule_frame(scene_output->output);
    }

    pixman_region32_fini(&damage);
}

static void scene_output_handle_needs_frame(struct wl_listener *listener, void *data)
{
    struct ky_scene_output *scene_output =
        wl_container_of(listener, scene_output, output_needs_frame);
    wlr_output_schedule_frame(scene_output->output);
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

    scene_output->index = prev_output_index + 1;
    assert(scene_output->index < 64);
    wl_list_insert(prev_output_link, &scene_output->link);

    wl_signal_init(&scene_output->events.frame);
    wl_signal_init(&scene_output->events.destroy);

    scene_output->output_commit.notify = scene_output_handle_commit;
    wl_signal_add(&output->events.commit, &scene_output->output_commit);

    scene_output->output_damage.notify = scene_output_handle_damage;
    wl_signal_add(&output->events.damage, &scene_output->output_damage);

    scene_output->output_needs_frame.notify = scene_output_handle_needs_frame;
    wl_signal_add(&output->events.needs_frame, &scene_output->output_needs_frame);

    scene_output_update_geometry(scene_output, false);

    wl_signal_emit_mutable(&scene->events.new_output, scene_output);

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

static void scene_node_send_frame_done(struct ky_scene_node *node,
                                       struct ky_scene_output *scene_output, struct timespec *now)
{
    if (!node->enabled) {
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
    struct wlr_output *output = scene_output->output;
    if (!wlr_output_configure_primary_swapchain(output, state, &output->swapchain)) {
        return false;
    }

    int buffer_age;
    struct wlr_buffer *buffer = wlr_swapchain_acquire(output->swapchain, &buffer_age);
    if (buffer == NULL) {
        return false;
    }

    struct wlr_render_pass *render_pass =
        wlr_renderer_begin_buffer_pass(output->renderer, buffer, NULL);
    if (render_pass == NULL) {
        wlr_buffer_unlock(buffer);
        return false;
    }

    target->buffer = buffer;
    target->render_pass = render_pass;
    pixman_region32_clear(&target->damage);
    wlr_damage_ring_get_buffer_damage(&scene_output->damage_ring, buffer_age, &target->damage);

    ky_scene_render_damage_in_target(scene_output->scene, target);

    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);
    pixman_region32_copy(&frame_damage, &scene_output->damage_ring.current);
    ky_scene_render_region(&frame_damage, target);
    wlr_output_state_set_damage(state, &frame_damage);
    pixman_region32_fini(&frame_damage);

    if (!wlr_render_pass_submit(render_pass)) {
        wlr_buffer_unlock(buffer);
        return false;
    }

    wlr_damage_ring_rotate(&scene_output->damage_ring);
    wlr_output_state_set_buffer(state, buffer);
    wlr_buffer_unlock(buffer);

    return true;
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
    };
    wlr_output_transformed_resolution(output, &target.trans_width, &target.trans_height);
    target.logical.width = target.trans_width / output->scale;
    target.logical.height = target.trans_height / output->scale;

    // current scene damage in the output box
    pixman_region32_init_rect(&target.damage, target.logical.x, target.logical.y,
                              target.logical.width, target.logical.height);
    ky_scene_collect_damage(scene_output->scene);
    pixman_region32_intersect(&target.damage, &target.damage, &scene_output->collected_damage);
    pixman_region32_clear(&scene_output->collected_damage);

    // union all damage in the output layout box
    pixman_region32_translate(&target.damage, -target.logical.x, -target.logical.y);
    if (floor(target.scale) != target.scale) {
        wlr_region_expand(&target.damage, &target.damage, 1);
    }
    wlr_damage_ring_add(&scene_output->damage_ring, &target.damage);

    if (!scene_output->output->needs_frame &&
        !pixman_region32_not_empty(&scene_output->damage_ring.current)) {
        pixman_region32_fini(&target.damage);
        return true;
    }

    // ky_scene_log_region(KYWC_ERROR, "frame damage", &scene_output->damage_ring.current);

    bool ok = false;
    struct wlr_output_state state;
    struct output *_output = output_from_wlr_output(output);
    wlr_output_state_init(&state);
    output_state_attempt_gamma(_output, &state);

    if (!scene_output_render(scene_output, &state, &target)) {
        goto out;
    }

    ok = wlr_output_commit_state(scene_output->output, &state);
    if (!ok) {
        goto out;
    }

out:
    wlr_output_state_finish(&state);
    pixman_region32_fini(&target.damage);
    return ok;
}

static int scale_length(int length, int offset, float scale)
{
    return round((offset + length) * scale) - round(offset * scale);
}

void ky_scene_render_box(struct wlr_box *box, struct ky_scene_render_target *target)
{
    box->width = scale_length(box->width, box->x, target->scale);
    box->height = scale_length(box->height, box->y, target->scale);
    box->x = round(box->x * target->scale);
    box->y = round(box->y * target->scale);

    enum wl_output_transform transform = wlr_output_transform_invert(target->transform);
    wlr_box_transform(box, box, transform, target->trans_width, target->trans_height);
}

void ky_scene_render_region(pixman_region32_t *region, struct ky_scene_render_target *target)
{
    wlr_region_scale(region, region, target->scale);

    enum wl_output_transform transform = wlr_output_transform_invert(target->transform);
    wlr_region_transform(region, region, transform, target->trans_width, target->trans_height);
}
