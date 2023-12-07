// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_buffer.h>

#include "painter.h"
#include "widget/scaled_buffer.h"
#include "widget_p.h"

// TODO: support buffers for different scales ?

#define color_valid(color) (color[3] != 0.0f)

static struct wlr_buffer *widget_paint_buffer(struct widget *widget, float scale, bool redraw_only)
{
    struct draw_info info = {
        .width = widget->auto_resize ? widget->max_width : widget->width,
        .height = widget->auto_resize ? widget->max_height : widget->height,
        .scale = scale,

        .solid_rgba = color_valid(widget->background_color) ? widget->background_color : NULL,
        .hover_rgba = color_valid(widget->hovered_color) ? widget->hovered_color : NULL,

        .border_rgba = color_valid(widget->border_color) ? widget->border_color : NULL,
        .border_mask = widget->border_mask,
        .border_width = widget->border_width,

        .corner_mask = widget->corner_mask,
        .corner_radius = widget->corner_radius,

        .text = widget->text,
        .font_rgba = color_valid(widget->front_color) ? widget->front_color : NULL,
        .font = widget->font_name,
        .font_size = widget->font_size,
        .align = widget->text_align,
        .submenu = widget->submenu,
        .checked = widget->checked,
        .auto_resize = widget->auto_resize,

        .svg = widget->svg,
        .hover_svg = widget->hover_svg,
    };

    struct wlr_buffer *buffer = ky_scene_buffer_get_buffer(widget->content.buffer);
    if (buffer && redraw_only) {
        painter_redraw_buffer(buffer, &info);
    } else {
        buffer = painter_draw_buffer(&info);
        if (!buffer) {
            return NULL;
        }
    }

    if (widget->auto_resize) {
        int width;
        painter_buffer_unscaled_size(buffer, &width, NULL);
        widget->text_truncated = width == widget->max_width;
    }

    return buffer;
}

static void widget_buffer_get_size(struct widget *widget, struct wlr_buffer *buffer,
                                   struct wlr_fbox *src, int *width, int *height)
{
    int w, h, scaled_w, scaled_h;
    painter_buffer_size(buffer, &scaled_w, &scaled_h);
    painter_buffer_unscaled_size(buffer, &w, &h);

    src->x = 0;
    src->y = widget->hovered ? scaled_h / 2 : 0;
    src->width = scaled_w;
    src->height = widget->hoverable ? scaled_h / 2 : scaled_h;

    *width = w;
    *height = widget->hoverable ? h / 2 : h;
}

static void widget_set_buffer(struct widget *widget, struct wlr_buffer *buffer, bool redraw_only)
{
    struct ky_scene_buffer *scene_buffer = widget->content.buffer;

    if (!buffer) {
        ky_scene_buffer_set_dest_size(scene_buffer, widget->width, widget->height);
        return;
    }

    struct wlr_buffer *old_buffer = ky_scene_buffer_get_buffer(scene_buffer);
    if (old_buffer != buffer) {
        wlr_buffer_drop(old_buffer);
    }
    if (old_buffer != buffer || redraw_only) {
        ky_scene_buffer_set_buffer(scene_buffer, buffer);
    }
    /* shortcut here if set_buffer triggered scaled buffer update */
    if (ky_scene_buffer_get_buffer(scene_buffer) != buffer) {
        return;
    }

    struct wlr_fbox src;
    int width, height;
    widget_buffer_get_size(widget, buffer, &src, &width, &height);
    ky_scene_buffer_set_source_box(scene_buffer, &src);
    ky_scene_buffer_set_dest_size(scene_buffer, width, height);
}

