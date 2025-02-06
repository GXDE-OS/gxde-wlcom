// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdlib.h>

#include <kywc/log.h>

#include "server.h"
#include "theme_p.h"
#include "util/file.h"
#include "util/hash_table.h"

#define MINIMIZE                                                                                   \
    "M19.5 15H10.5C10.2239 15 10 15.2239 10 15.5C10 15.7761 10.2239 16 10.5 16H19.5C19.7761 16 "   \
    "20 15.7761 20 15.5C20 15.2239 19.7761 15 19.5 15Z"
#define MAXMIZE                                                                                    \
    "M19 10C19.2652 10 19.5196 10.1054 19.7071 10.2929C19.8946 10.4804 20 10.7348 20 11V19C20 "    \
    "19.2652 19.8946 19.5196 19.7071 19.7071C19.5196 19.8946 19.2652 20 19 20H11C10.7348 20 "      \
    "10.4804 19.8946 10.2929 19.7071C10.1054 19.5196 10 19.2652 10 19V11C10 10.7348 10.1054 "      \
    "10.4804 10.2929 10.2929C10.4804 10.1054 10.7348 10 11 10H19ZM19 9H11C10.4696 9 9.96086 "      \
    "9.21071 9.58579 9.58579C9.21071 9.96086 9 10.4696 9 11V19C9 19.5304 9.21071 20.0391 9.58579 " \
    "20.4142C9.96086 20.7893 10.4696 21 11 21H19C19.5304 21 20.0391 20.7893 20.4142 "              \
    "20.4142C20.7893 20.0391 21 19.5304 21 19V11C21 10.4696 20.7893 9.96086 20.4142 "              \
    "9.58579C20.0391 9.21071 19.5304 9 19 9Z"
#define RESTORE                                                                                    \
    "M20 8H14C13.4696 8 12.9609 8.21071 12.5858 8.58579C12.2107 8.96086 12 9.46957 12 "            \
    "10V11H11C10.4696 11 9.96086 11.2107 9.58579 11.5858C9.21071 11.9609 9 12.4696 9 13V19C9 "     \
    "19.5304 9.21071 20.0391 9.58579 20.4142C9.96086 20.7893 10.4696 21 11 21H17C17.5304 21 "      \
    "18.0391 20.7893 18.4142 20.4142C18.7893 20.0391 19 19.5304 19 19V18H20C20.5304 18 21.0391 "   \
    "17.7893 21.4142 17.4142C21.7893 17.0391 22 16.5304 22 16V10C22 9.46957 21.7893 8.96086 "      \
    "21.4142 8.58579C21.0391 8.21071 20.5304 8 20 8ZM18 19C18 19.2652 17.8946 19.5196 17.7071 "    \
    "19.7071C17.5196 19.8946 17.2652 20 17 20H11C10.7348 20 10.4804 19.8946 10.2929 "              \
    "19.7071C10.1054 19.5196 10 19.2652 10 19V13C10 12.7348 10.1054 12.4804 10.2929 "              \
    "12.2929C10.4804 12.1054 10.7348 12 11 12H17C17.2652 12 17.5196 12.1054 17.7071 "              \
    "12.2929C17.8946 12.4804 18 12.7348 18 13V19ZM21 16C21 16.2652 20.8946 16.5196 20.7071 "       \
    "16.7071C20.5196 16.8946 20.2652 17 20 17H19V13C19 12.4696 18.7893 11.9609 18.4142 "           \
    "11.5858C18.0391 11.2107 17.5304 11 17 11H13V10C13 9.73478 13.1054 9.48043 13.2929 "           \
    "9.29289C13.4804 9.10536 13.7348 9 14 9H20C20.2652 9 20.5196 9.10536 20.7071 9.29289C20.8946 " \
    "9.48043 21 9.73478 21 10V16Z"
#define CLOSE1                                                                                     \
    "M20.253 19.5459L11.0606 10.3536C10.8653 10.1583 10.5487 10.1583 10.3535 10.3536C10.1582 "     \
    "10.5488 10.1582 10.8654 10.3535 11.0607L19.5459 20.253C19.7411 20.4483 20.0577 20.4483 "      \
    "20.253 20.253C20.4482 20.0578 20.4482 19.7412 20.253 19.5459Z"
