#ifndef _TEXT_INPUT_V1_H_
#define _TEXT_INPUT_V1_H_

#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct text_input_v1 {
    struct wl_resource *resource;
    struct wl_list link;

    struct wlr_seat *seat;
    struct wl_listener seat_destroy;

    struct wlr_surface *surface;
    struct wl_listener surface_destroy;

    struct wlr_box cursor_rectangle;

    struct {
        bool pending;
        char *text;
        uint32_t cursor;
        uint32_t anchor;
    } surrounding;

    struct {
        bool pending;
        uint32_t hint;
        uint32_t purpose;
    } content_type;

    uint32_t serial;
    bool activated;

    struct {
        struct wl_signal activate;
        struct wl_signal deactivate;
        struct wl_signal commit;
        struct wl_signal destroy;
    } events;
};

struct text_input_manager_v1 {
    struct wl_global *global;
    struct wl_list text_inputs;

    struct wl_listener display_destroy;

    struct {
        struct wl_signal text_input;
        struct wl_signal destroy;
    } events;
};

struct text_input_manager_v1 *text_input_manager_v1_create(struct wl_display *display);

void text_input_v1_send_enter(struct text_input_v1 *text_input, struct wlr_surface *surface);

void text_input_v1_send_leave(struct text_input_v1 *text_input);

void text_input_v1_send_preedit_string(struct text_input_v1 *text_input, const char *text,
                                       int32_t cursor_begin);

void text_input_v1_send_commit_string(struct text_input_v1 *text_input, const char *text);

void text_input_v1_send_delete_surrounding_text(struct text_input_v1 *text_input, const char *text,
                                                uint32_t before_length, uint32_t after_length);

#endif /* _TEXT_INPUT_V1_H_ */
