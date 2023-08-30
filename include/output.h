// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _OUTPUT_H_
#define _OUTPUT_H_

#include <kywc/output.h>

struct server;

struct output {
    struct kywc_output base;
    struct wlr_output *wlr_output;

    struct wl_list link;
    struct output_manager *manager;

    /* effective geometry in layout coord */
    struct kywc_box geometry;
    struct kywc_box usable_area;

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
    struct wl_listener damage;
    struct wl_listener needs_frame;
    struct wl_listener destroy;
};

struct output_manager *output_manager_create(struct server *server);

struct kywc_output *output_manager_get_primary(void);

void output_manager_add_configured_listener(struct wl_listener *listener);

void output_manager_emit_configured(void);

void output_manager_power_outputs(bool power);

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

#endif /* _OUTPUT_H_ */
