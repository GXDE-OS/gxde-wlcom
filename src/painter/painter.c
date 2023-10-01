// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "painter_p.h"

#define PI 3.1415926
#define ANGLE(ang) (ang * PI / 180.0)

static void buffer_draw(struct cairo_buffer *buffer, struct draw_info *info, struct kywc_box *box,
                        bool hover)
{
    cairo_t *cairo = buffer->cairo;
    cairo_surface_t *surf = buffer->surface;
    double width = box->width;
    double height = box->height;
    double radius = info->corner_radius;

    if (info->circle) {
        cairo_arc(cairo, box->x + width / 2, box->y + height / 2, radius, 0, 2 * PI);
    } else {
        if (info->corner_mask & CORNER_MASK_TOP_LEFT) {
            cairo_arc(cairo, box->x + radius, box->y + radius, radius, ANGLE(-180), ANGLE(-90));
        } else {
            cairo_line_to(cairo, box->x, box->y);
        }
        if (info->corner_mask & CORNER_MASK_TOP_RIGHT) {
            cairo_arc(cairo, box->x + width - radius, box->y + radius, radius, ANGLE(-90),
                      ANGLE(0));
        } else {
            cairo_line_to(cairo, box->x + width, box->y);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_RIGHT) {
            cairo_arc(cairo, box->x + width - radius, box->y + height - radius, radius, ANGLE(0),
                      ANGLE(90));
        } else {
            cairo_line_to(cairo, box->x + width, box->y + height);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_LEFT) {
            cairo_arc(cairo, box->x + radius, box->y + height - radius, radius, ANGLE(90),
                      ANGLE(180));
        } else {
            cairo_line_to(cairo, box->x, box->y + height);
        }
    }

    cairo_close_path(cairo);
    cairo_set_source_rgba(cairo, info->solid_rgba[0], info->solid_rgba[1], info->solid_rgba[2],
                          info->solid_rgba[3]);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_fill(cairo);

    if (hover) {
        if (info->circle) {
            cairo_arc(cairo, box->x + width / 2, box->y + height / 2, radius, 0, 2 * PI);
        } else {
            double offset = box->width * 0.02;
            double x = box->x + offset;
            double y = box->y + offset;
            double w = box->width - 2 * offset;
            double h = box->height - 2 * offset;
            cairo_arc(cairo, x + radius, y + radius, radius, ANGLE(-180), ANGLE(-90));
            cairo_arc(cairo, x + w - radius, y + radius, radius, ANGLE(-90), ANGLE(0));
            cairo_arc(cairo, x + w - radius, y + h - radius, radius, ANGLE(0), ANGLE(90));
            cairo_arc(cairo, x + radius, y + h - radius, radius, ANGLE(90), ANGLE(180));
        }
        cairo_close_path(cairo);
        cairo_set_source_rgba(cairo, info->hover_rgba[0], info->hover_rgba[1], info->hover_rgba[2],
                              info->hover_rgba[3]);
        cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
        cairo_fill(cairo);
    }

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
        cairo_arc(cairo, box->x + width / 2, box->y + height / 2, radius + half, 0, 2 * PI);
        cairo_stroke(cairo);
        cairo_surface_flush(surf);
        return;
    }

    if (info->border_mask & BORDER_MASK_TOP) {
        if (info->corner_mask & CORNER_MASK_TOP_LEFT) {
            cairo_move_to(cairo, box->x + half, box->y + radius);
            cairo_arc(cairo, box->x + radius, box->y + radius, radius - half, ANGLE(-180),
                      ANGLE(-90));
        } else {
            cairo_move_to(cairo, box->x, box->y + half);
        }
        if (info->corner_mask & CORNER_MASK_TOP_RIGHT) {
            cairo_line_to(cairo, box->x + width - radius, box->y + half);
            cairo_arc(cairo, box->x + width - radius, box->y + radius, radius - half, ANGLE(-90),
                      ANGLE(0));
        } else {
            cairo_line_to(cairo, box->x + width, box->y + half);
        }
        cairo_stroke(cairo);
    }

    if (info->border_mask & BORDER_MASK_RIGHT) {
        if (info->corner_mask & CORNER_MASK_TOP_RIGHT) {
            bool have_top_right = info->border_mask & BORDER_MASK_TOP;
            if (have_top_right) {
                cairo_move_to(cairo, box->x + width - half, box->y + radius);
            } else {
                cairo_move_to(cairo, box->x + width - radius, box->y + half);
                cairo_arc(cairo, box->x + width - radius, box->y + radius, radius - half,
                          ANGLE(-90), ANGLE(0));
            }
        } else {
            cairo_move_to(cairo, box->x + width - half, box->y);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_RIGHT) {
            cairo_line_to(cairo, box->x + width - half, box->y + height - radius);
            cairo_arc(cairo, box->x + width - radius, box->y + height - radius, radius - half,
                      ANGLE(0), ANGLE(90));
        } else {
            cairo_line_to(cairo, box->x + width - half, box->y + height);
        }
        cairo_stroke(cairo);
    }

    if (info->border_mask & BORDER_MASK_BOTTOM) {
        if (info->corner_mask & CORNER_MASK_BOTTOM_RIGHT) {
            bool have_bottom_right = info->border_mask & BORDER_MASK_RIGHT;
            if (have_bottom_right) {
                cairo_move_to(cairo, box->x + width - radius, box->y + height - half);
            } else {
                cairo_move_to(cairo, box->x + width - half, box->y + height - radius);
                cairo_arc(cairo, box->x + width - radius, box->y + height - radius, radius - half,
                          ANGLE(0), ANGLE(90));
            }
        } else {
            cairo_move_to(cairo, box->x + width, box->y + height - half);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_LEFT) {
            cairo_line_to(cairo, box->x + radius, box->y + height - half);
            cairo_arc(cairo, box->x + radius, box->y + height - radius, radius - half, ANGLE(90),
                      ANGLE(180));
        } else {
            cairo_line_to(cairo, box->x, box->y + height - half);
        }
        cairo_stroke(cairo);
    }

    if (info->border_mask & BORDER_MASK_LEFT) {
        if (info->corner_mask & CORNER_MASK_BOTTOM_LEFT) {
            bool have_bottom_left = info->border_mask & BORDER_MASK_BOTTOM;
            if (have_bottom_left) {
                cairo_move_to(cairo, box->x + half, box->y + height - radius);
            } else {
                cairo_move_to(cairo, box->x + radius, box->y + height - half);
                cairo_arc(cairo, box->x + radius, box->y + height - radius, radius - half,
                          ANGLE(90), ANGLE(180));
            }
        } else {
            cairo_move_to(cairo, box->x + half, box->y + height);
        }
        if (info->corner_mask & CORNER_MASK_TOP_LEFT) {
            bool have_top_left = info->border_mask & BORDER_MASK_TOP;
            if (have_top_left) {
                cairo_line_to(cairo, box->x + half, box->y + radius);
            } else {
                cairo_line_to(cairo, box->x + half, box->y + radius);
                cairo_arc(cairo, box->x + radius, box->y + radius, radius - half, ANGLE(-180),
                          ANGLE(-90));
            }
        } else {
            cairo_line_to(cairo, box->x + half, box->y);
        }
        cairo_stroke(cairo);
    }

    cairo_surface_flush(surf);
}

