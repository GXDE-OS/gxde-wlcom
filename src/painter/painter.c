#include "painter_p.h"

#define PI 3.1415926
#define ANGLE(ang) (ang * PI / 180.0)

static void buffer_draw(struct cairo_buffer *buffer, struct draw_info *info)
{
    cairo_t *cairo = buffer->cairo;
    cairo_surface_t *surf = buffer->surface;
    double width = buffer->width;
    double height = buffer->height;
    double radius = info->corner_radius;

    if (info->circle) {
        cairo_arc(cairo, width / 2, height / 2, radius, 0, 2 * PI);
    } else {
        if (info->corner_mask & CORNER_MASK_TOP_LEFT) {
            cairo_arc(cairo, radius, radius, radius, ANGLE(-180), ANGLE(-90));
        } else {
            cairo_line_to(cairo, 0, 0);
        }
        if (info->corner_mask & CORNER_MASK_TOP_RIGHT) {
            cairo_arc(cairo, width - radius, radius, radius, ANGLE(-90), ANGLE(0));
        } else {
            cairo_line_to(cairo, width, 0);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_RIGHT) {
            cairo_arc(cairo, width - radius, height - radius, radius, ANGLE(0), ANGLE(90));
        } else {
            cairo_line_to(cairo, width, height);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_LEFT) {
            cairo_arc(cairo, radius, height - radius, radius, ANGLE(90), ANGLE(180));
        } else {
            cairo_line_to(cairo, 0, height);
        }
    }

    cairo_close_path(cairo);
    cairo_set_source_rgba(cairo, info->solid_rgba[0], info->solid_rgba[1], info->solid_rgba[2],
                          info->solid_rgba[3]);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_fill(cairo);

    if (!info->border_rgba || !info->border_width || (!info->border_mask && !info->circle)) {
        cairo_surface_flush(surf);
        return;
    }

    /* border line */
    double half = info->border_width / 2.0;
    cairo_set_line_cap(cairo, CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgba(cairo, info->border_rgba[0], info->border_rgba[1], info->border_rgba[2],
                          info->border_rgba[3]);
    cairo_set_line_width(cairo, info->border_width);

    if (info->circle) {
        cairo_arc(cairo, width / 2, height / 2, radius + half, 0, 2 * PI);
        cairo_stroke(cairo);
        cairo_surface_flush(surf);
        return;
    }

    if (info->border_mask & BORDER_MASK_TOP) {
        if (info->corner_mask & CORNER_MASK_TOP_LEFT) {
            cairo_move_to(cairo, half, radius);
            cairo_arc(cairo, radius, radius, radius - half, ANGLE(-180), ANGLE(-90));
        } else {
            cairo_move_to(cairo, 0, half);
        }
        if (info->corner_mask & CORNER_MASK_TOP_RIGHT) {
            cairo_line_to(cairo, width - radius, half);
            cairo_arc(cairo, width - radius, radius, radius - half, ANGLE(-90), ANGLE(0));
        } else {
            cairo_line_to(cairo, width, half);
        }
        cairo_stroke(cairo);
    }

    if (info->border_mask & BORDER_MASK_RIGHT) {
        if (info->corner_mask & CORNER_MASK_TOP_RIGHT) {
            bool have_top_right = info->border_mask & BORDER_MASK_TOP;
            if (have_top_right) {
                cairo_move_to(cairo, width - half, radius);
            } else {
                cairo_move_to(cairo, width - radius, half);
                cairo_arc(cairo, width - radius, radius, radius - half, ANGLE(-90), ANGLE(0));
            }
        } else {
            cairo_move_to(cairo, width - half, 0);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_RIGHT) {
            cairo_line_to(cairo, width - half, height - radius);
            cairo_arc(cairo, width - radius, height - radius, radius - half, ANGLE(0), ANGLE(90));
        } else {
            cairo_line_to(cairo, width - half, height);
        }
        cairo_stroke(cairo);
    }

    if (info->border_mask & BORDER_MASK_BOTTOM) {
        if (info->corner_mask & CORNER_MASK_BOTTOM_RIGHT) {
            bool have_bottom_right = info->border_mask & BORDER_MASK_RIGHT;
            if (have_bottom_right) {
                cairo_move_to(cairo, width - radius, height - half);
            } else {
                cairo_move_to(cairo, width - half, height - radius);
                cairo_arc(cairo, width - radius, height - radius, radius - half, ANGLE(0),
                          ANGLE(90));
            }
        } else {
            cairo_move_to(cairo, width, height - half);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_LEFT) {
            cairo_line_to(cairo, radius, height - half);
            cairo_arc(cairo, radius, height - radius, radius - half, ANGLE(90), ANGLE(180));
        } else {
            cairo_line_to(cairo, 0, height - half);
        }
        cairo_stroke(cairo);
    }

    if (info->border_mask & BORDER_MASK_LEFT) {
        if (info->corner_mask & CORNER_MASK_BOTTOM_LEFT) {
            bool have_bottom_left = info->border_mask & BORDER_MASK_BOTTOM;
            if (have_bottom_left) {
                cairo_move_to(cairo, half, height - radius);
            } else {
                cairo_move_to(cairo, radius, height - half);
                cairo_arc(cairo, radius, height - radius, radius - half, ANGLE(90), ANGLE(180));
            }
        } else {
            cairo_move_to(cairo, half, height);
        }
        if (info->corner_mask & CORNER_MASK_TOP_LEFT) {
            bool have_top_left = info->border_mask & BORDER_MASK_TOP;
            if (have_top_left) {
                cairo_line_to(cairo, half, radius);
            } else {
                cairo_line_to(cairo, half, radius);
                cairo_arc(cairo, radius, radius, radius - half, ANGLE(-180), ANGLE(-90));
            }
        } else {
            cairo_line_to(cairo, half, 0);
        }
        cairo_stroke(cairo);
    }

    cairo_surface_flush(surf);
}

struct wlr_buffer *painter_draw_buffer(struct draw_info *info)
{
    /* auto resize to text size */
    if (info->auto_resize && info->text && *info->text) {
        int width, height;
        text_extents(info->font, info->font_size, info->text, &width, &height);
        if (width < info->width) {
            info->width = width;
        }
        if (height < info->height) {
            info->height = height;
        }
        info->align = TEXT_ALIGN_CENTER;
        info->submenu = false;
    }

    struct cairo_buffer *buffer = cairo_buffer_create(info->width, info->height, info->scale);
    if (!buffer) {
        return NULL;
    }

    /* corner, solid and border */
    if (info->solid_rgba) {
        buffer_draw(buffer, info);
    }

    /* text */
    if (info->text && *info->text) {
        cairo_buffer_draw_text(buffer, info->text, info->font, info->font_size, info->font_rgba,
                               info->align, info->submenu);
    }

    /* svg picture */
    if (info->svg && !cairo_buffer_draw_svg(buffer, info->svg)) {
        wlr_buffer_drop(&buffer->base);
        return NULL;
    }

    /* blur */
    if (info->blur_margin > 0 && !cairo_buffer_draw_blur(buffer, info->blur_margin, info->circle)) {
        wlr_buffer_drop(&buffer->base);
        return NULL;
    }

    return &buffer->base;
}

void painter_buffer_unscaled_size(struct wlr_buffer *buffer, int *width, int *height)
{
    struct cairo_buffer *buf = cairo_buffer_from_wlr_buffer(buffer);
    if (buf && width) {
        *width = buf->width;
    }
    if (buf && height) {
        *height = buf->height;
    }
}

void painter_buffer_to_file(struct wlr_buffer *buffer, const char *name)
{
    struct cairo_buffer *buf = cairo_buffer_from_wlr_buffer(buffer);
    if (!buf) {
        return;
    }
    cairo_surface_write_to_png(buf->surface, name);
}
