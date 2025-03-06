// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/util/box.h>

#include <kywc/log.h>

#include "base_dark_svg_src.h"
#include "base_light_svg_src.h"

#include "config.h"
#include "nls.h"
#include "painter.h"
#include "render/renderer.h"
#include "server.h"
#include "theme_p.h"

#define FALLBACK_THEME_NAME "fallback"

static struct theme_manager *manager = NULL;
static const char *fallback_icon_name = "fallback";

/* fallback light widget theme from ukui-white */
static struct widget_theme widget_light = {
    .name = FALLBACK_THEME_NAME,
    .type = THEME_TYPE_LIGHT,
    .builtin = true,

    .active_border_color = { 0.0, 0.0, 0.0, 0.15 },
    .inactive_border_color = { 0.0, 0.0, 0.0, 0.15 },
    .active_bg_color = { 1.0, 1.0, 1.0, 1.0 },
    .inactive_bg_color = { 245.0 / 255.0, 245.0 / 255.0, 245.0 / 255.0, 1.0 },
    .active_text_color = { 38.0 / 255.0, 38.0 / 255.0, 38.0 / 255.0, 1.0 },
    .inactive_text_color = { 38.0 / 255.0, 38.0 / 255.0, 38.0 / 255.0, 0.3 },
    .accent_color = { 55.0 / 255, 144.0 / 255, 250.0 / 255, 1.0 },
    .modal_mask_color = { 0, 0, 0, 0.2 },

    .button_svg = base_light_svg_src,
};

/* fallback dark theme from ukui-dark */
static struct widget_theme widget_dark = {
    .name = FALLBACK_THEME_NAME,
    .type = THEME_TYPE_DARK,
    .builtin = true,

    .active_border_color = { 1.0, 1.0, 1.0, 0.15 },
    .inactive_border_color = { 1.0, 1.0, 1.0, 0.15 },
    .active_bg_color = { 18.0 / 255.0, 18.0 / 255.0, 18.0 / 255.0, 1.0 },
    .inactive_bg_color = { 28.0 / 255.0, 28.0 / 255.0, 28.0 / 255.0, 1.0 },
    .active_text_color = { 0xcf / 255.0, 0xcf / 255.0, 0xcf / 255.0, 1.0 },
    .inactive_text_color = { 0xcf / 255.0, 0xcf / 255.0, 0xcf / 255.0, 0.3 },
    .accent_color = { 243.0 / 255, 34.0 / 255, 45.0 / 255, 1.0 },
    .modal_mask_color = { 0, 0, 0, 0.2 },

    .button_svg = base_dark_svg_src,
};

static struct wlr_buffer *draw_svg(const char *svg, int width, int height, float scale)
{
    struct draw_info info = { .width = width, .height = height, .scale = scale, .svg = svg };
    return painter_draw_buffer(&info);
}

static struct theme_buffer *draw_theme_buffer(struct theme *theme, float scale)
{
    struct theme_buffer *buffer = calloc(1, sizeof(struct theme_buffer));
    if (!buffer) {
        return NULL;
    }

    struct wlr_buffer *buf =
        draw_svg(theme->button_svg, theme->button_width * 4, theme->button_width * 3, scale);
    if (!buf) {
        free(buffer);
        return NULL;
    }

    buffer->scale = scale;
    wl_list_insert(&theme->scaled_buffers, &buffer->link);

    buffer->buffer = ky_renderer_upload_pixels(
        manager->server->renderer, manager->server->allocator, buf->width, buf->height, buf);
    if (buffer->buffer) {
        wlr_buffer_drop(buf);
    } else {
        buffer->buffer = buf;
    }

    return buffer;
}

