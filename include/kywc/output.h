// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _KYWC_OUTPUT_H_
#define _KYWC_OUTPUT_H_

#include <stdbool.h>
#include <stdint.h>

#include <wayland-server-protocol.h>

#include "boxes.h"

enum kywc_output_vrr_policy {
    KYWC_OUTPUT_VRR_POLICY_NEVER = 0,
    KYWC_OUTPUT_VRR_POLICY_ALWAYS,
    KYWC_OUTPUT_VRR_POLICY_AUTO,
};

enum kywc_output_capability {
    KYWC_OUTPUT_CAPABILITY_POWER = 1 << 0,
    KYWC_OUTPUT_CAPABILITY_BRIGHTNESS = 1 << 1,
    KYWC_OUTPUT_CAPABILITY_COLOR_TEMP = 1 << 2,
    KYWC_OUTPUT_CAPABILITY_VRR = 1 << 3,
};

struct kywc_output_state {
    bool enabled, power;
    int32_t width, height, refresh; // refresh in mHz
    enum wl_output_transform transform;
    enum kywc_output_vrr_policy vrr_policy;
    float scale;

    /* layout coord */
    int32_t lx, ly;
    uint32_t brightness;
    uint32_t color_temp;

    bool primary;
};

struct kywc_output_mode {
    int32_t width, height;
    int32_t refresh; // mHz
    bool preferred;
    struct wl_list link;
};

struct kywc_output_prop {
    bool is_virtual;
    bool is_fbdev;
    bool brightness_support;
    size_t gamma_size;
    uint32_t capabilities;
    int32_t phys_width, phys_height;          // mm
    const char *make, *model, *serial, *desc; // may be NULL
    struct wl_list modes;
};

/* connector with a monitor */
struct kywc_output {
    const char *name;
    const char *uuid;
    const char *edid; // base64
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
        struct wl_signal brightness;
        struct wl_signal color_temp;

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

struct kywc_output *kywc_output_at_point(double lx, double ly);

struct kywc_output *kywc_output_by_name(const char *name);

struct kywc_output *kywc_output_by_uuid(const char *uuid);

#endif /* _KYWC_OUTPUT_H_ */
