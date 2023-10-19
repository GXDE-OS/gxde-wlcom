// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/util/box.h>

#include <kywc/log.h>

#include "button_dark_svg_src.h"
#include "button_light_svg_src.h"

#include "config.h"
#include "painter.h"
#include "server.h"
#include "theme_p.h"

#define DEFAULT_THEME "builtin-light"
#define DEFAULT_DARK_THEME "builtin-dark"

static struct theme_manager *manager = NULL;

/* default light theme from ukui-white */
static struct theme light = {
    .builtin = true,
    .border_width = 1,
    .padding_height = 3,
    .menu_overlap_x = 0,
    .menu_overlap_y = 0,
    .button_width = 30,
    .corner_radius = 12,
    .title_height = 38,
    .resize_border = 13,
    .shadow_border = 30,
    .font_name = "sans",
    .font_size = 11,
    .active_border_color = { 1.0, 1.0, 1.0, 1.0 },
    .inactive_border_color = { 1.0, 1.0, 1.0, 1.0 },
    .active_bg_color = { 0xfc / 255.0, 0xfc / 255.0, 0xfc / 255.0, 1.0 },
    .inactive_bg_color = { 0xef / 255.0, 0xf0 / 255.0, 0xf1 / 255.0, 1.0 },
    .active_text_color = { 0.0, 0.0, 0.0, 1.0 },
    .inactive_text_color = { 0x69 / 255.0, 0x69 / 255.0, 0x69 / 255.0, 1.0 },
    .text_justify = JUSTIFY_LEFT,
    .selected_color = { 128.0 / 255, 128.0 / 255, 128.0 / 255, 128.0 / 255 },
    .accent_color = { 55.0 / 255, 144.0 / 255, 250.0 / 255, 1.0 },

    .theme_name = DEFAULT_THEME,
    .button_svg = button_light_svg_src,
};

/* default dark theme from ukui-dark */
static struct theme dark = {
    .builtin = true,
    .border_width = 1,
    .padding_height = 3,
    .menu_overlap_x = 0,
    .menu_overlap_y = 0,
    .button_width = 30,
    .corner_radius = 12,
    .title_height = 38,
    .resize_border = 13,
    .shadow_border = 30,
    .font_name = "sans",
    .font_size = 11,
    .active_border_color = { 0x1f / 255.0, 0x20 / 255.0, 0x22 / 255.0, 1.0 },
    .inactive_border_color = { 0x1f / 255.0, 0x20 / 255.0, 0x22 / 255.0, 1.0 },
    .active_bg_color = { 0x23 / 255.0, 0x26 / 255.0, 0x29 / 255.0, 1.0 },
    .inactive_bg_color = { 0x31 / 255.0, 0x36 / 255.0, 0x3b / 255.0, 1.0 },
    .active_text_color = { 0xcf / 255.0, 0xcf / 255.0, 0xcf / 255.0, 1.0 },
    .inactive_text_color = { 0x69 / 255.0, 0x69 / 255.0, 0x69 / 255.0, 1.0 },
    .text_justify = JUSTIFY_LEFT,
    .selected_color = { 128.0 / 255, 128.0 / 255, 128.0 / 255, 128.0 / 255 },
    .accent_color = { 243.0 / 255, 34.0 / 255, 45.0 / 255, 1.0 },

    .theme_name = DEFAULT_DARK_THEME,
    .button_svg = button_dark_svg_src,
};

static void destroy_theme_buffers(struct theme_buffer *bufs)
{
    for (int i = 0; i < THEME_BUFFER_COUNT; i++) {
        wlr_buffer_drop(bufs->buf[i]);
    }
    wl_list_remove(&bufs->link);
    free(bufs);
}

