#ifndef _SEAT_H_
#define _SEAT_H_

#include "input.h"
#include "scene/scene.h"

struct seat_pointer_grab;

struct seat_pointer_grab_interface {
    bool (*motion)(struct seat_pointer_grab *grab, uint32_t time, double lx, double ly);
    bool (*button)(struct seat_pointer_grab *grab, uint32_t time, uint32_t button, bool pressed);
    bool (*axis)(struct seat_pointer_grab *grab, uint32_t time, bool vertical, double value);
    void (*cancel)(struct seat_pointer_grab *grab);
};

struct seat_pointer_grab {
    const struct seat_pointer_grab_interface *interface;
    struct seat *seat;
    void *data;
};

// TODO: interal grab for compositor: pointer_grab keyboard_grab touch_grab
// struct seat_keyboard_grab *keyboard_grab;

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

    struct seat_pointer_grab *pointer_grab;

    struct ky_scene *scene;
    struct wlr_output_layout *layout;
    struct wlr_pointer_gestures_v1 *pointer_gestures;

    struct input_method_relay *relay;
    struct selection *selection;

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

bool seat_set_pointer_grab(struct seat *seat, struct seat_pointer_grab *pointer_grab);

struct wlr_surface;
void seat_notify_motion(struct seat *seat, struct wlr_surface *surface, uint32_t time, double sx,
                        double sy, bool first_enter);
void seat_notify_button(struct seat *seat, uint32_t time, uint32_t button, bool pressed);
void seat_notify_leave(struct seat *seat, struct wlr_surface *surface);

/* keyboard focus */
void seat_focus_surface(struct seat *seat, struct wlr_surface *surface);

#endif /* _SEAT_H_ */