#define CLOSE2                                                                                     \
    "M19.5506 10.3603L10.3582 19.5527C10.1629 19.748 10.1629 20.0645 10.3582 20.2598C10.5535 "     \
    "20.4551 10.87 20.4551 11.0653 20.2598L20.2577 11.0674C20.4529 10.8722 20.4529 10.5556 "       \
    "20.2577 10.3603C20.0624 10.165 19.7458 10.165 19.5506 10.3603Z"

#define THEME_DIR "/usr/share/config/themeconfig/token"

enum theme_value_type {
    theme_value_type_none = 0,
    theme_value_type_number = 1 << 0,
    theme_value_type_string = 1 << 1,
    theme_value_type_color = 1 << 2,
    theme_value_type_gradient = 1 << 3,
};

struct color {
    uint32_t r, g, b;
    float a;
};

struct gradient {
    struct color background;
    struct color start, stop;
    int angle;
};

struct theme_value {
    enum theme_value_type type;
    union {
        struct color color;
        struct gradient gradient;
        const char *string;
        int64_t number;
    };
};

struct theme_key_desc {
    // key name that need to find
    const char *name;
    // value type in masks
    uint32_t type;
    // value offset in struct ukui_theme
    size_t offset;
};

struct ukui_theme {
    struct widget_theme theme;
    struct wl_list link;
    struct hash_table *ht;

    struct theme_value normal_bg;
    struct theme_value normal_border;
    struct theme_value hover_bg;
    struct theme_value hover_border;
    struct theme_value click_bg;
    struct theme_value click_border;
    struct theme_value close_hover_bg;
    struct theme_value close_click_bg;

    struct theme_value white_text;
    struct theme_value active_text;
    struct theme_value inactive_text;

    struct theme_value corner_radius;
    struct theme_value window_radius;

    struct theme_value active_border;
    struct theme_value inactive_border;

    struct theme_value active_bg;
    struct theme_value inactive_bg;

    struct theme_value modal_mask;
};

static struct ukui_theme_manager {
    struct wl_list themes;
    struct wl_listener server_destroy;
} *manager = NULL;

static const struct theme_key_desc theme_keys[] = {
    { "kgray-alpha0", theme_value_type_color, offsetof(struct ukui_theme, normal_bg) },
    { "kline-component-normal", theme_value_type_color | theme_value_type_gradient,
      offsetof(struct ukui_theme, normal_border) },
    { "kcomponent-alpha-hover", theme_value_type_color | theme_value_type_gradient,
      offsetof(struct ukui_theme, hover_bg) },
    { "kline-component-hover", theme_value_type_color | theme_value_type_gradient,
      offsetof(struct ukui_theme, hover_border) },
    { "kcomponent-alpha-click", theme_value_type_color | theme_value_type_gradient,
      offsetof(struct ukui_theme, click_bg) },
    { "kline-component-click", theme_value_type_color | theme_value_type_gradient,
      offsetof(struct ukui_theme, click_border) },
    { "kerror-hover", theme_value_type_color | theme_value_type_gradient,
      offsetof(struct ukui_theme, close_hover_bg) },
    { "kerror-click", theme_value_type_color | theme_value_type_gradient,
      offsetof(struct ukui_theme, close_click_bg) },
    { "kfont-white", theme_value_type_color, offsetof(struct ukui_theme, white_text) },
    { "windowtext-active", theme_value_type_color, offsetof(struct ukui_theme, active_text) },
    { "windowtext-inactive", theme_value_type_color, offsetof(struct ukui_theme, inactive_text) },
    { "kradius-normal", theme_value_type_number, offsetof(struct ukui_theme, corner_radius) },
    { "kradius-window", theme_value_type_number, offsetof(struct ukui_theme, window_radius) },
    { "kline-window-active", theme_value_type_color | theme_value_type_gradient,
      offsetof(struct ukui_theme, active_border) },
    { "kline-window-inactive", theme_value_type_color | theme_value_type_gradient,
      offsetof(struct ukui_theme, inactive_border) },
    { "window-active", theme_value_type_color, offsetof(struct ukui_theme, active_bg) },
    { "window-inactive", theme_value_type_color, offsetof(struct ukui_theme, inactive_bg) },
    { "kmodalmask", theme_value_type_color, offsetof(struct ukui_theme, modal_mask) },
};

#define THEME_KEYS (sizeof(theme_keys) / sizeof(theme_keys[0]))

static inline uint32_t color_to_uint(struct color *color)
{
    return color->r << 16 | color->g << 8 | color->b;
}