static void widget_do_update(struct widget *widget)
{
    if (widget->pending_cause & WIDGET_UPDATE_CAUSE_ENABLED) {
        widget->pending_cause &= ~WIDGET_UPDATE_CAUSE_ENABLED;
        ky_scene_node_set_enabled(widget->content.node, widget->enabled);
    }

    /* skip update widget if widget is disabled */
    if (!widget->enabled) {
        return;
    }

    struct wlr_buffer *buffer = ky_scene_buffer_get_buffer(widget->content.buffer);
    /* draw it in scaled buffer update at the first time */
    if (widget->scale == 0.0 || !buffer) {
        widget->pending_cause = WIDGET_UPDATE_CAUSE_NONE;
        widget_set_buffer(widget, NULL, false);
        return;
    }

    /* we need paint a new buffer if content and scale changed */
    if (widget->pending_cause & (WIDGET_UPDATE_CAUSE_CONTENT | WIDGET_UPDATE_CAUSE_SCALE)) {
        /* only redraw content if size or scale not changed */
        bool redraw_only =
            !widget->auto_resize &&
            !(widget->pending_cause & (WIDGET_UPDATE_CAUSE_SIZE | WIDGET_UPDATE_CAUSE_SCALE));
        struct wlr_buffer *buf = widget_paint_buffer(widget, widget->scale, redraw_only);
        if (buf) {
            widget_set_buffer(widget, buf, redraw_only);
        }
        widget->pending_cause = WIDGET_UPDATE_CAUSE_NONE;
        return;
    }

    if (widget->pending_cause & WIDGET_UPDATE_CAUSE_SIZE) {
        int width;
        painter_buffer_unscaled_size(buffer, &width, NULL);
        /* skip paint buffer if text not truncated when auto-resized */
        if (widget->auto_resize && !widget->text_truncated && width <= widget->max_width) {
            widget->pending_cause &= ~WIDGET_UPDATE_CAUSE_SIZE;
        } else {
            struct wlr_buffer *buf = widget_paint_buffer(widget, widget->scale, false);
            if (buf) {
                widget_set_buffer(widget, buf, false);
            }
            widget->pending_cause = WIDGET_UPDATE_CAUSE_NONE;
            return;
        }
    }

    /* hovered changed only */
    if (widget->pending_cause & WIDGET_UPDATE_CAUSE_HOVERED) {
        widget->pending_cause = WIDGET_UPDATE_CAUSE_NONE;
        struct wlr_fbox src;
        int width, height;
        widget_buffer_get_size(widget, buffer, &src, &width, &height);
        ky_scene_buffer_set_source_box(widget->content.buffer, &src);
        ky_scene_buffer_set_dest_size(widget->content.buffer, width, height);
        return;
    }
}

void widget_update(struct widget *widget, bool immediately)
{
    if (widget->pending_cause == WIDGET_UPDATE_CAUSE_NONE ||
        (!widget->enabled && !(widget->pending_cause & WIDGET_UPDATE_CAUSE_ENABLED))) {
        return;
    }

    if (!immediately) {
        // TODO: painter buffer when output frame
        return;
    }

    widget_do_update(widget);
}

void widget_set_round_coner(struct widget *widget, uint32_t mask, float radius)
{
    if (widget->corner_mask == mask && widget->corner_radius == radius) {
        return;
    }

    widget->corner_mask = mask;
    widget->corner_radius = radius;
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
}

void widget_set_border(struct widget *widget, const float color[static 4], uint32_t mask,
                       float width)
{
    if (memcmp(widget->border_color, color, sizeof(widget->border_color))) {
        memcpy(widget->border_color, color, sizeof(widget->border_color));
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
    }
    if (widget->border_mask != mask) {
        widget->border_mask = mask;
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
    }
    if (widget->border_width != width) {
        widget->border_width = width;
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
    }
}

void widget_set_backgrond_color(struct widget *widget, const float color[static 4])
{
    if (memcmp(widget->background_color, color, sizeof(widget->background_color)) == 0) {
        return;
    }

    memcpy(widget->background_color, color, sizeof(widget->background_color));
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
}

void widget_set_front_color(struct widget *widget, const float color[static 4])
{
    if (memcmp(widget->front_color, color, sizeof(widget->front_color)) == 0) {
        return;
    }

    memcpy(widget->front_color, color, sizeof(widget->front_color));
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
}

void widget_set_hovered_color(struct widget *widget, const float color[static 4])
{
    if (memcmp(widget->hovered_color, color, sizeof(widget->hovered_color)) == 0) {
        return;
    }

    memcpy(widget->hovered_color, color, sizeof(widget->hovered_color));
    widget->hoverable = color_valid(widget->hovered_color);
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
}

void widget_set_auto_resize(struct widget *widget, int auto_resize)
{
    if (widget->auto_resize == auto_resize) {
        return;
    }

    widget->auto_resize = auto_resize;
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_SIZE;
}

void widget_set_size(struct widget *widget, int width, int height)
{
    if (widget->max_width != 0 && width > widget->max_width) {
        width = widget->max_width;
    }
    if (widget->max_height != 0 && height > widget->max_height) {
        height = widget->max_height;
    }

    if (widget->width == width && widget->height == height) {
        return;
    }

    widget->width = width;
    widget->height = height;
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_SIZE;
}

