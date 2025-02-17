// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

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

    if (info->solid_rgba) {
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

        cairo_close_path(cairo);
        cairo_set_source_rgba(cairo, info->solid_rgba[0], info->solid_rgba[1], info->solid_rgba[2],
                              info->solid_rgba[3]);
        cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
        cairo_fill(cairo);
    }

    if (hover) {
        double offset = box->height * 0.1;
        double x = box->x + offset;
        double y = box->y + offset;
        double w = box->width - 2 * offset;
        double h = box->height - 2 * offset;
        float radius = info->hover_radius;
        cairo_arc(cairo, x + radius, y + radius, radius, ANGLE(-180), ANGLE(-90));
        cairo_arc(cairo, x + w - radius, y + radius, radius, ANGLE(-90), ANGLE(0));
        cairo_arc(cairo, x + w - radius, y + h - radius, radius, ANGLE(0), ANGLE(90));
        cairo_arc(cairo, x + radius, y + h - radius, radius, ANGLE(90), ANGLE(180));

        cairo_close_path(cairo);
        cairo_set_source_rgba(cairo, info->hover_rgba[0], info->hover_rgba[1], info->hover_rgba[2],
                              info->hover_rgba[3]);
        cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
        cairo_fill(cairo);
    }

    if (!info->border_rgba || !info->border_width || !info->border_mask) {
        cairo_surface_flush(surf);
        return;
    }

    /* border line */
    double half = info->border_width / 2.0;
    cairo_set_source_rgba(cairo, info->border_rgba[0], info->border_rgba[1], info->border_rgba[2],
                          info->border_rgba[3]);
    cairo_set_line_width(cairo, info->border_width);

    if (info->border_mask & BORDER_MASK_TOP) {
        if (info->corner_mask & CORNER_MASK_TOP_LEFT) {
            cairo_move_to(cairo, box->x + half, box->y + radius);
            cairo_arc(cairo, box->x + radius, box->y + radius, radius - half, ANGLE(-180),
                      ANGLE(-90));
        } else {
            cairo_move_to(cairo, box->x + half, box->y + half);
        }
        if (info->corner_mask & CORNER_MASK_TOP_RIGHT) {
            cairo_line_to(cairo, box->x + width - radius, box->y + half);
            cairo_arc(cairo, box->x + width - radius, box->y + radius, radius - half, ANGLE(-90),
                      ANGLE(0));
        } else {
            cairo_line_to(cairo, box->x + width - half, box->y + half);
        }
        cairo_stroke(cairo);
    }

    if (info->border_mask & BORDER_MASK_RIGHT) {
        if (info->corner_mask & CORNER_MASK_TOP_RIGHT) {
            if (info->border_mask & BORDER_MASK_TOP) {
                cairo_move_to(cairo, box->x + width - half, box->y + radius);
            } else {
                cairo_move_to(cairo, box->x + width - radius, box->y + half);
                cairo_arc(cairo, box->x + width - radius, box->y + radius, radius - half,
                          ANGLE(-90), ANGLE(0));
            }
        } else {
            cairo_move_to(cairo, box->x + width - half, box->y + half);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_RIGHT) {
            cairo_line_to(cairo, box->x + width - half, box->y + height - radius);
            cairo_arc(cairo, box->x + width - radius, box->y + height - radius, radius - half,
                      ANGLE(0), ANGLE(90));
        } else {
            cairo_line_to(cairo, box->x + width - half, box->y + height - half);
        }
        cairo_stroke(cairo);
    }

    if (info->border_mask & BORDER_MASK_BOTTOM) {
        if (info->corner_mask & CORNER_MASK_BOTTOM_RIGHT) {
            if (info->border_mask & BORDER_MASK_RIGHT) {
                cairo_move_to(cairo, box->x + width - radius, box->y + height - half);
            } else {
                cairo_move_to(cairo, box->x + width - half, box->y + height - radius);
                cairo_arc(cairo, box->x + width - radius, box->y + height - radius, radius - half,
                          ANGLE(0), ANGLE(90));
            }
        } else {
            cairo_move_to(cairo, box->x + width - half, box->y + height - half);
        }
        if (info->corner_mask & CORNER_MASK_BOTTOM_LEFT) {
            cairo_line_to(cairo, box->x + radius, box->y + height - half);
            cairo_arc(cairo, box->x + radius, box->y + height - radius, radius - half, ANGLE(90),
                      ANGLE(180));
        } else {
            cairo_line_to(cairo, box->x + half, box->y + height - half);
        }
        cairo_stroke(cairo);
    }

    if (info->border_mask & BORDER_MASK_LEFT) {
        if (info->corner_mask & CORNER_MASK_BOTTOM_LEFT) {
            if (info->border_mask & BORDER_MASK_BOTTOM) {
                cairo_move_to(cairo, box->x + half, box->y + height - radius);
            } else {
                cairo_move_to(cairo, box->x + radius, box->y + height - half);
                cairo_arc(cairo, box->x + radius, box->y + height - radius, radius - half,
                          ANGLE(90), ANGLE(180));
            }
        } else {
            cairo_move_to(cairo, box->x + half, box->y + height - half);
        }
        if (info->corner_mask & CORNER_MASK_TOP_LEFT) {
            if (info->border_mask & BORDER_MASK_TOP) {
                cairo_line_to(cairo, box->x + half, box->y + radius);
            } else {
                cairo_line_to(cairo, box->x + half, box->y + radius);
                cairo_arc(cairo, box->x + radius, box->y + radius, radius - half, ANGLE(-180),
                          ANGLE(-90));
            }
        } else {
            cairo_line_to(cairo, box->x + half, box->y + half);
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
    if (info->hover_rgba) {
        int height = info->height / 2;
        buffer_draw(buffer, info, &(struct kywc_box){ 0, 0, info->width, height }, false);
        buffer_draw(buffer, info, &(struct kywc_box){ 0, height, info->width, height }, true);
    } else {
        buffer_draw(buffer, info, &(struct kywc_box){ 0, 0, info->width, info->height }, false);
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

    return true;
}

struct wlr_buffer *painter_draw_buffer(struct draw_info *info)
{
    /* auto resize to text size */
    if (info->auto_resize && info->text && *info->text) {
        int width, height;
        text_extents(info->font, info->font_size, info->text, &width, &height);
        if (info->auto_resize == AUTO_RESIZE_EXTEND) {
            height += height / 2 + 4;
            width += height / 2;
            info->align = TEXT_ALIGN_CENTER;
        }
        if (width < info->width) {
            info->width = width;
        }
        if (height < info->height) {
            info->height = height;
        }
        info->text_attr &= ~(TEXT_ATTR_SUBMENU | TEXT_ATTR_CHECKED);
    }

    /* draw hover together */
    if (info->hover_rgba || info->hover_svg) {
        info->height *= 2;
    }

    struct cairo_buffer *buffer = NULL;
    if (info->image) {
        buffer = cairo_buffer_create_from_file(info->width, info->height, info->scale, info->image);
    } else if (info->pixel.data) {
        buffer = cairo_buffer_create_from_pixel(info->width, info->height, info->pixel.width,
                                                info->pixel.height, info->pixel.data);
    } else {
        buffer = cairo_buffer_create(info->width, info->height, info->scale);
    }
    if (!buffer) {
        return NULL;
    }

    if (!painter_draw(buffer, info, false)) {
        wlr_buffer_drop(&buffer->base);
        return NULL;
    }

    return &buffer->base;
}

bool painter_buffer_redraw(struct wlr_buffer *buffer, struct draw_info *info)
{
    struct cairo_buffer *buf = cairo_buffer_from_wlr_buffer(buffer);
    if (!buf) {
        return false;
    }

    /* fix size to buffer unscaled size */
    info->width = buf->dst_width;
    info->height = buf->dst_height;
    return painter_draw(buf, info, true);
}

void painter_buffer_get_dest_size(struct wlr_buffer *buffer, int *width, int *height)
{
    struct cairo_buffer *buf = cairo_buffer_from_wlr_buffer(buffer);
    if (buf && width) {
        *width = buf->dst_width;
    }
    if (buf && height) {
        *height = buf->dst_height;
    }
}

void painter_buffer_write_to_file(struct wlr_buffer *buffer, const char *name)
{
    struct cairo_buffer *buf = cairo_buffer_from_wlr_buffer(buffer);
    if (!buf) {
        return;
    }

    size_t len = strlen(name);
    const char *suffix = name + len - 3;
    if (len > 3 && strncmp(suffix, "bmp", 3) == 0) {
        cairo_buffer_write_to_bmp(buf, name);
    } else {
        cairo_surface_write_to_png(buf->surface, name);
    }
}

struct wlr_buffer *painter_create_buffer(int width, int height, float scale)
{
    struct cairo_buffer *buffer = cairo_buffer_create(width, height, scale);
    if (!buffer) {
        return NULL;
    }

    return &buffer->base;
}

void painter_get_text_size(const char *text, const char *font, int font_size, int *width,
                           int *height)
{
    text_extents(font, font_size, text, width, height);
}
