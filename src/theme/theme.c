// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
    .font_name = "sans",
    .font_size = 11,
    .active_border_color = { 1.0, 1.0, 1.0, 1.0 },
    .inactive_border_color = { 1.0, 1.0, 1.0, 1.0 },
    .active_bg_color = { 0xfc / 255.0, 0xfc / 255.0, 0xfc / 255.0, 1.0 },
    .inactive_bg_color = { 0xef / 255.0, 0xf0 / 255.0, 0xf1 / 255.0, 1.0 },
    .active_text_color = { 0.0, 0.0, 0.0, 1.0 },
    .inactive_text_color = { 0x69 / 255.0, 0x69 / 255.0, 0x69 / 255.0, 1.0 },
    .text_justify = JUSTIFY_LEFT,
    .accent_color = { 55.0 / 255, 144.0 / 255, 250.0 / 255, 1.0 },

    .theme_name = DEFAULT_THEME,

    .ssd = {
        .border_width = 1,
        .corner_radius = 12,
        .title_height = 38,
        .resize_border = 13,
        .button_width = 30,
        .icon_size = 24,
        .button_svg = button_light_svg_src,
    },

    .shadow = {
        .shadow_border = 30,
        .corner_radius = 12,
    },

    .tooltip = {
        .border_width = 1,
        .corner_radius = 8,
    },

    .menu = {
        .border_width = 1,
        .corner_radius = 8,
        .sub_menu_gap = 2,
    },

    .snapbox = {
        .background_color = { 128.0 / 255, 128.0 / 255, 128.0 / 255, 128.0 / 255 },
    },

    .maxswitcher = {
        .background_color = { 1.0, 1.0, 1.0, 1.0 },
        .border_color = {25.0 / 255, 25.0 / 255, 25.0 / 255, 1.0 },
        .select_color = {16.0 / 255, 43.0 / 255.0, 75.0 / 255, 0.3},
        .select_width_gap = 5,
        .select_height_gap = 8,
        .icon_ratio = 0.4,
        .item_height = 48,
        .max_display_view = 25,
        .min_display_view = 4,
        .icon_size = 24,
        .icon_area_width = 30,
    },
};

/* default dark theme from ukui-dark */
static struct theme dark = {
    .builtin = true,
    .font_name = "sans",
    .font_size = 11,
    .active_border_color = { 0x1f / 255.0, 0x20 / 255.0, 0x22 / 255.0, 1.0 },
    .inactive_border_color = { 0x1f / 255.0, 0x20 / 255.0, 0x22 / 255.0, 1.0 },
    .active_bg_color = { 0x23 / 255.0, 0x26 / 255.0, 0x29 / 255.0, 1.0 },
    .inactive_bg_color = { 0x31 / 255.0, 0x36 / 255.0, 0x3b / 255.0, 1.0 },
    .active_text_color = { 0xcf / 255.0, 0xcf / 255.0, 0xcf / 255.0, 1.0 },
    .inactive_text_color = { 0x69 / 255.0, 0x69 / 255.0, 0x69 / 255.0, 1.0 },
    .text_justify = JUSTIFY_LEFT,
    .accent_color = { 243.0 / 255, 34.0 / 255, 45.0 / 255, 1.0 },

    .theme_name = DEFAULT_DARK_THEME,

    .ssd = {
        .border_width = 1,
        .corner_radius = 12,
        .title_height = 38,
        .resize_border = 13,
        .button_width = 30,
        .icon_size = 24,
        .button_svg = button_dark_svg_src,
    },

    .shadow = {
        .shadow_border = 30,
        .corner_radius = 12,
    },

    .tooltip = {
        .border_width = 1,
        .corner_radius = 8,
    },

    .menu = {
        .border_width = 1,
        .corner_radius = 8,
        .sub_menu_gap = 2,
    },

    .snapbox = {
        .background_color = { 128.0 / 255, 128.0 / 255, 128.0 / 255, 128.0 / 255 },
    },

    .maxswitcher = {
        .background_color = { 25.0 / 255, 25.0 / 255, 25.0 / 255, 1.0 },
        .border_color = { 1.0, 1.0, 1.0, 1.0 },
        .select_color = { 73.0 / 255, 10.0 / 255.0, 13.0 / 255, 0.3},
        .select_width_gap = 5,
        .select_height_gap = 8,
        .icon_ratio = 0.4,
        .item_height = 48,
        .max_display_view = 25,
        .min_display_view = 4,
        .icon_size = 24,
        .icon_area_width = 30,
    },
};

static int handle_manager_timer(void *data)
{
    time_t current_time, threshold;
    struct icon_theme *current_theme = manager->icon_theme;
    struct icon_theme *hicolor_theme = manager->hicolor_theme;
    bool reload_desktop_file = false, reload_pixmaps_file = false,
         reload_current_theme_file = false, reload_hicolor_theme_file = false;

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

    if (reload_desktop_file || reload_pixmaps_file || reload_current_theme_file ||
        reload_hicolor_theme_file) {
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
            new_hicolor_theme = icon_theme_load(DEFAULT_ICON_THEME_NAME);
            manager->hicolor_theme = new_hicolor_theme;
        }

        struct icon_theme *theme =
            manager->icon_theme ? manager->icon_theme : manager->hicolor_theme;
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
    }

    wl_event_source_timer_update(manager->timer, 5000);

    return 0;
}

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
    info.width = theme->ssd.button_width + theme->ssd.border_width;
    info.height = theme->ssd.title_height + theme->ssd.border_width;
    info.scale = scale;
    info.border_width = theme->ssd.border_width;
    info.corner_radius = theme->ssd.corner_radius;

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
    wl_list_insert(&theme->ssd.scaled_buffers, &buffers->link);

    buffers->buf[BUTTONS_BUFFER] = draw_svg(theme->ssd.button_svg, theme->ssd.button_width * 4,
                                            theme->ssd.button_width * 2, scale);

    draw_theme_corner(theme, scale, buffers);
    return buffers;
}

