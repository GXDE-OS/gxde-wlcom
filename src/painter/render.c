// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <ctype.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wexpansion-to-defined"
#include <librsvg/rsvg.h>
#pragma GCC diagnostic pop

#include <kywc/boxes.h>

#include "painter_p.h"

#define PI 3.1415926
#define ANGLE(ang) (ang * PI / 180.0)
#define FONT_WEIGHT (400)

void text_get_size(const char *font, int font_size, const char *text, int *width, int *height)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cairo = cairo_create(surface);

    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, font);
    pango_font_description_set_size(desc, font_size * PANGO_SCALE);
    pango_font_description_set_weight(desc, FONT_WEIGHT);

    PangoLayout *layout = pango_cairo_create_layout(cairo);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_width(layout, -1);
    pango_layout_get_pixel_size(layout, width, height);

    g_object_unref(layout);
    pango_font_description_free(desc);
    cairo_destroy(cairo);
    cairo_surface_destroy(surface);
}

static bool draw_text(cairo_surface_t *surface, cairo_t *cairo, struct draw_info *info,
                      struct kywc_fbox *box, const float *font_rgba)
{
    if (!info->text || !*info->text) {
        return false;
    }

    int width, height;
    text_get_size(info->font, info->font_size, "fg", &width, &height);
    double ly = (box->height - height) / 2;
    ly = ly < 0 ? 0 : ly;
    double lx = info->auto_resize ? 0 : 4 * ly;

    cairo_set_source_rgba(cairo, font_rgba[0], font_rgba[1], font_rgba[2], font_rgba[3]);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);

    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, info->font);
    pango_font_description_set_size(desc, info->font_size * PANGO_SCALE);
    pango_font_description_set_weight(desc, FONT_WEIGHT);
    if (info->text_attr & TEXT_ATTR_SLANT) {
        pango_font_description_set_style(desc, PANGO_STYLE_ITALIC);
    }

    if (info->text_attr & TEXT_ATTR_SUBMENU) {
        PangoLayout *layout = pango_cairo_create_layout(cairo);
        pango_layout_set_width(layout, -1);
        if (info->layout_is_right_to_left) {
            cairo_move_to(cairo, box->x + 2 * ly, box->y + ly);
            pango_layout_set_text(layout, "<", -1);
        } else {
            cairo_move_to(cairo, box->width - width * 2, box->y + ly);
            pango_layout_set_text(layout, ">", -1);
        }
        pango_layout_set_font_description(layout, desc);
        pango_cairo_update_layout(cairo, layout);
        pango_cairo_show_layout(cairo, layout);
        g_object_unref(layout);
    }

    if (info->text_attr & TEXT_ATTR_CHECKED) {
        int x = info->layout_is_right_to_left ? (box->width - lx + ly) : (box->x + 2 * ly);
        cairo_move_to(cairo, x, box->y + ly);
        PangoLayout *layout = pango_cairo_create_layout(cairo);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, "✓", -1);
        pango_layout_set_font_description(layout, desc);
        pango_cairo_update_layout(cairo, layout);
        pango_cairo_show_layout(cairo, layout);
        g_object_unref(layout);
    }

    int shortcut_width = 0;
    if (info->text_attr & TEXT_ATTR_SHORTCUT) {
        PangoLayout *layout = pango_cairo_create_layout(cairo);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, info->shortcut, -1);
        pango_layout_set_font_description(layout, desc);
        pango_layout_get_pixel_size(layout, &shortcut_width, NULL);
        int x = info->layout_is_right_to_left ? (box->x + 2 * ly)
                                              : (box->width - shortcut_width - width);
        cairo_move_to(cairo, x, box->y + ly);
        pango_cairo_update_layout(cairo, layout);
        pango_cairo_show_layout(cairo, layout);
        g_object_unref(layout);
    }

    PangoLayout *layout = pango_cairo_create_layout(cairo);
    int left = box->width - lx;
    left -= (info->text_attr & TEXT_ATTR_SUBMENU) ? width * 2 : 0;

    if (info->layout_is_right_to_left) {
        left -= shortcut_width;
        cairo_move_to(cairo, box->width - left, box->y + ly);
        pango_layout_set_width(layout, (left - lx) * PANGO_SCALE);
    } else {
        cairo_move_to(cairo, box->x + lx, box->y + ly);
        pango_layout_set_width(layout, left * PANGO_SCALE);
    }

    if (info->text_attr & TEXT_ATTR_ACCEL) {
        pango_layout_set_markup_with_accel(layout, info->text, -1, '_', NULL);
    } else {
        pango_layout_set_text(layout, info->text, -1);
    }

    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
    pango_layout_set_alignment(layout, (PangoAlignment)info->align);

    pango_layout_set_font_description(layout, desc);
    pango_cairo_update_layout(cairo, layout);
    pango_cairo_show_layout(cairo, layout);
    g_object_unref(layout);
    pango_font_description_free(desc);

    return true;
}

