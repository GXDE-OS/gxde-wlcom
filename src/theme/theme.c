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

static void icon_pairs_destroy(void)
{
    struct icon_pair *icon_pair, *pair_tmp;
    wl_list_for_each_safe(icon_pair, pair_tmp, &manager->icon_pairs, link) {
        wl_list_remove(&icon_pair->link);
        free(icon_pair->app_id);
        free(icon_pair);
    }
}

static int handle_manager_timer(void *data)
{
    time_t current_time, threshold;
    struct icon_theme *current_theme = manager->icon_theme;
    struct icon_theme *hicolor_theme = manager->hicolor_theme;
    bool reload_desktop_file = false, reload_pixmaps_file = false,
         reload_current_theme_file = false, reload_hicolor_theme_file = false;

    wl_event_source_timer_update(manager->timer, 5000);
    current_time = time(NULL);
    threshold = current_time - 6;
    reload_desktop_file = icon_need_reload(APPPATH, NULL, threshold);
    reload_pixmaps_file = icon_need_reload(PIXMAPPATH, NULL, threshold);
    if (current_theme) {
        reload_current_theme_file = icon_need_reload(ICONPATH, current_theme, threshold);
    }
    if (hicolor_theme) {
        reload_hicolor_theme_file = icon_need_reload(ICONPATH, hicolor_theme, threshold);
    }

    if (!reload_desktop_file && !reload_pixmaps_file && !reload_current_theme_file &&
        !reload_hicolor_theme_file) {
        return 0;
    }

    struct wl_list new_desktop_infos, old_desktop_infos, new_pixmaps_icons, old_pixmaps_icons;
    struct icon_theme *new_icon_theme, *old_icon_theme, *new_hicolor_theme, *old_hicolor_theme;
    if (reload_desktop_file) {
        wl_list_init(&new_desktop_infos);
        icon_load_desktop(&new_desktop_infos);
        wl_list_init(&old_desktop_infos);
        wl_list_insert_list(&old_desktop_infos, &manager->desktop_infos);
        wl_list_init(&manager->desktop_infos);
        wl_list_insert_list(&manager->desktop_infos, &new_desktop_infos);
    }
    if (reload_pixmaps_file) {
        wl_list_init(&new_pixmaps_icons);
        icon_load_pixmaps_path(&new_pixmaps_icons);
        wl_list_init(&old_pixmaps_icons);
        wl_list_insert_list(&old_pixmaps_icons, &manager->pixmaps_icons);
        wl_list_init(&manager->pixmaps_icons);
        wl_list_insert_list(&manager->pixmaps_icons, &new_pixmaps_icons);
    }
    if (reload_current_theme_file) {
        old_icon_theme = manager->icon_theme;
        new_icon_theme = icon_theme_load(old_icon_theme->name);
        manager->icon_theme = new_icon_theme;
    }
    if (reload_hicolor_theme_file) {
        old_hicolor_theme = manager->hicolor_theme;
        new_hicolor_theme = icon_theme_load(FALLBACK_ICON_THEME_NAME);
        manager->hicolor_theme = new_hicolor_theme;
    }

    icon_pairs_destroy();

    struct icon_theme *theme = manager->icon_theme ? manager->icon_theme : manager->hicolor_theme;
    if (theme) {
        wl_signal_emit_mutable(&manager->events.icon_update, theme);
    }

    if (reload_desktop_file) {
        desktop_infos_destroy(&old_desktop_infos);
    }
    if (reload_pixmaps_file) {
        pixmaps_icon_destroy(&old_pixmaps_icons);
    }
    if (reload_current_theme_file) {
        icon_theme_destroy(old_icon_theme);
    }
    if (reload_hicolor_theme_file) {
        icon_theme_destroy(old_hicolor_theme);
    }

    return 0;
}

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

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->display_destroy.link);
    if (manager->timer) {
        wl_event_source_remove(manager->timer);
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);

    theme_finish(&manager->theme);
    free(manager->global.font_name);

    desktop_infos_destroy(&manager->desktop_infos);
    icon_theme_destroy(manager->icon_theme);
    icon_theme_destroy(manager->hicolor_theme);
    pixmaps_icon_destroy(&manager->pixmaps_icons);
    icon_destroy(manager->fallback_icon);
    icon_pairs_destroy();

    struct icon *icon, *icon_tmp;
    wl_list_for_each_safe(icon, icon_tmp, &manager->specific_icons, link) {
        icon_destroy(icon);
    }

    free(manager);
    manager = NULL;
}