static struct wlr_buffer *draw_shadow_buffer(struct theme *theme)
{
    int half = theme->shadow.shadow_border + theme->shadow.corner_radius + theme->ssd.border_width;
    float fill_color[4] = { 0.0, 0.0, 0.0, 1.0 };

    /* a blured circle */
    struct draw_info info = {
        .width = half * 2,
        .height = half * 2,
        .scale = 1.0,
        .solid_rgba = fill_color,
        .circle = CIRCLE_TYPE_CLEAR,
        .corner_radius = theme->shadow.corner_radius + theme->ssd.border_width,
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

    wl_list_init(&theme->ssd.scaled_buffers);
    wl_list_insert(&manager->themes, &theme->link);

    /* override some configs */
    theme_override_config(theme);

    /* paint all buffers in scale */
    draw_theme_buffers(theme, scale);

    /* draw shadow buffer */
    theme->shadow.shadow = draw_shadow_buffer(theme);
    // painter_buffer_to_file(theme->shadow->shadow, "shadow.png");

    return theme;
}

static void theme_destroy(struct theme *theme)
{
    wl_list_remove(&theme->link);

    /* no need to free strings when builtin themes */
    if (!theme->builtin) {
        free((void *)theme->theme_name);
        free((void *)theme->font_name);
        free((void *)theme->ssd.button_svg);
    }

    /* destroy all theme buffers */
    struct theme_buffer *bufs, *bufs_tmp;
    wl_list_for_each_safe(bufs, bufs_tmp, &theme->ssd.scaled_buffers, link) {
        destroy_theme_buffers(bufs);
    }

    wlr_buffer_drop(theme->shadow.shadow);
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

    struct theme *theme, *tmp;
    wl_list_for_each_safe(theme, tmp, &manager->themes, link) {
        theme_destroy(theme);
    }

    desktop_infos_destroy(&manager->desktop_infos);
    icon_theme_destroy(manager->icon_theme);
    icon_theme_destroy(manager->hicolor_theme);
    pixmaps_icon_destroy(&manager->pixmaps_icons);
    icon_destroy(manager->fallback_icon);

    struct icon *icon, *icon_tmp;
    wl_list_for_each_safe(icon, icon_tmp, &manager->specific_icons, link) {
        icon_destroy(icon);
    }

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

    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);

    /* config support */
    theme_manager_config_init(manager);

    /* load all desktop files and get icon name */
    wl_list_init(&manager->desktop_infos);
    icon_load_desktop(&manager->desktop_infos);

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

    /* load pixmaps path icons */
    wl_list_init(&manager->pixmaps_icons);
    icon_load_pixmaps_path(&manager->pixmaps_icons);

    wl_list_init(&manager->specific_icons);

    manager->fallback_icon = icon_fallback_create();
    assert(manager->fallback_icon);

    theme_manager_write_config(manager, manager->current->theme_name);
    if (manager->icon_theme) {
        theme_manager_write_icon_config(manager, manager->icon_theme->name);
    }

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

struct theme *theme_manager_get_current(void)
{
    return manager->current;
}

static struct theme_buffer *theme_buffers_load(struct theme *theme, float scale)
{
    /* find scale buffers */
    struct theme_buffer *bufs;
    wl_list_for_each(bufs, &theme->ssd.scaled_buffers, link) {
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
        src->width = theme->ssd.button_width * scale;
        src->height = theme->ssd.button_width * scale;
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

static struct icon *theme_icon_find(const char *app_id)
{
    struct icon_theme *theme = manager->icon_theme ? manager->icon_theme : manager->hicolor_theme;
    struct icon *icon = NULL;
    char *icon_name = NULL;

    if (!theme || !app_id) {
        goto fallback;
    }

    struct desktop_info *desktop_info;
    wl_list_for_each(desktop_info, &manager->desktop_infos, link) {
        if (strcasecmp(desktop_info->app_id, app_id) == 0) {
            icon_name = desktop_info->icon_name;
        }
    }

    if (icon_name) {
        /* get icon from icon_name*/
        icon = icon_theme_get_icon(theme, icon_name, true);
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
    }

    /* get icon from app_id*/
    if (!icon) {
        icon = icon_theme_get_icon(theme, app_id, true);
        if (!icon && theme != manager->hicolor_theme) {
            icon = icon_theme_get_icon(manager->hicolor_theme, app_id, true);
        }
        if (!icon) {
            struct icon *icon_tmp;
            wl_list_for_each(icon_tmp, &manager->pixmaps_icons, link) {
                if (strcmp(app_id, icon_tmp->name) == 0) {
                    icon = icon_tmp;
                    break;
                }
            }
        }
    }

    /* if icon_name is specific path */
    if (!icon && icon_name) {
        icon = get_icon_from_specific_path(icon_name);
    }

fallback:
    if (!icon) {
        icon = manager->fallback_icon;
    }

    return icon;
}

struct wlr_buffer *theme_icon_load(const char *app_id, float scale)
{
    struct icon *icon = theme_icon_find(app_id);
    if (icon == manager->fallback_icon) {
        return NULL;
    }

    struct icon_buffer *buf = icon_get_buffer(icon, scale);
    return buf ? buf->buffer : NULL;
}

const char *theme_icon_name(const char *app_id)
{
    struct icon *icon = theme_icon_find(app_id);
    return icon->name;
}
