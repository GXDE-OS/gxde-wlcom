#ifndef _INPUT_H_
#define _INPUT_H_

#include <wayland-server-core.h>

enum kywc_input_device_type {
    KYWC_INPUT_DEVICE_KEYBOARD,
    KYWC_INPUT_DEVICE_POINTER,
    KYWC_INPUT_DEVICE_TOUCH,
    KYWC_INPUT_DEVICE_TABLET_TOOL,
    KYWC_INPUT_DEVICE_TABLET_PAD,
    KYWC_INPUT_DEVICE_SWITCH,
};

enum kywc_keyboard_modifier {
    KYWC_MODIFIER_SHIFT = 1 << 0,
    KYWC_MODIFIER_CAPS = 1 << 1,
    KYWC_MODIFIER_CTRL = 1 << 2,
    KYWC_MODIFIER_ALT = 1 << 3,
    KYWC_MODIFIER_MOD2 = 1 << 4,
    KYWC_MODIFIER_MOD3 = 1 << 5,
    KYWC_MODIFIER_LOGO = 1 << 6,
    KYWC_MODIFIER_MOD5 = 1 << 7,
};

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
    enum kywc_input_device_type type;
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

    /* adapter */
    struct wl_listener destroy;
    const struct input_impl *impl;
    void *data;
};

struct input_impl {
    void (*get_prop)(struct input *input, struct input_prop *prop);
    void (*get_state)(struct input *input, struct input_state *state);
    bool (*set_state)(struct input *output, struct input_state *state);
};

enum cursor_name {
    CURSOR_NONE,
    CURSOR_DEFAULT,
    CURSOR_MOVE,
    CURSOR_RESIZE_TOP_LEFT,
    CURSOR_RESIZE_TOP,
    CURSOR_RESIZE_TOP_RIGHT,
    CURSOR_RESIZE_RIGHT,
    CURSOR_RESIZE_BOTTOM_RIGHT,
    CURSOR_RESIZE_BOTTOM,
    CURSOR_RESIZE_BOTTOM_LEFT,
    CURSOR_RESIZE_LEFT,
};

struct cursor {
    struct seat *seat;

    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener frame;

    struct wl_listener swipe_begin;
    struct wl_listener swipe_update;
    struct wl_listener swipe_end;
    struct wl_listener pinch_begin;
    struct wl_listener pinch_update;
    struct wl_listener pinch_end;
    struct wl_listener hold_begin;
    struct wl_listener hold_end;

    struct wl_signal touch_up;
    struct wl_signal touch_down;
    struct wl_signal touch_motion;
    struct wl_signal touch_cancel;
    struct wl_signal touch_frame;

    struct wl_listener tool_axis;
    struct wl_listener tool_proximity;
    struct wl_listener tool_tip;
    struct wl_listener tool_button;
    bool tool_tip_simulation_pointer;
    bool tool_button_simulation_pointer;

    struct wl_listener request_set_cursor;
    bool client_requested;

    enum cursor_name name;
    float scale;

    /* current hover position in surface coord */
    double sx, sy;
    double lx, ly;

    void *data;
};

#define MAX_PRESSED_KEY 10

struct keyboard_state {
    struct xkb_state *xkb_state;

    uint32_t pressed_keysyms[MAX_PRESSED_KEY];
    uint32_t last_keysym;
    uint32_t last_modifiers;
    size_t npressed;
};

struct keyboard {
    struct wl_list link;
    struct seat *seat;

    struct wl_listener key;
    struct wl_listener modifiers;

    bool is_virtual;
    struct keyboard_state state;

    void *data;
};

struct seat {
    char *name;
    struct wl_list link;

    uint32_t caps; // enum wl_seat_capability

    /* input devices attached */
    struct wl_list inputs;

    // TODO: timer to hide cursor
    struct cursor *cursor;
    struct wl_list keyboards;

    struct {
        struct wl_signal destroy;
    } events;

    struct wl_listener destroy;
    const struct seat_impl *impl;
    void *data;
};

struct seat_impl {
    void (*move_cursor)(struct seat *seat, double x, double y, bool delta);
    void (*set_cursor_image)(struct seat *seat, enum cursor_name name, float scale);

    void (*add_input)(struct seat *seat, struct input *input);
    void (*remove_input)(struct seat *seat, struct input *input);

    void (*set_caps)(struct seat *seat, uint32_t caps);
    void (*destroy)(struct seat *seat);
};

struct input_manager {
    struct server *server;

    struct wl_list seats;
    struct wl_list inputs;

    struct {
        struct wl_signal new_input;
        struct wl_signal new_seat;
    } events;

    struct bindings *bindings;

    struct config *config;
    struct wl_listener server_destroy;
};

struct input_manager *input_manager_create(struct server *server);

bool input_manager_config_init(struct input_manager *input_manager);

void input_add_new_listener(struct wl_listener *listener);

struct input *input_create(const char *name, const struct input_impl *impl, void *data);

void input_destroy(struct input *input);

bool input_set_state(struct input *input, struct input_state *state);

bool input_read_config(struct input *input, struct input_state *state);

void input_write_config(struct input *input);

void input_prop_and_state_debug(struct input *input);

struct input *input_by_name(const char *name);

void input_set_seat(struct input *input, const char *seat);

/**
 * seat
 */
struct seat *seat_create(struct input_manager *input_manager, const char *name);

void seat_destroy(struct seat *seat);

void seat_add_input(struct seat *seat, struct input *input);

void seat_remove_input(struct input *input);

void seat_set_cursor_image(struct seat *seat, enum cursor_name name, float scale, bool force);

void seat_move_cursor(struct seat *seat, double x, double y, bool delta);

/**
 * libinput helper functions
 */
void libinput_get_prop(struct input *input, struct input_prop *prop);

void libinput_get_state(struct input *input, struct input_state *state);

bool libinput_set_state(struct input *input, struct input_state *state);

/**
 * cursor
 */

const char *cursor_image_by_name(enum cursor_name name);

void cursor_feed_motion(struct cursor *cursor, double lx, double ly, uint32_t time);

void cursor_feed_button(struct cursor *cursor, uint32_t button, bool pressed, uint32_t time);

/**
 * keyboard
 */

uint32_t keyboard_get_modifier_mask_by_name(const char *name);

const char *keyboard_get_modifier_name_by_mask(uint32_t modifier);

void keyboard_feed_key(struct keyboard *keyboard, uint32_t key, bool pressed, uint32_t time,
                       uint32_t modifiers);

void keyboard_feed_modifiers(struct keyboard *keyboard, uint32_t depressed, uint32_t latched,
                             uint32_t locked, uint32_t group);

/**
 * bindings
 */

struct bindings *bindings_create(struct input_manager *input_manager);

void bindings_destroy(struct bindings *bindings);

bool bindings_handle_key_binding(struct keyboard_state *keyboard_state);

#endif /* _INPUT_H_ */