static inline void color_to_array(struct color *color, float *array)
{
    array[0] = color->r / 255.0;
    array[1] = color->g / 255.0;
    array[2] = color->b / 255.0;
    array[3] = color->a;
}

static void svg_start(FILE *stream, float x, float y, float width, float height)
{
    fprintf(stream, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%g %g %g %g\">", x, y,
            width, height);
}

static void svg_stop(FILE *stream)
{
    fprintf(stream, " </svg>\n");
}

static void svg_begin(FILE *stream, float x, float y, float width, float height)
{
    fprintf(stream, " <svg x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\">", x, y, width, height);
}

static void svg_end(FILE *stream)
{
    fprintf(stream, " </svg>");
}

static void svg_add_path(FILE *stream, const char *path, struct theme_value *fill)
{
    fprintf(stream, " <path d=\"%s\" fill=\"#%x\" fill-opacity=\"%g\"/>", path,
            color_to_uint(&fill->color), fill->color.a);
}

static void svg_add_rect(FILE *stream, float width, float height, struct theme_value *radius,
                         float border_width, struct theme_value *border_color,
                         struct theme_value *background)
{
    static int url_index = 0;

    if (background->type == theme_value_type_color) {
        if (background->color.a == 0) {
            return;
        }

        fprintf(stream,
                " <rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"%g\" fill=\"#%x\" "
                "stroke=\"#%x\" stroke-width=\"%g\" opacity=\"%g\"/>",
                border_width / 2, border_width / 2, width - border_width, height - border_width,
                (float)radius->number, color_to_uint(&background->color),
                color_to_uint(&border_color->color), border_width, background->color.a);

    } else if (background->type == theme_value_type_gradient) {
        fprintf(stream,
                " <rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"%g\" fill=\"#%x\" "
                "stroke=\"#%x\" stroke-width=\"%g\" opacity=\"%g\"/>",
                border_width / 2, border_width / 2, width - border_width, height - border_width,
                (float)radius->number, color_to_uint(&background->gradient.background),
                color_to_uint(&border_color->color), border_width,
                background->gradient.background.a);

        url_index++;
        fprintf(stream,
                " <rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" rx=\"%g\" fill=\"url(#%d)\" "
                "stroke=\"#%x\" stroke-width=\"%g\" opacity=\"%g\"/>",
                border_width / 2, border_width / 2, width - border_width, height - border_width,
                (float)radius->number, url_index, color_to_uint(&border_color->color), border_width,
                background->gradient.start.a);

        double angle = background->gradient.angle * 3.1415926 / 180.0;
        fprintf(stream,
                " <defs> <linearGradient id=\"%d\" x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\"> <stop "
                "offset=\"0\" stop-color=\"#%x\"/> <stop offset=\"1\" stop-color=\"#%x\"/> "
                "</linearGradient> </defs>",
                url_index, 0.5 - sin(angle) * 0.5, 0.5 + cos(angle) * 0.5, 0.5 + sin(angle) * 0.5,
                0.5 - cos(angle) * 0.5, color_to_uint(&background->gradient.start),
                color_to_uint(&background->gradient.stop));
    }
}

static void theme_generate_svgs(struct ukui_theme *theme)
{
    char *buffer = NULL;
    size_t size = 0;
    FILE *stream = open_memstream(&buffer, &size);
    if (!stream) {
        return;
    }

    char *paths[5] = { MINIMIZE, MAXMIZE, RESTORE, CLOSE1, CLOSE2 };

    // svg start
#if 0
    svg_start(stream, 0, 0, 120, 180);
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 4; j++) {
            svg_begin(stream, j * 30, i * 30, 30, 30);
            if (i < 2) { // normal
                svg_add_rect(stream, 30, 30, &theme->corner_radius, 0, &theme->normal_border,
                             &theme->normal_bg);
            } else if (i < 4) { // hover
                svg_add_rect(stream, 30, 30, &theme->corner_radius, 0, &theme->hover_border,
                             j == 3 ? &theme->close_hover_bg : &theme->hover_bg);
            } else { // click
                svg_add_rect(stream, 30, 30, &theme->corner_radius, 0, &theme->click_border,
                             j == 3 ? &theme->close_click_bg : &theme->click_bg);
            }
            if (j == 3) {
                if (i < 2) {
                    svg_add_path(stream, paths[j],
                                 i % 2 ? &theme->active_text : &theme->inactive_text);
                    svg_add_path(stream, paths[j + 1],
                                 i % 2 ? &theme->active_text : &theme->inactive_text);
                } else {
                    svg_add_path(stream, paths[j], &theme->white_text);
                    svg_add_path(stream, paths[j + 1], &theme->white_text);
                }
            } else {
                svg_add_path(stream, paths[j], i % 2 ? &theme->active_text : &theme->inactive_text);
            }
            svg_end(stream);
        }
    }
