// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <cairo.h>

#include "render/opengl.h"
#include "render/pass.h"

#define ANGLE(ang) (ang * 3.1415926 / 180.0)

static bool ky_render_pass_create_rounded_region(pixman_region32_t *region,
                                                 const struct wlr_box *box,
                                                 const struct ky_render_round_corner *radius)
{
    int width = box->width, height = box->height;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_A1, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return false;
    }

    cairo_t *cairo = cairo_create(surface);

    if (radius->lt > 0) {
        cairo_arc(cairo, radius->lt, radius->lt, radius->lt, ANGLE(-180), ANGLE(-90));
    } else {
        cairo_line_to(cairo, 0, 0);
    }
    if (radius->rt > 0) {
        cairo_arc(cairo, width - radius->rt, radius->rt, radius->rt, ANGLE(-90), ANGLE(0));
    } else {
        cairo_line_to(cairo, width, 0);
    }
    if (radius->rb > 0) {
        cairo_arc(cairo, width - radius->rb, height - radius->rb, radius->rb, ANGLE(0), ANGLE(90));
    } else {
        cairo_line_to(cairo, width, height);
    }
    if (radius->lb > 0) {
        cairo_arc(cairo, radius->lb, height - radius->lb, radius->lb, ANGLE(90), ANGLE(180));
    } else {
        cairo_line_to(cairo, 0, height);
    }

    cairo_close_path(cairo);
    cairo_set_source_rgba(cairo, 0, 0, 0, 1);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_fill(cairo);
    cairo_surface_flush(surface);

    void *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    pixman_image_t *image =
        pixman_image_create_bits_no_clear(PIXMAN_a1, width, height, data, stride);
    if (!image) {
        cairo_destroy(cairo);
        cairo_surface_destroy(surface);
        return false;
    }

    pixman_region32_init_from_image(region, image);
    pixman_region32_translate(region, box->x, box->y);

    pixman_image_unref(image);
    cairo_destroy(cairo);
    cairo_surface_destroy(surface);

    return true;
}

bool ky_render_pass_options_has_radius(const struct ky_render_round_corner *radius)
{
    return radius->lb > 0 || radius->lt > 0 || radius->rb > 0 || radius->rt > 0;
}

bool ky_render_pass_submit(struct wlr_render_pass *render_pass, uint32_t quirks)
{
    if (wlr_render_pass_is_opengl(render_pass)) {
        struct ky_opengl_render_pass *opengl_pass =
            ky_opengl_render_pass_from_wlr_render_pass(render_pass);
        if (opengl_pass->impl && opengl_pass->impl->submit) {
            return opengl_pass->impl->submit(render_pass, quirks);
        }
    }

    return wlr_render_pass_submit(render_pass);
}

void ky_render_pass_add_texture(struct wlr_render_pass *render_pass,
                                const struct ky_render_texture_options *options)
{
    if (wlr_render_pass_is_opengl(render_pass)) {
        struct ky_opengl_render_pass *opengl_pass =
            ky_opengl_render_pass_from_wlr_render_pass(render_pass);
        if (opengl_pass->impl && opengl_pass->impl->add_texture) {
            opengl_pass->impl->add_texture(render_pass, options);
            return;
        }
    }

    wlr_render_pass_add_texture(render_pass, &options->base);
}

void ky_render_pass_add_rect(struct wlr_render_pass *render_pass,
                             const struct ky_render_rect_options *options)
{
    if (wlr_render_pass_is_opengl(render_pass)) {
        struct ky_opengl_render_pass *opengl_pass =
            ky_opengl_render_pass_from_wlr_render_pass(render_pass);
        if (opengl_pass->impl && opengl_pass->impl->add_rect) {
            opengl_pass->impl->add_rect(render_pass, options);
            return;
        }
    } else if (!options->base.clip && ky_render_pass_options_has_radius(&options->radius)) {
        pixman_region32_t region;
        if (ky_render_pass_create_rounded_region(&region, &options->base.box, &options->radius)) {
            struct wlr_render_rect_options base = options->base;
            base.clip = &region;
            wlr_render_pass_add_rect(render_pass, &base);
            pixman_region32_fini(&region);
            return;
        }
    }

    wlr_render_pass_add_rect(render_pass, &options->base);
}

struct wlr_renderer *ky_render_pass_get_renderer(struct wlr_render_pass *render_pass)
{
    if (wlr_render_pass_is_opengl(render_pass)) {
        struct ky_opengl_render_pass *opengl_pass =
            ky_opengl_render_pass_from_wlr_render_pass(render_pass);
        return &opengl_pass->renderer->wlr_renderer;
    }

    return NULL;
}
