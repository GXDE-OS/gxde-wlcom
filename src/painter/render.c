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

/* the DTK check icon used by GXDE KWin for checked popup menu items
   (themes/popupmenu/inActive.svg), 14x12 viewBox, filled with the text color */
static void draw_check(cairo_t *cairo, double x, double y)
{
    cairo_save(cairo);
    cairo_translate(cairo, x, y);
    cairo_move_to(cairo, 11.9161746, 0.383335474);
    cairo_curve_to(cairo, 12.2567731, -0.0514186317, 12.8853206, -0.127746861, 13.3200747,
                   0.212851602);
    cairo_curve_to(cairo, 13.7213861, 0.527250183, 13.8172933, 1.08700174, 13.5617837,
                   1.5132029);
    cairo_line_to(cairo, 13.4905585, 1.61675162);
    cairo_line_to(cairo, 5.65628029, 11.6167516);
    cairo_curve_to(cairo, 5.30422001, 12.0661361, 4.65596945, 12.124889, 4.22971115, 11.7691292);
    cairo_line_to(cairo, 4.14195676, 11.6865417);
    cairo_line_to(cairo, 0.272871003, 7.58844671);
    cairo_curve_to(cairo, -0.106271436, 7.18686302, -0.0880792126, 6.55395945, 0.313504472,
                   6.17481701);
    cairo_curve_to(cairo, 0.684197104, 5.82483938, 1.25199071, 5.81341909, 1.63535592,
                   6.12958944);
    cairo_line_to(cairo, 1.72713417, 6.21545048);
    cairo_line_to(cairo, 4.79800259, 9.46804354);
    cairo_line_to(cairo, 11.9161746, 0.383335474);
    cairo_close_path(cairo);
    cairo_fill(cairo);
    cairo_restore(cairo);
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
    double lx;
    if (info->auto_resize) {
        lx = 0;
    } else if (info->text_padding_left) {
        lx = info->text_padding_left;
    } else {
        lx = 4 * ly;
    }

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
            /* submenu arrow right-aligned with the 22px item padding */
            cairo_move_to(cairo, box->width - width - 22, box->y + ly);
            pango_layout_set_text(layout, ">", -1);
        }
        pango_layout_set_font_description(layout, desc);
        pango_cairo_update_layout(cairo, layout);
        pango_cairo_show_layout(cairo, layout);
        g_object_unref(layout);
    }

    if (info->text_attr & TEXT_ATTR_CHECKED) {
        /* DTK check icon at the start of the left padding zone */
        double x = info->layout_is_right_to_left ? (box->width - ly - 14) : (box->x + ly);
        draw_check(cairo, x, box->y + ly);
    }

    int shortcut_width = 0;
    if (info->text_attr & TEXT_ATTR_SHORTCUT) {
        PangoLayout *layout = pango_cairo_create_layout(cairo);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, info->shortcut, -1);
        pango_layout_set_font_description(layout, desc);
        pango_layout_get_pixel_size(layout, &shortcut_width, NULL);
        int x = info->layout_is_right_to_left ? (box->x + 2 * ly)
                                              : (box->width - shortcut_width - 22);
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
        /* DTK6 style: the highlight follows the menu content padding
           horizontally and is inset 2px from the item edges vertically */
        double offset_x = info->hover_inset;
        double offset_y = 2;
        double x = box->x + offset_x;
        double y = box->y + offset_y;
        double w = box->width - 2 * offset_x;
        double h = box->height - 2 * offset_y;
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