static void draw_theme_corner(struct theme *theme, float scale, struct theme_buffer *buffers)
{
    struct draw_info info = { 0 };
    info.width = theme->button_width + theme->border_width;
    info.height = theme->title_height + theme->border_width;
    info.scale = scale;
    info.border_width = theme->border_width;
    info.corner_radius = theme->corner_radius;

    /* draw top-left corner */
    info.corner_mask = CORNER_MASK_TOP_LEFT;
    info.border_mask = BORDER_MASK_LEFT | BORDER_MASK_TOP;
    info.solid_rgba = theme->active_bg_color;
    info.border_rgba = theme->active_border_color;
    buffers->buf[CORNER_TOP_LEFT_ACTIVE_BUFFER] = painter_draw_buffer(&info);
    info.solid_rgba = theme->inactive_bg_color;
    info.border_rgba = theme->inactive_border_color;
    buffers->buf[CORNER_TOP_LEFT_INACTIVE_BUFFER] = painter_draw_buffer(&info);

    /* draw top-right corner */
    info.corner_mask = CORNER_MASK_TOP_RIGHT;
    info.border_mask = BORDER_MASK_TOP | BORDER_MASK_RIGHT;
    info.solid_rgba = theme->active_bg_color;
    info.border_rgba = theme->active_border_color;
    buffers->buf[CORNER_TOP_RIGHT_ACTIVE_BUFFER] = painter_draw_buffer(&info);
    info.solid_rgba = theme->inactive_bg_color;
    info.border_rgba = theme->inactive_border_color;
    buffers->buf[CORNER_TOP_RIGHT_INACTIVE_BUFFER] = painter_draw_buffer(&info);
}

static struct wlr_buffer *draw_svg(const char *svg, int width, int height, float scale)
{
    struct draw_info info = {
        .width = width,
        .height = height,
        .scale = scale,
        .svg = svg,
    };

    return painter_draw_buffer(&info);
}

static struct theme_buffer *draw_theme_buffers(struct theme *theme, float scale)
{
    struct theme_buffer *buffers = calloc(1, sizeof(struct theme_buffer));
    if (!buffers) {
        return NULL;
    }

    buffers->scale = scale;
    wl_list_insert(&theme->scaled_buffers, &buffers->link);

    buffers->buf[BUTTONS_BUFFER] =
        draw_svg(theme->button_svg, theme->button_width * 4, theme->button_width * 2, scale);

    draw_theme_corner(theme, scale, buffers);
    return buffers;
}

static struct wlr_buffer *draw_shadow_buffer(struct theme *theme)
{
    int half = theme->shadow_border + theme->corner_radius + theme->border_width;
    float fill_color[4] = { 0.0, 0.0, 0.0, 1.0 };

    /* a blured circle */
    struct draw_info info = {
        .width = half * 2,
        .height = half * 2,
        .scale = 1.0,
        .solid_rgba = fill_color,
        .circle = CIRCLE_TYPE_CLEAR,
        .corner_radius = theme->corner_radius + theme->border_width,
        .blur_margin = half,
    };

    return painter_draw_buffer(&info);
}

static int32_t get_color_int(float *rgba);

static void theme_override_config(struct theme *theme)
{
    struct theme_override *override = &manager->override;

    if (override->font_name && strcmp(override->font_name, theme->font_name)) {
        if (theme->builtin) {
            theme->font_name = override->font_name;
        } else {
            free((void *)theme->font_name);
            theme->font_name = strdup(override->font_name);
        }
    }

    if (override->font_size > 0 && override->font_size != theme->font_size) {
        theme->font_size = override->font_size;
    }

    if (override->accent_color >= 0) {
        theme->accent_color[0] = (float)((override->accent_color >> 16) & 0xff) / 255;
        theme->accent_color[1] = (float)((override->accent_color >> 8) & 0xff) / 255;
        theme->accent_color[2] = (float)(override->accent_color & 0xff) / 255;
        theme->accent_color[3] = 1.0;
    }
}

static struct theme *theme_create(const char *name, float scale)
{
    if (!name || !*name) {
        return NULL;
    }

    struct theme *theme = NULL;
    if (!strcmp(name, DEFAULT_THEME)) {
        theme = &light;
    } else if (!strcmp(name, DEFAULT_DARK_THEME)) {
        theme = &dark;
    } else {
        // TODO: load theme from file
        return NULL;
    }

    wl_list_init(&theme->scaled_buffers);
    wl_list_insert(&manager->themes, &theme->link);

    /* override some configs */
    theme_override_config(theme);

    /* paint all buffers in scale */
    draw_theme_buffers(theme, scale);

    /* draw shadow buffer */
    theme->shadow = draw_shadow_buffer(theme);
    // painter_buffer_to_file(theme->shadow, "shadow.png");

    return theme;
}