static void draw_color(cairo_surface_t *surface, cairo_t *cairo, struct draw_info *info,
                       struct kywc_fbox *box, bool hover)
{
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
        /* edge-to-edge highlight, GXDE KWin style */
        double offset = 0;
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
}

static bool draw_svg(cairo_t *cairo, const char *data, struct kywc_fbox *box)
{
    size_t size = strlen(data);
    // check signature, this an xml, so skip spaces from the start
    while (size && isspace(*data) != 0) {
        ++data;
        --size;
    }

    const uint8_t signature[] = { '<' };
    if (size <= sizeof(signature) || memcmp(data, signature, sizeof(signature))) {
        return false;
    }

    GError *err = NULL;
    RsvgHandle *svg = rsvg_handle_new_from_data((guint8 *)data, size, &err);
    if (!svg) {
        kywc_log(KYWC_ERROR, "Invalid SVG format");
        if (err && err->message) {
            kywc_log(KYWC_ERROR, "%s: %s", data, err->message);
        }
        return false;
    }

    RsvgRectangle viewport = {
        .x = box->x, .y = box->y, .width = box->width, .height = box->height
    };

    // render svg to surface
    gboolean ok = rsvg_handle_render_document(svg, cairo, &viewport, &err);
    g_object_unref(svg);
    if (!ok && err && err->message) {
        kywc_log(KYWC_ERROR, "%s", err->message);
    }
    return ok;
}

bool render_buffer(struct painter_buffer *buffer, struct draw_info *info)
{
    if (buffer->own_data && !info->image) {
        memset(buffer->data, 0x0, buffer->base.height * buffer->stride);
    }

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        buffer->data, CAIRO_FORMAT_ARGB32, buffer->base.width, buffer->base.height, buffer->stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return false;
    }

    cairo_surface_set_device_scale(surface, buffer->scale, buffer->scale);
    cairo_t *cairo = cairo_create(surface);

    double half = info->height / 2.0;
    struct kywc_fbox upper = { 0, 0, info->width, half };
    struct kywc_fbox lower = { 0, half, info->width, half };
    struct kywc_fbox whole = { 0, 0, info->width, info->height };

    /* svg picture */
    if (info->svg) {
        if (info->hover_svg) {
            draw_svg(cairo, info->svg, &upper);
            draw_svg(cairo, info->hover_svg, &lower);
        } else {
            draw_svg(cairo, info->svg, &whole);
        }
    }

    /* corner, solid and border */
    if (info->hover_rgba) {
        draw_color(surface, cairo, info, &upper, false);
        draw_color(surface, cairo, info, &lower, true);
    } else {
        draw_color(surface, cairo, info, &whole, false);
    }

    /* text */
    if (info->text && *info->text) {
        if (info->hover_rgba) {
            const float *hover_font_rgba = info->hover_font_rgba ? info->hover_font_rgba
                : info->font_rgba;
            draw_text(surface, cairo, info, &upper, info->font_rgba);
            draw_text(surface, cairo, info, &lower, hover_font_rgba);
        } else {
            draw_text(surface, cairo, info, &whole, info->font_rgba);
        }
    }

    cairo_surface_flush(surface);
    cairo_destroy(cairo);
    cairo_surface_destroy(surface);

    return true;
}