struct theme_manager *theme_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct theme_manager));
    if (!manager) {
        return NULL;
    }

    wl_signal_init(&manager->events.update);
    wl_signal_init(&manager->events.icon_update);

    manager->server = server;
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);

    /* config support */
    theme_manager_config_init(manager);

    /* load theme from config */
    wl_list_init(&manager->theme.scaled_buffers);
    enum theme_type theme_type = theme_manager_read_config(manager);
    theme_manager_set_widget_theme(NULL, theme_type);

    wl_list_init(&manager->icon_pairs);
    /* load all desktop files and get icon name */
    wl_list_init(&manager->desktop_infos);
    icon_load_desktop(&manager->desktop_infos);
    /* fallback hicolor theme */
    manager->hicolor_theme = icon_theme_load(FALLBACK_ICON_THEME_NAME);

    const char *icon_theme = theme_manager_read_icon_config(manager);
    /* fallback to defaut if NULL */
    manager->icon_theme = icon_theme_load(icon_theme);
    assert(manager->icon_theme);

    /* load pixmaps path icons */
    wl_list_init(&manager->pixmaps_icons);
    icon_load_pixmaps_path(&manager->pixmaps_icons);

    manager->fallback_icon = icon_fallback_create();
    assert(manager->fallback_icon);

    wl_list_init(&manager->specific_icons);
    theme_manager_write_icon_config(manager, manager->icon_theme->name);

    manager->timer = wl_event_loop_add_timer(server->event_loop, handle_manager_timer, NULL);
    if (manager->timer) {
        wl_event_source_timer_update(manager->timer, 5000);
    } else {
        kywc_log(KYWC_ERROR, "failed to add theme manager timer!");
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

bool theme_manager_set_icon_theme(const char *icon_theme_name)
{
    /* invalid or empty name */
    if (!icon_theme_name || !*icon_theme_name) {
        return false;
    }

    struct icon_theme *old = manager->icon_theme;
    /* current icon_theme is not changed */
    if (old && !strcmp(icon_theme_name, old->name)) {
        return true;
    }

    /* old icon_theme is hicolor and current icon_theme is hicolor */
    if (!old && !strcmp(icon_theme_name, FALLBACK_ICON_THEME_NAME)) {
        return true;
    }

    /* not found, keep current icon_theme */
    struct icon_theme *new = NULL;
    if (strcmp(icon_theme_name, FALLBACK_ICON_THEME_NAME)) {
        new = icon_theme_load(icon_theme_name);
        if (!new) {
            return false;
        }
    }

    icon_pairs_destroy();
    /* apply the new icon_theme */
    manager->icon_theme = new;
    wl_signal_emit_mutable(&manager->events.icon_update, new);

    if (old) {
        icon_theme_destroy(old);
    }

    theme_manager_write_icon_config(manager, icon_theme_name);
    return true;
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

    if (strcmp(name, global->font_name) != 0) {
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

    struct theme *theme = theme_manager_get_theme();
    struct draw_info info = {
        .width = theme->icon_size,
        .height = theme->icon_size,
        .scale = scale,
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
            info.image = icon_png_similar->path;
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

static struct icon *get_icon_from_specific_path(const char *path)
{
    const char *full_name = strrchr(path, '/');
    if (!full_name) {
        return NULL;
    }
    full_name++;

    size_t index = strlen(full_name) - 4;
    struct icon *icon;
    wl_list_for_each(icon, &manager->specific_icons, link) {
        if (strncmp(full_name, icon->name, index) == 0) {
            return icon;
        }
    }

    icon = NULL;
    icon = icon_create(NULL, path, full_name);
    if (!icon) {
        return NULL;
    }
    wl_list_insert(&manager->specific_icons, &icon->link);

    return icon;
}

static struct icon *theme_icon_find(struct icon_theme *theme, const char *icon_name)
{
    if (!icon_name) {
        return NULL;
    }

    struct icon *icon = icon_theme_get_icon(theme, icon_name, true);
    if (!icon && theme != manager->hicolor_theme) {
        icon = icon_theme_get_icon(manager->hicolor_theme, icon_name, true);
    }
    if (!icon) {
        struct icon *icon_tmp;
        wl_list_for_each(icon_tmp, &manager->pixmaps_icons, link) {
            if (strcmp(icon_name, icon_tmp->name) == 0) {
                icon = icon_tmp;
                break;
            }
        }
    }

    /* if icon_name is specific path */
    if (!icon) {
        icon = get_icon_from_specific_path(icon_name);
    }

    return icon;
}

static void icon_pair_insert(const char *app_id, struct icon *icon)
{
    if (!icon) {
        return;
    }

    struct icon_pair *icon_pair = malloc(sizeof(struct icon_pair));
    if (!icon_pair) {
        return;
    }

    wl_list_insert(&manager->icon_pairs, &icon_pair->link);
    icon_pair->app_id = strdup(app_id);
    icon_pair->icon = icon;
}

static struct icon *theme_icon_from_icon_pair(const char *app_id)
{
    struct icon_pair *icon_pair;
    wl_list_for_each(icon_pair, &manager->icon_pairs, link) {
        if (strcmp(app_id, icon_pair->app_id) == 0) {
            return icon_pair->icon;
        }
    }
    return NULL;
}

struct icon *theme_icon_from_app_id(const char *app_id)
{
    struct icon_theme *theme = manager->icon_theme ? manager->icon_theme : manager->hicolor_theme;
    struct icon *icon = NULL;
    struct desktop_info *desktop_appid = NULL, *desktop_exec = NULL;

    if (!theme || !app_id || !*app_id) {
        return manager->fallback_icon;
    }

    /* first find in icon_pair cache */
    icon = theme_icon_from_icon_pair(app_id);
    if (icon) {
        return icon;
    }

    /* find desktop file from desktop_info->app_id */
    struct desktop_info *desktop_info;
    wl_list_for_each(desktop_info, &manager->desktop_infos, link) {
        if ((strcasecmp(desktop_info->app_id, app_id) == 0) ||
            (desktop_info->startup_name && strcasecmp(desktop_info->startup_name, app_id) == 0)) {
            desktop_appid = desktop_info;
            break;
        }
    }

    if (desktop_appid) {
        icon = theme_icon_find(theme, desktop_appid->icon_name);
    }
    if (!icon) {
        icon = theme_icon_find(theme, app_id);
    }

    /* fuzzy search desktop file from exec name */
    if (!icon) {
        wl_list_for_each(desktop_info, &manager->desktop_infos, link) {
            if (desktop_info->exec_name && strcasecmp(desktop_info->exec_name, app_id) == 0) {
                // abandon this search if find multiple results
                if (desktop_exec) {
                    desktop_exec = NULL;
                    break;
                }
                desktop_exec = desktop_info;
            }
        }

        if (desktop_exec) {
            icon = theme_icon_find(theme, desktop_exec->icon_name);
        }
    }

    if (!icon) {
        icon = manager->fallback_icon;
    }
    icon_pair_insert(app_id, icon);

    return icon;
}

const char *theme_icon_get_name(struct icon *icon)
{
    return icon->name;
}

struct wlr_buffer *theme_icon_get_buffer(struct icon *icon, float scale)
{
    if (icon == manager->fallback_icon) {
        return NULL;
    }

    struct icon_buffer *buf = icon_get_buffer(icon, scale);
    return buf ? buf->buffer : NULL;
}