static void theme_destroy(struct theme *theme)
{
    wl_list_remove(&theme->link);

    /* no need to free strings when builtin themes */
    if (!theme->builtin) {
        free((void *)theme->theme_name);
        free((void *)theme->font_name);
        free((void *)theme->button_svg);
    }

    /* destroy all theme buffers */
    struct theme_buffer *bufs, *bufs_tmp;
    wl_list_for_each_safe(bufs, bufs_tmp, &theme->scaled_buffers, link) {
        destroy_theme_buffers(bufs);
    }

    wlr_buffer_drop(theme->shadow);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);

    struct theme *theme, *tmp;
    wl_list_for_each_safe(theme, tmp, &manager->themes, link) {
        theme_destroy(theme);
    }

    icon_theme_destroy(manager->icon_theme);
    icon_theme_destroy(manager->hicolor_theme);
    icon_destroy(manager->fallback_icon);

    free(manager->override.font_name);
    free(manager);
    manager = NULL;
}

struct theme_manager *theme_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct theme_manager));
    if (!manager) {
        return NULL;
    }

    wl_list_init(&manager->themes);
    wl_signal_init(&manager->events.update);
    wl_signal_init(&manager->events.icon_update);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);

    /* config support */
    theme_manager_config_init(manager);

    /* load theme from config */
    const char *theme = theme_manager_read_config(manager);
    manager->current = theme_create(theme ? theme : DEFAULT_THEME, 1.0);
    /* theme load failed, fallback to default theme */
    if (!manager->current) {
        manager->current = theme_create(DEFAULT_THEME, 1.0);
    }

    manager->hicolor_theme = icon_theme_load(DEFAULT_ICON_THEME_NAME);
    const char *icon_theme = theme_manager_read_icon_config(manager);
    if (icon_theme) {
        manager->icon_theme = icon_theme_load(icon_theme);
    }
    manager->fallback_icon = icon_fallback_create();
    assert(manager->fallback_icon);

    theme_manager_write_config(manager, manager->current->theme_name);
    if (manager->icon_theme) {
        theme_manager_write_icon_config(manager, manager->icon_theme->name);
    }

    return manager;
}

void theme_manager_add_update_listener(struct wl_listener *listener)
{
    wl_signal_add(&manager->events.update, listener);
}

void theme_manager_add_icon_update_listener(struct wl_listener *listener)
{
    wl_signal_add(&manager->events.icon_update, listener);
}

struct theme *theme_manager_get_current(void)
{
    return manager->current;
}

static struct theme_buffer *theme_buffers_load(struct theme *theme, float scale)
{
    /* find scale buffers */
    struct theme_buffer *bufs;
    wl_list_for_each(bufs, &theme->scaled_buffers, link) {
        if (bufs->scale == scale) {
            return bufs;
        }
    }

    return draw_theme_buffers(theme, scale);
}

struct wlr_buffer *theme_buffer_load(struct theme *theme, float scale, enum theme_buffer_type type,
                                     struct wlr_fbox *src)
{
    struct theme_buffer *bufs = theme_buffers_load(theme, scale);
    if (!bufs) {
        return NULL;
    }

    int buffer_index = type <= BUTTON_CLOSE ? BUTTONS_BUFFER : (type - BUTTON_CLOSE);
    if (!src) {
        return bufs->buf[buffer_index];
    }

    if (buffer_index == BUTTONS_BUFFER) {
        src->width = theme->button_width * scale;
        src->height = theme->button_width * scale;
        src->x = src->width * (type % 4);
        src->y = src->height * (int)(type / 4);
    } else {
        src->width = src->height = 0;
    }

    return bufs->buf[buffer_index];
}

bool theme_manager_set_icon_theme(const char *icon_theme_name)
{
    /* invaild or empty name */
    if (!icon_theme_name || !*icon_theme_name) {
        return false;
    }

    struct icon_theme *old = manager->icon_theme;
    /* current icon_theme is not changed */
    if (old && !strcmp(icon_theme_name, old->name)) {
        return true;
    }

    /* old icon_theme is hicolor and current icon_theme is hicolor */
    if (!old && !strcmp(icon_theme_name, DEFAULT_ICON_THEME_NAME)) {
        return true;
    }

    /* not found, keep current icon_theme */
    struct icon_theme *new = NULL;
    if (strcmp(icon_theme_name, DEFAULT_ICON_THEME_NAME)) {
        new = icon_theme_load(icon_theme_name);
        if (!new) {
            return false;
        }
    }

    /* apply the new icon_theme */
    manager->icon_theme = new;
    wl_signal_emit_mutable(&manager->events.icon_update, new);

    if (old) {
        icon_theme_destroy(old);
    }

    theme_manager_write_icon_config(manager, icon_theme_name);
    return true;
}

