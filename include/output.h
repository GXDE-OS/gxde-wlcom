// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _OUTPUT_H_
#define _OUTPUT_H_

#include <kywc/output.h>

#include "server.h"

struct output_pending_config {
    struct output *output;
    struct kywc_output_state state;
    struct wl_list link;
};

struct output {
    struct kywc_output base;
    struct wlr_output *wlr_output;
    struct ky_scene_output *scene_output;

    struct wl_list link;
    struct output_manager *manager;

    /* effective geometry in layout coord */
    struct kywc_box geometry;
    struct kywc_box usable_area;

    /* software rendering and gamma settings for color-temp and brightness */
    uint32_t color_temp;
    uint32_t brightness;
    bool gamma_changed;

    struct {
        /* emit when output geometry changed */
        struct wl_signal geometry;
        /* emit when output usable area changed */
        struct wl_signal usable_area;
        /* emit when output need update usable area */
        struct wl_signal update_usable_area;
        struct wl_signal update_late_usable_area;
    } events;

    struct wl_listener frame;
    struct wl_listener destroy;

    bool modeset;
};

typedef void (*output_iterator_func_t)(struct kywc_output *output, int index, void *data);

uint32_t output_manager_for_each_output(output_iterator_func_t iterator, bool enabled, void *data);

struct output_manager *output_manager_create(struct server *server);

void output_manager_add_configured_listener(struct wl_listener *listener);

void output_manager_emit_configured(void);

bool output_manager_configure_outputs(void);

void output_manager_power_outputs(bool power);

float output_manager_get_scale(void);

/* update scale to xwayland */
void output_manager_update_scale(float scale);

void output_set_colortemp(struct kywc_output *kywc_output, uint32_t color_temp);

void output_manager_add_output_pending_state(struct output *output,
                                             struct kywc_output_state *state);

struct kywc_output *kywc_output_from_resource(struct wl_resource *resource);

struct output *output_from_kywc_output(struct kywc_output *kywc_output);

struct output *output_from_wlr_output(struct wlr_output *wlr_output);

void output_add_update_usable_area_listener(struct kywc_output *kywc_output,
                                            struct wl_listener *listener, bool late);

void kywc_output_update_usable_area(struct kywc_output *kywc_output);

enum layout_edge {
    LAYOUT_EDGE_TOP,
    LAYOUT_EDGE_BOTTOM,
    LAYOUT_EDGE_LEFT,
    LAYOUT_EDGE_RIGHT,
};

bool output_at_layout_edge(struct output *output, enum layout_edge edge);

struct output *output_adjacent_output(struct output *output, enum layout_edge edge);

struct kywc_output *kywc_output_at_point(double lx, double ly);

struct wlr_output_state;
bool output_state_attempt_gamma(struct output *output, struct wlr_output_state *state);

#endif /* _OUTPUT_H_ */
