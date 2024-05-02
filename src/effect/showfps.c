// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <wlr/types/wlr_output.h>

#include "effect_p.h"
#include "output.h"
#include "painter.h"
#include "scene/scene.h"
#include "view/view.h"
#include "widget/widget.h"

#define MAX_FRAMES (200)

struct frame_output {
    struct wl_list link;
    struct showfps_effect *effect;

    struct widget *widget;
    struct ky_scene_node *node;

    int frames[MAX_FRAMES];
    int frames_pos;
    int fps;

    struct ky_scene_output *output;
    struct wl_listener present; // wlr_output
    struct wl_listener frame;
    struct wl_listener destroy;
};

struct showfps_effect {
    struct wl_list outputs;
    struct wl_listener new_enabled_output;

    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    struct effect_manager *manager;
};

static void frame_output_destroy(struct frame_output *output)
{
    wl_list_remove(&output->link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->present.link);

    widget_destroy(output->widget);
    free(output);
}

static void output_handle_destroy(struct wl_listener *listener, void *data)
{
    struct frame_output *output = wl_container_of(listener, output, destroy);
    frame_output_destroy(output);
}

static void output_handle_frame(struct wl_listener *listener, void *data)
{
    struct frame_output *output = wl_container_of(listener, output, frame);

    char text[4];
    snprintf(text, 4, "%3d", output->fps);
    widget_set_text(output->widget, text, TEXT_ALIGN_LEFT, false, false, false);
    widget_update(output->widget, false);

    ky_scene_node_set_position(output->node, output->output->x + 5, output->output->y + 5);
}

static void output_handle_present(struct wl_listener *listener, void *data)
{
    struct frame_output *output = wl_container_of(listener, output, present);
    struct wlr_output_event_present *event = data;
    int time = event->when->tv_sec * 1000 + event->when->tv_nsec / 1000000;

    output->frames[output->frames_pos] = time;
    if (++output->frames_pos == MAX_FRAMES) {
        output->frames_pos = 0;
    }

    output->fps = 0;
    for (int i = 0; i < MAX_FRAMES; ++i) {
        if (abs(time - output->frames[i]) < 1000) {
            ++output->fps;
        }
    }
}

static void frame_output_create(struct showfps_effect *effect, struct ky_scene_output *scene_output)
{
    struct frame_output *output = calloc(1, sizeof(*output));
    if (!output) {
        return;
    }

    output->effect = effect;
    wl_list_insert(&effect->outputs, &output->link);

    output->output = scene_output;
    output->present.notify = output_handle_present;
    wl_signal_add(&scene_output->output->events.present, &output->present);
    output->frame.notify = output_handle_frame;
    wl_signal_add(&scene_output->events.frame, &output->frame);
    output->destroy.notify = output_handle_destroy;
    wl_signal_add(&scene_output->events.destroy, &output->destroy);

    struct view_layer *layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
    output->widget = widget_create(layer->tree);
    widget_set_font(output->widget, NULL, 20);
    widget_set_max_size(output->widget, 500, 200);
    widget_set_auto_resize(output->widget, AUTO_RESIZE_ONLY);
    widget_set_front_color(output->widget, (float[4]){ 1, 0, 0, 1 });
    widget_set_enabled(output->widget, true);

    output->node = ky_scene_node_from_widget(output->widget);
    ky_scene_node_set_input_bypassed(output->node, true);

    wlr_output_schedule_frame(output->output->output);
}

static void handle_new_enabled_output(struct wl_listener *listener, void *data)
{
    struct showfps_effect *effect = wl_container_of(listener, effect, new_enabled_output);
    struct kywc_output *output = data;
    frame_output_create(effect, output_from_kywc_output(output)->scene_output);
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct showfps_effect *effect = wl_container_of(listener, effect, enable);
    assert(wl_list_empty(&effect->outputs));

    struct ky_scene *scene = effect->manager->server->scene;
    struct ky_scene_output *output;
    wl_list_for_each(output, &scene->outputs, link) {
        frame_output_create(effect, output);
    }

    effect->new_enabled_output.notify = handle_new_enabled_output;
    kywc_output_add_new_enabled_listener(&effect->new_enabled_output);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct showfps_effect *effect = wl_container_of(listener, effect, disable);
    wl_list_remove(&effect->new_enabled_output.link);

    struct frame_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &effect->outputs, link) {
        frame_output_destroy(output);
    }
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct showfps_effect *effect = wl_container_of(listener, effect, destroy);
    assert(wl_list_empty(&effect->outputs));
    free(effect);
}

bool showfps_effect_create(struct effect_manager *manager)
{
    struct showfps_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect = effect_create("showfps", 0, false, NULL);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    effect->manager = manager;
    wl_list_init(&effect->outputs);
    wl_list_init(&effect->new_enabled_output.link);

    effect->enable.notify = handle_effect_enable;
    wl_signal_add(&effect->effect->events.enable, &effect->enable);
    effect->disable.notify = handle_effect_disable;
    wl_signal_add(&effect->effect->events.disable, &effect->disable);
    effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&effect->effect->events.destroy, &effect->destroy);

    return true;
}
