// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _INPUT_H_
#define _INPUT_H_

#include <wayland-server-core.h>
#include <wlr/types/wlr_input_device.h>

struct server;

struct input_state {
    const char *mapped_to_output;
    const char *seat;

    uint32_t send_events_mode;
    uint32_t click_method;

    /* useful when prop.tap_finger_count > 0 */
    bool tap_to_click;
    bool tap_and_drag;
    bool tap_drag_lock;
    uint32_t tap_button_map; // LRM or LMR

    /* useful when prop.has_pointer_accel is true */
    float pointer_accel_speed; // [-1, 1]
    uint32_t accel_profile;

    bool natural_scroll;
    bool left_handed;
    /* When enabled, a simultaneous press of the left and right button generates a middle mouse
     * button event. Releasing the buttons generates a middle mouse button release, the left and
     * right button events are discarded otherwise. */
    bool middle_emulation;

    uint32_t scroll_method;
    /* when scroll_method is LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN */
    uint32_t scroll_button;
    bool scroll_button_lock;

    bool dwt;
    bool dwtp;

    uint32_t rotation_angle; // CW

    float calibration_matrix[6];

    /* for keyboard */
    const char *xkb_layout;
    const char *xkb_model;
    const char *xkb_options;
    const char *xkb_rules;
    const char *xkb_variant;
    int repeat_delay;
    int repeat_rate;
};

struct input_prop {
    enum wlr_input_device_type type;
    unsigned int vendor, product;
    bool is_virtual;

    union {
        struct {
            /* how many fingers can be used for tapping,
             * 0 if the device does not support tapping  */
            uint32_t tap_finger_count : 3;
            /* https://wayland.freedesktop.org/libinput/doc/latest/clickpad-softbuttons.html */
            uint32_t click_methods : 3;
            uint32_t scroll_methods : 3;
            uint32_t accel_profiles : 3;
            /* https://wayland.freedesktop.org/libinput/doc/latest/configuration.html */
            /* bitmask */
            uint32_t send_events_modes : 3;
            /* whether a device can have a custom rotation applied */
            uint32_t has_rotation : 1;
            /* if this device supports configurable disable-while-trackpointing feature */
            uint32_t has_dwtp : 1;
            /* if this device supports configurable disable-while-typing feature */
            uint32_t has_dwt : 1;
            /* if middle mouse button emulation configuration is available */
            uint32_t has_middle_emulation : 1;
            /* if a device has a configuration that supports left-handed usage */
            uint32_t has_left_handed : 1;
            /* if the device supports "natural scrolling" */
            uint32_t has_natural_scroll : 1;
            /* if the device can be calibrated via a calibration matrix */
            uint32_t has_calibration_matrix : 1;
            /* if a device uses libinput-internal pointer-acceleration */
            uint32_t has_pointer_accel : 1;
            uint32_t support_mapped_to_output : 1;
            uint32_t : 8;
        };
        uint32_t prop;
    };
};

struct input {
    struct wlr_input_device *wlr_input;
    struct wl_list link;

    const char *name;
    struct input_manager *manager;
    struct libinput_device *device;

    /* seat that input device attached */
    struct seat *seat;
    struct wl_list seat_link;

    /* output that mapped to */
    char *desired_mapped_output;
    struct kywc_output *mapped_output;
    struct wl_listener mapped_output_off;
    struct wl_listener mapped_output_destroy;

    /* input device prop and state per device */
    struct input_prop prop;
    struct input_state state;
    struct input_state default_state;

    struct {
        struct wl_signal destroy;
    } events;

    struct wl_listener destroy;
};

struct input_manager *input_manager_create(struct server *server);

struct seat *input_manager_get_default_seat(void);

struct output *input_current_output(struct seat *seat);

void input_add_new_listener(struct wl_listener *listener);

bool input_set_state(struct input *input, struct input_state *state);

struct input *input_by_name(const char *name);

struct input *input_from_wlr_input(struct wlr_input_device *wlr_input);

void input_set_seat(struct input *input, const char *seat);

void input_rebase_all_cursor(void);

#endif /* _INPUT_H_ */
