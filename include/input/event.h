#ifndef _EVENT_H_
#define _EVENT_H_

#include "input/seat.h"

#define LEFT_BUTTON_PRESSED(button, pressed) (button == BTN_LEFT && pressed)
#define LEFT_BUTTON_RELEASED(button, pressed) (button == BTN_LEFT && !pressed)
#define RIGHT_BUTTON_PRESSED(button, pressed) (button == BTN_RIGHT && pressed)
#define RIGHT_BUTTON_RELEASED(button, pressed) (button == BTN_RIGHT && !pressed)

struct input_event_node_impl {
    /**
     * called when cursor over this node,
     * return true if support hold
     */
    bool (*hover)(struct seat *seat, struct ky_scene_node *node, double x, double y, uint32_t time,
                  bool first, bool hold, void *data);
    /**
     * called when pointer button click on this node,
     * dual is true when double clicked
     */
    void (*click)(struct seat *seat, struct ky_scene_node *node, uint32_t button, bool pressed,
                  uint32_t time, bool dual, void *data);
    /**
     * called when pointer leave this node,
     * last is true when leaving the root
     */
    void (*leave)(struct seat *seat, struct ky_scene_node *node, bool last, void *data);
};

typedef struct ky_scene_node *(*input_event_node_get_root)(void *data);

struct input_event_node {
    const struct input_event_node_impl *impl;
    /* free input_event_node when node destroy */
    struct wl_listener node_destroy;
    /* return the root this event node beloong to */
    input_event_node_get_root get_root;
    void *data;
};

struct input_event_node *input_event_node_create(struct ky_scene_node *node,
                                                 const struct input_event_node_impl *impl,
                                                 input_event_node_get_root get_root, void *data);

struct input_event_node *input_event_dumb_node_create(struct ky_scene_node *node,
                                                      struct ky_scene_node *root);

/* found event node from node and node's parent */
struct input_event_node *input_event_node_from_node(struct ky_scene_node *node);

struct ky_scene_node *input_event_node_root(struct input_event_node *event_node);

#endif /* _EVENT_H_ */
