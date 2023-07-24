#include <pango/pangocairo.h>

#include "painter_p.h"

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

bool cairo_buffer_draw_text(struct cairo_buffer *buffer, const char *text, const char *font,
                            int font_size, float *font_color, int align, bool submenu)
{
    if (!text || !*text) {
        return false;
    }

    cairo_t *cairo = buffer->cairo;
    cairo_surface_t *surf = buffer->surface;

    int width, height;
    text_extents(font, font_size, "fg", &width, &height);
    double y = (double)(buffer->height - height) / 2;

    cairo_set_source_rgba(cairo, font_color[0], font_color[1], font_color[2], font_color[3]);
    cairo_move_to(cairo, 0, y < 0 ? 0 : y);

    PangoLayout *layout = pango_cairo_create_layout(cairo);
    pango_layout_set_width(layout, (buffer->width - (submenu ? width * 2 : 0)) * PANGO_SCALE);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, align);

    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, font);
    pango_font_description_set_size(desc, font_size * PANGO_SCALE);

    pango_layout_set_font_description(layout, desc);
    pango_cairo_update_layout(cairo, layout);
    pango_cairo_show_layout(cairo, layout);
    g_object_unref(layout);

    if (submenu) {
        cairo_move_to(cairo, buffer->width - width, y);
        PangoLayout *layout = pango_cairo_create_layout(cairo);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, ">", -1);
        pango_layout_set_font_description(layout, desc);
        pango_cairo_update_layout(cairo, layout);
        pango_cairo_show_layout(cairo, layout);
        g_object_unref(layout);
    }

    pango_font_description_free(desc);
    cairo_surface_flush(surf);
    return true;
}