bool theme_manager_set_theme(const char *name)
{
    /* invaild or empty name */
    if (!name || !*name) {
        return false;
    }

    struct theme *old = manager->current;
    /* current theme is not changed */
    if (old && !strcmp(name, old->theme_name)) {
        return true;
    }

    /* not found, keep current theme */
    struct theme *new = theme_create(name, 1.0);
    if (!new) {
        return false;
    }

    /* apply the new theme */
    manager->current = new;
    wl_signal_emit_mutable(&manager->events.update, new);

    if (old) {
        theme_destroy(old);
    }

    theme_manager_write_config(manager, name);
    return true;
}

bool theme_manager_set_font(const char *name, int size)
{
    struct theme_override *override = &manager->override;
    struct theme *current = manager->current;
    bool changed = false;

    if (name && strcmp(name, current->font_name) != 0) {
        free(override->font_name);
        override->font_name = strdup(name);
        changed = true;
    }

    if (size > 0 && current->font_size != size) {
        override->font_size = size;
        changed = true;
    }

    if (!changed) {
        return false;
    }

    theme_override_config(current);
    wl_signal_emit_mutable(&manager->events.update, current);

    theme_manager_write_config(manager, NULL);
    return true;
}

static int32_t get_color_int(float *rgba)
{
    int32_t color = (int)(rgba[0] * 255) << 16;
    color |= (int)(rgba[1] * 255) << 8;
    color |= (int)(rgba[2] * 255);
    return color;
}

bool theme_manager_set_accent_color(int32_t color)
{
    struct theme_override *override = &manager->override;
    struct theme *current = manager->current;

    int32_t current_color = get_color_int(current->accent_color);
    if (color == current_color) {
        return true;
    }

    override->accent_color = color;
    theme_override_config(current);
    wl_signal_emit_mutable(&manager->events.update, current);

    theme_manager_write_config(manager, NULL);
    return true;
}

static struct icon_buffer *icon_get_buffer(struct icon *icon, float scale)
{
    struct icon_buffer *buf;
    wl_list_for_each(buf, &icon->buffers, link) {
        if (buf->scale == scale) {
            return buf;
        }
    }

    buf = calloc(1, sizeof(struct icon_buffer));
    if (!buf) {
        return NULL;
    }

    struct draw_info info = {
        .width = 24,
        .height = 24,
        .scale = scale,
        .svg = NULL,
        .png_path = NULL,
    };

    if (icon->svg) {
        info.svg = icon->svg;
    } else if (!wl_list_empty(&icon->pngs)) {
        float scale_width = info.width * info.scale;
        float min_abs = 256.0;
        float tmp_abs;
        struct icon_png *icon_png_similar = NULL;
        struct icon_png *icon_png;
        wl_list_for_each(icon_png, &icon->pngs, link) {
            if (icon_png->scale == 1) {
                tmp_abs = fabs(icon_png->width - scale_width);
                if (tmp_abs < min_abs) {
                    min_abs = tmp_abs;
                    icon_png_similar = icon_png;
                }
            }
        }
        if (icon_png_similar) {
            info.png_path = icon_png_similar->path;
        }
    }

    buf->buffer = painter_draw_buffer(&info);
    if (!buf->buffer) {
        free(buf);
        return NULL;
    }

    buf->scale = scale;
    wl_list_insert(&icon->buffers, &buf->link);
    return buf;
}

struct wlr_buffer *theme_icon_load(const char *name, float scale)
{
    struct icon_theme *theme = manager->icon_theme ? manager->icon_theme : manager->hicolor_theme;
    struct icon *icon = NULL;

    if (theme && name) {
        icon = icon_theme_get_icon(theme, name, true);
        if (!icon && theme != manager->hicolor_theme) {
            icon = icon_theme_get_icon(manager->hicolor_theme, name, true);
        }
    }
    if (!icon) {
        icon = manager->fallback_icon;
    }

    struct icon_buffer *buf = icon_get_buffer(icon, scale);
    return buf ? buf->buffer : NULL;
}