static uint32_t theme_init(struct widget_theme *widget, float scale)
{
    struct theme *theme = &manager->theme;
    uint32_t mask = THEME_UPDATE_MASK_NONE;

    /* use name and type from widget */
    theme->name = widget->name;
    theme->builtin = widget->builtin;

    if (theme->type != widget->type) {
        theme->type = widget->type;
        mask |= THEME_UPDATE_MASK_TYPE;
    }

    /* copy color from widget */
    if (memcmp(theme->active_border_color, widget->active_border_color, sizeof(float[4]))) {
        memcpy(theme->active_border_color, widget->active_border_color, sizeof(float[4]));
        mask |= THEME_UPDATE_MASK_BORDER_COLOR;
    }
    if (memcmp(theme->inactive_border_color, widget->inactive_border_color, sizeof(float[4]))) {
        memcpy(theme->inactive_border_color, widget->inactive_border_color, sizeof(float[4]));
        mask |= THEME_UPDATE_MASK_BORDER_COLOR;
    }

    if (memcmp(theme->active_bg_color, widget->active_bg_color, sizeof(float[4]))) {
        memcpy(theme->active_bg_color, widget->active_bg_color, sizeof(float[4]));
        mask |= THEME_UPDATE_MASK_BACKGROUND_COLOR;
    }
    if (memcmp(theme->inactive_bg_color, widget->inactive_bg_color, sizeof(float[4]))) {
        memcpy(theme->inactive_bg_color, widget->inactive_bg_color, sizeof(float[4]));
        mask |= THEME_UPDATE_MASK_BACKGROUND_COLOR;
    }

    if (memcmp(theme->active_text_color, widget->active_text_color, sizeof(float[4]))) {
        memcpy(theme->active_text_color, widget->active_text_color, sizeof(float[4]));
        mask |= THEME_UPDATE_MASK_FONT;
    }
    if (memcmp(theme->inactive_text_color, widget->inactive_text_color, sizeof(float[4]))) {
        memcpy(theme->inactive_text_color, widget->inactive_text_color, sizeof(float[4]));
        mask |= THEME_UPDATE_MASK_FONT;
    }

    if (memcmp(theme->modal_mask_color, widget->modal_mask_color, sizeof(float[4]))) {
        memcpy(theme->modal_mask_color, widget->modal_mask_color, sizeof(float[4]));
        mask |= THEME_UPDATE_MASK_MODAL_MASK_COLOR;
    }

    struct global_theme *global = &manager->global;
    theme->font_name = global->font_name;
    theme->font_size = global->font_size;

    if (global->accent_color < 0 &&
        memcmp(theme->accent_color, widget->accent_color, sizeof(float[4]))) {
        memcpy(theme->accent_color, widget->accent_color, sizeof(float[4]));
        mask |= THEME_UPDATE_MASK_ACCENT_COLOR;
    }
    if (global->accent_color >= 0) {
        theme->accent_color[0] = (float)((global->accent_color >> 16) & 0xff) / 255;
        theme->accent_color[1] = (float)((global->accent_color >> 8) & 0xff) / 255;
        theme->accent_color[2] = (float)(global->accent_color & 0xff) / 255;
        theme->accent_color[3] = 1.0;
    }

    theme->corner_radius = global->corner_radius;
    theme->opacity = global->opacity;

    theme->layout_is_right_to_left = nls_layout_is_right_to_left();
    theme->text_is_right_align = nls_text_is_right_align();
    theme->text_justify = theme->layout_is_right_to_left ? JUSTIFY_RIGHT : JUSTIFY_LEFT;

    theme->ssd_need_maximize_button = true;
    theme->button_width = 32;
    theme->icon_size = 24;
    theme->border_width = 1;
    theme->title_height = 38;
    theme->subtitle_height = 38;
    theme->shadow_border = 30;
    theme->shadow_offset_x = 0;
    theme->shadow_offset_y = 0;
    theme->normal_radius = 6;

    // TODO: destroy all buffers, reuse it ?
    struct theme_buffer *buffer, *tmp;
    wl_list_for_each_safe(buffer, tmp, &theme->scaled_buffers, link) {
        wlr_buffer_drop(buffer->buffer);
        wl_list_remove(&buffer->link);
        free(buffer);
    }
    theme->button_svg = widget->button_svg;
    /* paint theme buffer in scale */
    draw_theme_buffer(theme, scale);

    return mask;
}

static void theme_finish(struct theme *theme)
{
    /* destroy all theme buffers */
    struct theme_buffer *buffer, *tmp;
    wl_list_for_each_safe(buffer, tmp, &theme->scaled_buffers, link) {
        wlr_buffer_drop(buffer->buffer);
        wl_list_remove(&buffer->link);
        free(buffer);
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);

    theme_finish(&manager->theme);
    free(manager->global.font_name);
    free(manager);
    manager = NULL;
}

