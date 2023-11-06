// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYWC_OUTPUT_H_
#define _KYWC_OUTPUT_H_

#include <stdbool.h>
#include <stdint.h>

#include <wayland-server-protocol.h>

#include "boxes.h"

enum kywc_output_vrr_policy {
    KYWC_OUTPUT_VRR_DISABLED,
    KYWC_OUTPUT_VRR_ENABLED,
    KYWC_OUTPUT_VRR_AUTO,
};

enum kywc_output_capability {
    KYWC_OUTPUT_CAPABILITY_OVERSCAN = 1 << 0,
    KYWC_OUTPUT_CAPABILITY_VRR = 1 << 1,
    KYWC_OUTPUT_CAPABILITY_RGB_RANGE = 1 << 2
};

struct kywc_output_state {
    bool enabled, power;
    int32_t width, height, refresh; // refresh in mHz
    enum wl_output_transform transform;
    enum kywc_output_vrr_policy vrr_policy;
    float scale;

    /* layout coord */
    int32_t lx, ly;
    int32_t brightness;
    int32_t color_temp;
};

struct kywc_output_mode {
    int32_t width, height;
    int32_t refresh; // mHz
    bool preferred;
    struct wl_list link;
};

struct kywc_output_prop {
    bool is_virtual;
    bool brightness_support;
    size_t gamma_size;
    uint32_t capability;
    int32_t phys_width, phys_height;          // mm
    const char *make, *model, *serial, *desc; // may be NULL
    struct wl_list modes;
};

/* connector with a monitor */
struct kywc_output {
    const char *name;
    char uuid[16];
    bool destroying;

    struct kywc_output_prop prop;
    struct kywc_output_state state;

    struct {
        struct wl_signal on;
        struct wl_signal off;
        struct wl_signal scale;
        struct wl_signal transform;
        struct wl_signal mode;
        struct wl_signal position;
        struct wl_signal power;

        struct wl_signal frame;
        struct wl_signal destroy;
    } events;
};

void kywc_output_add_new_listener(struct wl_listener *listener);

void kywc_output_add_primary_listener(struct wl_listener *listener);

bool kywc_output_set_state(struct kywc_output *kywc_output, struct kywc_output_state *state);

void kywc_output_set_primary(struct kywc_output *kywc_output);

struct kywc_output *kywc_output_get_primary(void);

struct kywc_output_mode *kywc_output_preferred_mode(struct kywc_output *kywc_output);

float kywc_output_preferred_scale(struct kywc_output *kywc_output, int width, int height);

void kywc_output_effective_geometry(struct kywc_output *kywc_output, struct kywc_box *box);

bool kywc_output_contains_point(struct kywc_output *kywc_output, int x, int y);

struct kywc_output *kywc_output_by_name(const char *name);

#endif /* _KYWC_OUTPUT_H_ */
