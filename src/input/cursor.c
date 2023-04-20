#include <kywc/log.h>

#include "input.h"

/* cursor images used in compositor */
static char *cursor_image[] = {
    "",
    "left_ptr",
    "grabbing",
    "top_left_corner",
    "top_side",
    "top_right_corner",
    "right_side",
    "bottom_right_corner",
    "bottom_side",
    "bottom_left_corner",
    "left_side",
};

static char *cursor_button[] = {
    "left", "right", "middle", "side", "extra", "forward", "back", "task",
};

const char *cursor_image_by_name(enum cursor_name name)
{
    return cursor_image[name];
}

void cursor_feed_motion(struct cursor *cursor, double lx, double ly, uint32_t time)
{
    cursor->lx = lx;
    cursor->ly = ly;

    kywc_log(KYWC_DEBUG, "cursor move to (%f, %f)", cursor->lx, cursor->ly);
}

void cursor_feed_button(struct cursor *cursor, uint32_t button, bool pressed, uint32_t time)
{
    kywc_log(KYWC_DEBUG, "cursor %s button %s", cursor_button[button - 0x110],
             pressed ? "pressed" : "released");
}