static void handle_server_ready(struct wl_listener *listener, void *data)
{
    /* if widget theme is not set, use fallback widget theme */
    if (!manager->theme.name) {
        /* load theme from config, global theme is synced */
        enum theme_type theme_type = theme_manager_read_config(manager);
        theme_manager_set_widget_theme(NULL, theme_type);
    }
    /* just read the icon name, may shortcut in set_icon_theme  */
    const char *icon_theme_name = theme_manager_read_icon_config(manager);
    theme_manager_set_icon_theme(icon_theme_name);
}

struct theme_manager *theme_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct theme_manager));
    if (!manager) {
        return NULL;
    }

    wl_list_init(&manager->fallback_icon);
    wl_signal_init(&manager->events.update);
    wl_signal_init(&manager->events.icon_update);

    manager->server = server;
    manager->server_ready.notify = handle_server_ready;
    wl_signal_add(&server->events.ready, &manager->server_ready);
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);

    /* config support */
    theme_manager_config_init(manager);
    manager->icon = icon_manager_create(manager);
    wl_list_init(&manager->theme.scaled_buffers);

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

struct theme *theme_manager_get_theme(void)
{
    return &manager->theme;
}

static struct theme_buffer *theme_buffer_get_or_create(struct theme *theme, float scale)
{
    /* find scale buffer */
    struct theme_buffer *buffer;
    wl_list_for_each(buffer, &theme->scaled_buffers, link) {
        if (buffer->scale == scale) {
            return buffer;
        }
    }

    return draw_theme_buffer(theme, scale);
}

struct wlr_buffer *theme_buffer_load(struct theme *theme, float scale, enum theme_buffer_type type,
                                     struct wlr_fbox *src)
{
    struct theme_buffer *buffer = theme_buffer_get_or_create(theme, scale);
    if (!buffer) {
        return NULL;
    }

    if (src) {
        src->width = theme->button_width * scale;
        src->height = theme->button_width * scale;
        src->x = src->width * (type % 4);
        src->y = src->height * (int)(type / 4);
    }

    return buffer->buffer;
}

bool theme_manager_set_widget_theme(const char *name, enum theme_type type)
{
    // fallback to builtin theme
    if (!name || !*name || !manager->load_widget_theme) {
        name = FALLBACK_THEME_NAME;
    }
    if (type > THEME_TYPE_DARK) {
        type = THEME_TYPE_DARK;
    }

    struct theme *current = &manager->theme;
    /* current theme is not changed */
    if (current->name && strcmp(current->name, name) == 0 && current->type == type) {
        return true;
    }

    struct widget_theme *widget = NULL;
    if (strcmp(name, FALLBACK_THEME_NAME) == 0) {
        widget = type == THEME_TYPE_DARK ? &widget_dark : &widget_light;
    } else if (manager->load_widget_theme) {
        widget = manager->load_widget_theme(name, type);
    }
    if (!widget) {
        kywc_log(KYWC_WARN, "widget theme %s(%d) load failed", name, type);
        return false;
    }

    /* merge widget and global to theme */
    uint32_t mask = theme_init(widget, 1.0);
    theme_manager_write_config(manager, type);

    struct theme_update_event update_event = {
        .theme_type = type,
        .update_mask = mask,
    };
    wl_signal_emit_mutable(&manager->events.update, &update_event);

    return true;
}

bool theme_manager_set_font(const char *name, int size)
{
    if (!name || !*name || size <= 0) {
        return false;
    }

    struct global_theme *global = &manager->global;
    struct theme *theme = &manager->theme;
    bool changed = false;

    if (!global->font_name || strcmp(name, global->font_name) != 0) {
        free(global->font_name);
        global->font_name = strdup(name);
        theme->font_name = global->font_name;
        changed = true;
    }
    if (global->font_size != size) {
        global->font_size = size;
        theme->font_size = global->font_size;
        changed = true;
    }

    if (!changed) {
        return true;
    }

    struct theme_update_event update_event = {
        .update_mask = THEME_UPDATE_MASK_FONT,
    };
    wl_signal_emit_mutable(&manager->events.update, &update_event);

    theme_manager_write_config(manager, THEME_TYPE_UNDEFINED);
    return true;
}