#else
    svg_start(stream, 0, 0, 120, 90);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            svg_begin(stream, j * 30, i * 30, 30, 30);
            if (i < 1) { // normal
                svg_add_rect(stream, 30, 30, &theme->corner_radius, 0, &theme->normal_border,
                             &theme->normal_bg);
            } else if (i < 2) { // hover
                svg_add_rect(stream, 30, 30, &theme->corner_radius, 0, &theme->hover_border,
                             j == 3 ? &theme->close_hover_bg : &theme->hover_bg);
            } else { // click
                svg_add_rect(stream, 30, 30, &theme->corner_radius, 0, &theme->click_border,
                             j == 3 ? &theme->close_click_bg : &theme->click_bg);
            }
            if (j == 3) {
                if (i < 1) {
                    svg_add_path(stream, paths[j], &theme->active_text);
                    svg_add_path(stream, paths[j + 1], &theme->active_text);
                } else {
                    svg_add_path(stream, paths[j], &theme->white_text);
                    svg_add_path(stream, paths[j + 1], &theme->white_text);
                }
            } else {
                svg_add_path(stream, paths[j], &theme->active_text);
            }
            svg_end(stream);
        }
    }
#endif
    // svg end
    svg_stop(stream);

    fclose(stream);

#if 0
    FILE *file = fopen("theme.svg", "w");
    fwrite(buffer, size, 1, file);
    fclose(file);
#endif
    theme->theme.button_svg = buffer;
}

static void theme_generate_colors(struct ukui_theme *theme)
{
    // border color
    color_to_array(theme->active_border.type == theme_value_type_color
                       ? &theme->active_border.color
                       : &theme->active_border.gradient.background,
                   theme->theme.active_border_color);
    color_to_array(theme->inactive_border.type == theme_value_type_color
                       ? &theme->inactive_border.color
                       : &theme->inactive_border.gradient.background,
                   theme->theme.inactive_border_color);

    // background color
    color_to_array(&theme->active_bg.color, theme->theme.active_bg_color);
    color_to_array(&theme->inactive_bg.color, theme->theme.inactive_bg_color);

    // text color
    color_to_array(&theme->active_text.color, theme->theme.active_text_color);
    color_to_array(&theme->inactive_text.color, theme->theme.inactive_text_color);

    // modal mask color
    color_to_array(&theme->modal_mask.color, theme->theme.modal_mask_color);
}

static bool theme_pair_parse(struct file *file, const char *key, const char *value, void *data)
{
    const char *k = strstr(key, "--");
    if (!k) {
        return false;
    }

    struct ukui_theme *theme = data;
    hash_table_insert(theme->ht, k + 2, (void *)value);

    return false;
}

static const char *get_alias(const char *str)
{
    char *var = strstr(str, "--");
    if (!var) {
        return NULL;
    }

    char *alias = malloc(strlen(str) - 6); // "var(--)" + 1
    if (!alias) {
        return NULL;
    }

    char *d = alias, *s = var + 2; // skip --
    while (*s != '\0' && *s != ')' && !isspace(*s)) {
        *d++ = tolower(*s++);
    }
    *d = '\0';

    return alias;
}

static const char *theme_color_parse(struct color *color, const char *str)
{
    if (!str || !*str) {
        return str;
    }
    char *start = strchr(str, '(');
    if (!start) {
        return str;
    }
    char *end = strchr(start, ')');
    if (!end) {
        return str;
    }
    sscanf(start, "(%u,%u,%u,%f)", &color->r, &color->g, &color->b, &color->a);
    return end + 1;
}

