#include <ctype.h>
#include <librsvg/rsvg.h>

#include "painter_p.h"

// SVG signature
static const uint8_t signature[] = { '<' };

bool cairo_buffer_draw_svg(struct cairo_buffer *buffer, const char *data)
{
    RsvgRectangle viewport = {
        .x = 0,
        .y = 0,
        .width = buffer->width,
        .height = buffer->height,
    };

    size_t size = strlen(data);
    // check signature, this an xml, so skip spaces from the start
    while (size && isspace(*data) != 0) {
        ++data;
        --size;
    }
    if (size < sizeof(signature) || memcmp(data, signature, sizeof(signature))) {
        return false;
    }

    GError *err = NULL;
    RsvgHandle *svg = NULL;
    svg = rsvg_handle_new_from_data((guint8 *)data, size, &err);
    if (!svg) {
        kywc_log(KYWC_ERROR, "Invalid SVG format");
        if (err && err->message) {
            kywc_log(KYWC_ERROR, "%s: %s", data, err->message);
        }
        return false;
    }

    // render svg to surface
    cairo_t *cairo = buffer->cairo;
    if (!rsvg_handle_render_document(svg, cairo, &viewport, &err)) {
        kywc_log(KYWC_ERROR, "Invalid SVG format");
        if (err && err->message) {
            kywc_log(KYWC_ERROR, ": %s", err->message);
        }
        g_object_unref(svg);
        return false;
    }

    return true;
}