static bool painter_draw(struct cairo_buffer *buffer, struct draw_info *info, bool clear)
{
    /* clear the surface */
    if (clear) {
        cairo_set_operator(buffer->cairo, CAIRO_OPERATOR_CLEAR);
        cairo_paint(buffer->cairo);
        cairo_move_to(buffer->cairo, 0, 0);
    }

    /* corner, solid and border */
    if (info->solid_rgba) {
        if (info->hover_rgba) {
            int height = info->height / 2;
            buffer_draw(buffer, info, &(struct kywc_box){ 0, 0, info->width, height }, false);
            buffer_draw(buffer, info, &(struct kywc_box){ 0, height, info->width, height }, true);
        } else {
            buffer_draw(buffer, info, &(struct kywc_box){ 0, 0, info->width, info->height }, false);
        }
    }

    /* text */
    if (info->text && *info->text) {
        if (info->hover_rgba) {
            int height = info->height / 2;
            cairo_buffer_draw_text(buffer, info, &(struct kywc_box){ 0, 0, info->width, height });
            cairo_buffer_draw_text(buffer, info,
                                   &(struct kywc_box){ 0, height, info->width, height });
        } else {
            cairo_buffer_draw_text(buffer, info,
                                   &(struct kywc_box){ 0, 0, info->width, info->height });
        }
    }

    /* svg picture */
    if (info->svg) {
        if (info->hover_svg) {
            int height = info->height / 2;
            cairo_buffer_draw_svg(buffer, info->svg,
                                  &(struct kywc_box){ 0, 0, info->width, height });
            cairo_buffer_draw_svg(buffer, info->hover_svg,
                                  &(struct kywc_box){ 0, height, info->width, height });
        } else {
            cairo_buffer_draw_svg(buffer, info->svg,
                                  &(struct kywc_box){ 0, 0, info->width, info->height });
        }
    }

    /* blur */
    if (info->blur_margin > 0 && !cairo_buffer_draw_blur(buffer, info->blur_margin, info->circle)) {
        return false;
    }

    return true;
}

struct wlr_buffer *painter_draw_buffer(struct draw_info *info)
{
    /* auto resize to text size */
    if (info->auto_resize && info->text && *info->text) {
        int width, height;
        text_extents(info->font, info->font_size, info->text, &width, &height);
        height += height / 2;
        width += height / 2;
        if (width < info->width) {
            info->width = width;
        }
        if (height < info->height) {
            info->height = height;
        }
        info->align = TEXT_ALIGN_CENTER;
        info->submenu = false;
    }

    /* draw hover together */
    if (info->hover_rgba || info->hover_svg) {
        info->height *= 2;
    }

    struct cairo_buffer *buffer = cairo_buffer_create(info->width, info->height, info->scale);
    if (!buffer) {
        return NULL;
    }

    if (!painter_draw(buffer, info, false)) {
        wlr_buffer_drop(&buffer->base);
        return NULL;
    }

    return &buffer->base;
}

bool painter_redraw_buffer(struct wlr_buffer *buffer, struct draw_info *info)
{
    struct cairo_buffer *buf = cairo_buffer_from_wlr_buffer(buffer);
    if (!buf) {
        return false;
    }

    /* fix size to buffer unscaled size */
    info->width = buf->width;
    info->height = buf->height;
    return painter_draw(buf, info, true);
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

void painter_buffer_size(struct wlr_buffer *buffer, int *width, int *height)
{
    struct cairo_buffer *buf = cairo_buffer_from_wlr_buffer(buffer);
    if (buf && width) {
        *width = buf->base.width;
    }
    if (buf && height) {
        *height = buf->base.height;
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

struct wlr_buffer *painter_create_buffer(int width, int height, float scale)
{
    struct cairo_buffer *buffer = cairo_buffer_create(width, height, scale);
    if (!buffer) {
        return NULL;
    }

    return &buffer->base;
}
