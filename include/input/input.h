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

    bool support_mapped_to_output;
    bool is_virtual;

    /* https://wayland.freedesktop.org/libinput/doc/latest/configuration.html */
    /* bitmask */
    uint32_t send_events_modes;
    /* https://wayland.freedesktop.org/libinput/doc/latest/clickpad-softbuttons.html */
    uint32_t click_methods;
    uint32_t scroll_methods;
    uint32_t accel_profiles;

    /* how many fingers can be used for tapping,
     * 0 if the device does not support tapping  */
    uint32_t tap_finger_count;

    /* if the device can be calibrated via a calibration matrix */
    bool has_calibration_matrix;
    /* if a device uses libinput-internal pointer-acceleration */
    bool has_pointer_accel;
    /* if the device supports "natural scrolling" */
    bool has_natural_scroll;
    /* if a device has a configuration that supports left-handed usage */
    bool has_left_handed;
    /* if middle mouse button emulation configuration is available */
    bool has_middle_emulation;
    /* if this device supports configurable disable-while-typing feature */
    bool has_dwt;
    /* if this device supports configurable disable-while-trackpointing feature */
    bool has_dwtp;
    /* whether a device can have a custom rotation applied */
    bool has_rotation;
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

    struct {
        struct wl_signal destroy;
    } events;

    struct wl_listener destroy;
};

struct input_manager {
    struct server *server;

    struct wl_list seats;
    struct wl_list inputs;

    struct {
        struct wl_signal new_input;
        struct wl_signal new_seat;
    } events;

    struct config *config;
    struct bindings *bindings;

    struct wl_listener new_input;
    struct wl_listener new_virtual_pointer;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener server_destroy;
};

struct input_manager *input_manager_create(struct server *server);

bool input_manager_config_init(struct input_manager *input_manager);

void input_add_new_listener(struct wl_listener *listener);

bool input_set_state(struct input *input, struct input_state *state);

bool input_read_config(struct input *input, struct input_state *state);

void input_write_config(struct input *input);

void input_prop_and_state_debug(struct input *input);

struct input *input_by_name(const char *name);

void input_set_seat(struct input *input, const char *seat);

#endif /* _INPUT_H_ */
