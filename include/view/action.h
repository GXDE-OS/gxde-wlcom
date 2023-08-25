#ifndef _ACTION_H_
#define _ACTION_H_

#include "view.h"

struct seat;

enum window_action {
    WINDOW_ACTION_NONE = 0,
    WINDOW_ACTION_MINIMIZE,
    WINDOW_ACTION_MAXIMIZE,
    WINDOW_ACTION_FULLSCREEN,
    WINDOW_ACTION_CLOSE,
    WINDOW_ACTION_MOVE,
    WINDOW_ACTION_RESIZE,
    WINDOW_ACTION_KEEP_ABOVE,
    WINDOW_ACTION_KEEP_BELOW,
    WINDOW_ACTION_MENU,
    WINDOW_ACTION_SNAP_TOP,
    WINDOW_ACTION_SNAP_BOTTOM,
    WINDOW_ACTION_SNAP_LEFT,
    WINDOW_ACTION_SNAP_RIGHT,
};

void window_action(struct view *view, struct seat *seat, enum window_action action);

#endif