static bool ukui_theme_value_parse(struct ukui_theme *theme, const struct theme_key_desc *desc,
                                   const char *str)
{
    struct theme_value *value = (struct theme_value *)((char *)theme + desc->offset);

    if (strncmp(str, "linear-gradient", 15) == 0) {
        if (!(desc->type & theme_value_type_gradient)) {
            return false;
        }
        value->type = theme_value_type_gradient;

        const char *start = str + 16; // linear-gradient(
        char *end = strstr(start, "deg");
        if (end) {
            value->gradient.angle = atoi(start);
            start = end + 1;
        }
        start = theme_color_parse(&value->gradient.start, start);
        start = theme_color_parse(&value->gradient.stop, start);
        theme_color_parse(&value->gradient.background, start);
    } else if (strncmp(str, "rgba", 4) == 0) {
        if (!(desc->type & theme_value_type_color)) {
            return false;
        }
        value->type = theme_value_type_color;
        theme_color_parse(&value->color, str);
    } else if (strncmp(str, "\"", 1) == 0) {
        if (!(desc->type & theme_value_type_string)) {
            return false;
        }
        value->type = theme_value_type_string;
        value->string = str; // include ""
    } else {
        if (!(desc->type & theme_value_type_number)) {
            return false;
        }
        value->type = theme_value_type_number;
        value->number = atoi(str);
    }

    return true;
}

static const char *ukui_theme_get_value(struct ukui_theme *theme, const char *key)
{
    struct hash_entry *entry = hash_table_search(theme->ht, key);
    if (!entry || !entry->data) {
        return NULL;
    }

    const char *value = entry->data;
    if (strncmp(value, "var", 3) == 0) {
        const char *alias = get_alias(value);
        if (!alias) {
            return NULL;
        }
        value = ukui_theme_get_value(theme, alias);
        free((void *)alias);
    }

    return value;
}

static bool ukui_theme_post_parse(struct ukui_theme *theme)
{
    const struct theme_key_desc *desc;
    const char *value;

    for (uint32_t i = 0; i < THEME_KEYS; i++) {
        desc = &theme_keys[i];
        value = ukui_theme_get_value(theme, desc->name);
        if (!value) {
            kywc_log(KYWC_WARN, "%s is not found", desc->name);
            return false;
        }

        if (!ukui_theme_value_parse(theme, desc, value)) {
            kywc_log(KYWC_WARN, "%s parse failed", desc->name);
            return false;
        }
    }

    theme_generate_svgs(theme);
    theme_generate_colors(theme);

    return true;
}

static struct ukui_theme *ukui_theme_create(const char *name, enum theme_type type)
{
    struct ukui_theme *theme = calloc(1, sizeof(*theme));
    if (!theme) {
        return NULL;
    }

    theme->theme.name = strdup(name);
    theme->theme.type = type;
    wl_list_insert(&manager->themes, &theme->link);

    theme->ht = hash_table_create_string(NULL);
    hash_table_set_max_entries(theme->ht, 300);

    return theme;
}

static void ukui_theme_destroy(struct ukui_theme *theme)
{
    wl_list_remove(&theme->link);
    free((void *)theme->theme.button_svg);
    free((void *)theme->theme.name);
    free(theme);
}

static struct widget_theme *ukui_theme_load_from_file(const char *name, enum theme_type type)
{
    /* load css file from THEME_DIR */
    char path[512];
    snprintf(path, 512, "%s/k%s-%s.css", THEME_DIR, name,
             type == THEME_TYPE_DARK ? "dark" : "light");
    kywc_log(KYWC_INFO, "load widget theme from %s", path);

    struct file *file = file_open(path, ";", ":");
    if (!file) {
        return NULL;
    }

    struct ukui_theme *theme = ukui_theme_create(name, type);
    if (!theme) {
        file_close(file);
        return NULL;
    }

    file_parse(file, theme_pair_parse, theme);
    bool ok = ukui_theme_post_parse(theme);
    file_close(file);
    hash_table_destroy(theme->ht);

    if (!ok) {
        ukui_theme_destroy(theme);
        return NULL;
    }

    return &theme->theme;
}

static struct widget_theme *ukui_theme_load(const char *name, enum theme_type type)
{
    struct ukui_theme *theme;
    wl_list_for_each(theme, &manager->themes, link) {
        if (strcmp(theme->theme.name, name) == 0 && theme->theme.type == type) {
            return &theme->theme;
        }
    }

    return ukui_theme_load_from_file(name, type);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);

    struct ukui_theme *theme, *tmp;
    wl_list_for_each_safe(theme, tmp, &manager->themes, link) {
        ukui_theme_destroy(theme);
    }

    free(manager);
    manager = NULL;
}

bool ukui_theme_manager_create(struct theme_manager *theme_manager)
{
    manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->themes);
    theme_manager->load_widget_theme = ukui_theme_load;

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(theme_manager->server, &manager->server_destroy);

    return true;
}
