// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_output_layout.h>

#include "kywc/kycom/scene.h"

struct kywc_scene_output_layout {
    struct wlr_output_layout *layout;
    struct kywc_root *scene;

    struct wl_list outputs; // kywc_scene_output_layout_output.link

    struct wl_listener layout_change;
    struct wl_listener layout_destroy;
    struct wl_listener scene_destroy;
};

struct kywc_scene_output_layout_output {
    struct wlr_output_layout_output *layout_output;
    struct kywc_scene_output *scene_output;

    struct wl_list link; // kywc_scene_output_layout.outputs

    struct wl_listener layout_output_destroy;
    struct wl_listener scene_output_destroy;
};

static void scene_output_layout_output_destroy(struct kywc_scene_output_layout_output *solo)
{
    wl_list_remove(&solo->layout_output_destroy.link);
    wl_list_remove(&solo->scene_output_destroy.link);
    wl_list_remove(&solo->link);
    kywc_scene_output_destroy(solo->scene_output);
    free(solo);
}

static void scene_output_layout_output_handle_layout_output_destroy(struct wl_listener *listener,
                                                                    void *data)
{
    struct kywc_scene_output_layout_output *solo =
        wl_container_of(listener, solo, layout_output_destroy);
    scene_output_layout_output_destroy(solo);
}

static void scene_output_layout_output_handle_scene_output_destroy(struct wl_listener *listener,
                                                                   void *data)
{
    struct kywc_scene_output_layout_output *solo =
        wl_container_of(listener, solo, scene_output_destroy);
    solo->scene_output = NULL;
    scene_output_layout_output_destroy(solo);
}

static void scene_output_layout_destroy(struct kywc_scene_output_layout *sol)
{
    struct kywc_scene_output_layout_output *solo, *tmp;
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
    struct kywc_scene_output_layout *sol = wl_container_of(listener, sol, layout_change);

    struct kywc_scene_output_layout_output *solo;
    wl_list_for_each(solo, &sol->outputs, link) {
        kywc_scene_output_set_position(solo->scene_output, solo->layout_output->x,
                                       solo->layout_output->y);
    }
}

void kywc_scene_output_layout_add_output(struct kywc_scene_output_layout *sol,
                                         struct wlr_output_layout_output *loa,
                                         struct kywc_scene_output *so)
{
    struct kywc_scene_output_layout_output *solo = calloc(1, sizeof(*solo));
    if (!solo) {
        return;
    }

    solo->scene_output = so;
    solo->layout_output = loa;

    solo->layout_output_destroy.notify = scene_output_layout_output_handle_layout_output_destroy;
    wl_signal_add(&loa->events.destroy, &solo->layout_output_destroy);

    solo->scene_output_destroy.notify = scene_output_layout_output_handle_scene_output_destroy;
    wl_signal_add(&solo->scene_output->events.destroy, &solo->scene_output_destroy);

    wl_list_insert(&sol->outputs, &solo->link);
    kywc_scene_output_set_position(solo->scene_output, loa->x, loa->y);
}

static void scene_output_layout_handle_layout_destroy(struct wl_listener *listener, void *data)
{
    struct kywc_scene_output_layout *sol = wl_container_of(listener, sol, layout_destroy);
    scene_output_layout_destroy(sol);
}

static void scene_output_layout_handle_scene_destroy(struct wl_listener *listener, void *data)
{
    struct kywc_scene_output_layout *sol = wl_container_of(listener, sol, scene_destroy);
    scene_output_layout_destroy(sol);
}

struct kywc_scene_output_layout *
kywc_scene_attach_output_layout(struct kywc_root *scene, struct wlr_output_layout *output_layout)
{
    struct kywc_scene_output_layout *sol = calloc(1, sizeof(*sol));
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
    wl_signal_add(&scene->group.node.events.destroy, &sol->scene_destroy);

    return sol;
}