void widget_set_max_size(struct widget *widget, int width, int height)
{
    if (widget->max_width == width && widget->max_height == height) {
        return;
    }

    widget->max_width = width;
    widget->max_height = height;
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_SIZE;

    if (widget->width > widget->max_width) {
        widget->width = widget->max_width;
    }
    if (widget->height > widget->max_height) {
        widget->height = widget->max_height;
    }
}

void widget_set_enabled(struct widget *widget, bool enabled)
{
    if (widget->enabled == enabled) {
        return;
    }

    widget->enabled = enabled;
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_ENABLED;
}

void widget_set_hovered(struct widget *widget, bool hovered)
{
    if (!widget->hoverable || widget->hovered == hovered) {
        return;
    }

    widget->hovered = hovered;
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_HOVERED;
}

void widget_set_image(struct widget *widget, const char *svg, const char *hover_svg)
{
    bool same_svg = widget->svg && svg && strcmp(widget->svg, svg) == 0;
    bool same_hover_svg =
        widget->hover_svg && hover_svg && strcmp(widget->hover_svg, hover_svg) == 0;

    if (!same_svg) {
        free((void *)widget->svg);
        widget->svg = svg ? strdup(svg) : NULL;
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
    }

    if (!same_hover_svg) {
        free((void *)widget->hover_svg);
        widget->hover_svg = hover_svg ? strdup(hover_svg) : NULL;
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
    }

    widget->hoverable = hover_svg != NULL;
}

void widget_set_font(struct widget *widget, const char *name, int size)
{
    if (widget->font_size != size) {
        widget->font_size = size;
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
    }

    if (widget->font_name && name && strcmp(widget->font_name, name) == 0) {
        return;
    }

    free((void *)widget->font_name);
    widget->font_name = name ? strdup(name) : NULL;
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
}

void widget_set_text(struct widget *widget, const char *text, int align, bool submenu, bool checked)
{
    if (widget->submenu != submenu) {
        widget->submenu = submenu;
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
    }

    if (widget->checked != checked) {
        widget->checked = checked;
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
    }

    if (widget->text_align != align) {
        widget->text_align = align;
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
    }

    if (widget->text && text && strcmp(widget->text, text) == 0) {
        return;
    }

    free((void *)widget->text);
    widget->text = text ? strdup(text) : NULL;
    widget->pending_cause |= WIDGET_UPDATE_CAUSE_CONTENT;
}

void widget_destroy(struct widget *widget)
{
    /* widget_destroy_buffer is called */
    ky_scene_node_destroy(widget->content.node);
}

/* called when widget scene node destroyed */
static void widget_destroy_buffer(struct ky_scene_buffer *buffer, void *data)
{
    struct widget *widget = data;
    wlr_buffer_drop(ky_scene_buffer_get_buffer(buffer));
    free((void *)widget->text);
    free((void *)widget->svg);
    free((void *)widget->hover_svg);
    free((void *)widget->font_name);
    free(widget);
}

static void widget_update_buffer(struct ky_scene_buffer *buffer, float scale, void *data)
{
    struct widget *widget = data;
    widget->scale = scale;

    if (!widget->enabled) {
        widget->pending_cause |= WIDGET_UPDATE_CAUSE_SCALE;
        return;
    }

    struct wlr_buffer *buf = widget_paint_buffer(widget, scale, false);
    if (buf) {
        widget_set_buffer(widget, buf, false);
    }
}

struct ky_scene_node *ky_scene_node_from_widget(struct widget *widget)
{
    return widget->content.node;
}

struct widget *widget_create(struct ky_scene_tree *parent)
{
    struct widget *widget = calloc(1, sizeof(struct widget));
    if (!widget) {
        return NULL;
    }

    widget->scale = 0.0;
    widget->content.buffer = scaled_buffer_create(parent, widget->scale, widget_update_buffer,
                                                  widget_destroy_buffer, widget);
    widget->content.node = ky_scene_node_from_buffer(widget->content.buffer);
    ky_scene_node_set_enabled(widget->content.node, false);

    return widget;
}

void widget_get_size(struct widget *widget, int *width, int *height)
{
    *width = 0;
    *height = 0;

    struct wlr_buffer *buffer = ky_scene_buffer_get_buffer(widget->content.buffer);
    if (!buffer) {
        return;
    }

    painter_buffer_unscaled_size(buffer, width, height);
}