bool theme_manager_set_accent_color(int32_t color)
{
    struct global_theme *global = &manager->global;
    struct theme *theme = &manager->theme;

    if (global->accent_color == color) {
        return true;
    }

    global->accent_color = color;
    theme->accent_color[0] = (float)((global->accent_color >> 16) & 0xff) / 255;
    theme->accent_color[1] = (float)((global->accent_color >> 8) & 0xff) / 255;
    theme->accent_color[2] = (float)(global->accent_color & 0xff) / 255;
    theme->accent_color[3] = 1.0;

    struct theme_update_event update_event = {
        .update_mask = THEME_UPDATE_MASK_ACCENT_COLOR,
    };
    wl_signal_emit_mutable(&manager->events.update, &update_event);

    theme_manager_write_config(manager, THEME_TYPE_UNDEFINED);
    return true;
}

bool theme_manager_set_corner_radius(int32_t radius)
{
    struct global_theme *global = &manager->global;
    struct theme *theme = &manager->theme;

    if (global->corner_radius == radius) {
        return true;
    }

    global->corner_radius = radius;
    theme->corner_radius = global->corner_radius;

    struct theme_update_event update_event = {
        .update_mask = THEME_UPDATE_MASK_CORNER_RADIUS,
    };
    wl_signal_emit_mutable(&manager->events.update, &update_event);

    theme_manager_write_config(manager, THEME_TYPE_UNDEFINED);
    return true;
}

bool theme_manager_set_opacity(int32_t opacity)
{
    struct global_theme *global = &manager->global;
    struct theme *theme = &manager->theme;

    opacity = opacity < 0 ? 0 : (opacity > 100 ? 100 : opacity);
    if (global->opacity == opacity) {
        return true;
    }

    global->opacity = opacity;
    theme->opacity = global->opacity;

    struct theme_update_event update_event = {
        .update_mask = THEME_UPDATE_MASK_OPACITY,
    };
    wl_signal_emit_mutable(&manager->events.update, &update_event);

    theme_manager_write_config(manager, THEME_TYPE_UNDEFINED);
    return true;
}

bool theme_manager_set_icon_theme(const char *icon_theme_name)
{
    if (!manager->icon_impl.set_icon_theme) {
        return false;
    }

    /* invalid or empty name */
    if (!icon_theme_name || !*icon_theme_name) {
        return false;
    }

    /* skip if icon_theme is hicolor */
    if (strcmp(icon_theme_name, FALLBACK_ICON_THEME_NAME) == 0) {
        return false;
    }

    bool ok = manager->icon_impl.set_icon_theme(manager->icon, icon_theme_name);
    if (!ok) {
        return false;
    }

    theme_manager_write_icon_config(manager, icon_theme_name);
    wl_signal_emit_mutable(&manager->events.icon_update, NULL);

    return true;
}

struct icon *theme_icon_from_app_id(const char *app_id)
{
    struct icon *fallback = (struct icon *)manager->fallback_icon.prev;
    if (!app_id || !*app_id) {
        return fallback;
    }

    if (manager->icon_impl.get_icon) {
        struct icon *icon = manager->icon_impl.get_icon(manager->icon, app_id);
        if (icon) {
            return icon;
        }
    }

    return fallback;
}

bool theme_icon_is_fallback(struct icon *icon)
{
    return icon == (struct icon *)manager->fallback_icon.prev;
}

const char *theme_icon_get_name(struct icon *icon)
{
    if (theme_icon_is_fallback(icon)) {
        return fallback_icon_name;
    }

    if (manager->icon_impl.get_icon_name) {
        return manager->icon_impl.get_icon_name(icon);
    }

    return fallback_icon_name;
}

struct wlr_buffer *theme_icon_get_buffer(struct icon *icon, int size, float scale)
{
    /* hide the fallback icon */
    if (theme_icon_is_fallback(icon)) {
        return NULL;
    }

    if (manager->icon_impl.get_icon_buffer) {
        return manager->icon_impl.get_icon_buffer(icon, size, scale);
    }

    return NULL;
}
