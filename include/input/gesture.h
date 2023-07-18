#ifndef _GESTURE_H_
#define _GESTURE_H_

#include <kywc/binding.h>

struct gesture_state {
    enum gesture_type type;
    enum gesture_device device;
    enum gesture_edge edge;
    uint8_t fingers;
    uint32_t directions;

    double dx, dy;
    double scale;
    double rotation;

    /* timer for hold gesture */
    struct wl_event_source *timer;
};

void gesture_state_init(struct gesture_state *state, void *display);

void gesture_state_finish(struct gesture_state *state);

void gesture_state_begin(struct gesture_state *state, enum gesture_type type,
                         enum gesture_device device, enum gesture_edge edge, uint8_t fingers);

void gesture_state_update(struct gesture_state *state, enum gesture_type type,
                          enum gesture_device device, double dx, double dy, double scale,
                          double rotation);

bool gesture_state_end(struct gesture_state *state, enum gesture_type type,
                       enum gesture_device device, bool cancelled);

#endif /* _GESTURE_H_ */
