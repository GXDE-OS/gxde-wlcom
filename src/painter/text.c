// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <pango/pangocairo.h>

#include "painter_p.h"

#define FONT_WEIGHT (400)

void text_extents(const char *font, int font_size, const char *text, int *width, int *height)
{
    PangoRectangle rect;
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoLayout *layout;
    PangoFontDescription *desc;

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cr = cairo_create(surface);
    layout = pango_cairo_create_layout(cr);
    desc = pango_font_description_new();
    pango_font_description_set_family(desc, font);
    pango_font_description_set_size(desc, font_size * PANGO_SCALE);
    pango_font_description_set_weight(desc, FONT_WEIGHT);

    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
    pango_layout_get_extents(layout, NULL, &rect);
    pango_extents_to_pixels(&rect, NULL);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    pango_font_description_free(desc);
    g_object_unref(layout);

    if (width) {
        *width = rect.width;
    }
    if (height) {
        *height = rect.height;
    }
}

bool cairo_buffer_draw_text(struct cairo_buffer *buffer, struct draw_info *info,
                            struct kywc_box *box)
{
    if (!info->text || !*info->text) {
        return false;
    }

    cairo_t *cairo = buffer->cairo;
    cairo_surface_t *surf = buffer->surface;

    int width, height;
    text_extents(info->font, info->font_size, "fg", &width, &height);
    double ly = (double)(box->height - height) / 2;
    ly = ly < 0 ? 0 : ly;
    double lx = info->auto_resize ? 0 : 4 * ly;

    cairo_set_source_rgba(cairo, info->font_rgba[0], info->font_rgba[1], info->font_rgba[2],
                          info->font_rgba[3]);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_move_to(cairo, box->x + lx, box->y + ly);

    PangoLayout *layout = pango_cairo_create_layout(cairo);
    int left = box->width - lx - (info->submenu ? width * 2 : 0);
    pango_layout_set_width(layout, left * PANGO_SCALE);
    pango_layout_set_text(layout, info->text, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
    pango_layout_set_alignment(layout, (PangoAlignment)info->align);

    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, info->font);
    pango_font_description_set_size(desc, info->font_size * PANGO_SCALE);
    pango_font_description_set_weight(desc, FONT_WEIGHT);
    if (info->slant) {
        pango_font_description_set_style(desc, PANGO_STYLE_ITALIC);
    }

    pango_layout_set_font_description(layout, desc);
    pango_cairo_update_layout(cairo, layout);
    pango_cairo_show_layout(cairo, layout);
    g_object_unref(layout);

    if (info->submenu) {
        cairo_move_to(cairo, box->width - width * 2, box->y + ly);
        PangoLayout *layout = pango_cairo_create_layout(cairo);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, ">", -1);
        pango_layout_set_font_description(layout, desc);
        pango_cairo_update_layout(cairo, layout);
        pango_cairo_show_layout(cairo, layout);
        g_object_unref(layout);
    }
    if (info->checked) {
        cairo_move_to(cairo, box->x + 2 * ly, box->y + ly);
        PangoLayout *layout = pango_cairo_create_layout(cairo);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, "✓", -1);
        pango_layout_set_font_description(layout, desc);
        pango_cairo_update_layout(cairo, layout);
        pango_cairo_show_layout(cairo, layout);
        g_object_unref(layout);
    }

    pango_font_description_free(desc);
    cairo_surface_flush(surf);
    return true;
}
