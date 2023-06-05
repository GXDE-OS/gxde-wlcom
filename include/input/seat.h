#ifndef _SEAT_H_
#define _SEAT_H_

#include "input.h"
#include "scene/scene.h"

struct seat {
    struct wlr_seat *wlr_seat;
    char *name;
    struct wl_list link;

    uint32_t caps; // enum wl_seat_capability

    /* input devices attached */
    struct wl_list inputs;

    // TODO: timer to hide cursor
    struct cursor *cursor;
    struct wl_list keyboards;

    struct ky_scene *scene;
    struct wlr_output_layout *layout;

    struct {
        struct wl_signal destroy;
    } events;

    struct wl_listener destroy;
};

struct seat *seat_create(struct input_manager *input_manager, const char *name);

void seat_destroy(struct seat *seat);

void seat_add_input(struct seat *seat, struct input *input);

void seat_remove_input(struct input *input);

struct seat *seat_from_resource(struct wl_resource *resource);

struct seat *seat_from_wlr_seat(struct wlr_seat *wlr_seat);

struct wlr_surface;
void seat_notify_motion(struct seat *seat, struct wlr_surface *surface, uint32_t time, double sx,
                        double sy, bool first_enter);
void seat_notify_button(struct seat *seat, uint32_t time, uint32_t button, bool pressed);
void seat_notify_leave(struct seat *seat, struct wlr_surface *surface);

/* keyboard focus */
void seat_focus_surface(struct seat *seat, struct wlr_surface *surface);

#endif /* _SEAT_H_ */
