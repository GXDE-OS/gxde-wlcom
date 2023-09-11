// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>

#include <wayland-util.h>
#include <wlr/types/wlr_buffer.h>

#include "theme_p.h"
#include "unknown_svg_src.h"
#include "util/fscan.h"

#define ICONPATH "/usr/share/icons:~/.icons:~/.local/share/icons"
#define APPPATH "/usr/share/applications:/usr/local/share/applications:~/.local/share/applications"

static void icon_create(struct icon_theme *theme, FILE *fp, char *name)
{
    size_t index = strlen(name) - 4;
    if (strcasecmp(name + index, ".svg")) {
        return;
    }

    struct icon *icon = calloc(1, sizeof(struct icon));
    if (!icon) {
        return;
    }

    struct icon_name *iname = calloc(1, sizeof(struct icon_name));
    if (!iname) {
        free(icon);
        return;
    }

    name[index] = '\0';
    iname->name = strdup(name);
    wl_list_init(&icon->names);
    wl_list_insert(&icon->names, &iname->link);

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    icon->svg = malloc(size + 1);
    fread(icon->svg, 1, size, fp);
    icon->svg[size] = '\0';

    wl_list_init(&icon->buffers);
    wl_list_insert(&theme->icons, &icon->link);
}

static void icon_load(FILE *fp, char *name, void *data)
{
    struct icon_theme *theme = data;
    icon_create(theme, fp, name);
}

static void icon_load_theme(struct icon_theme *theme)
{
    char *subdir = fscan_build_fullname(theme->name, "scalable", "apps");
    fscan_start(ICONPATH, subdir, icon_load, theme);
    free(subdir);
}

static void icon_destroy(struct icon *icon)
{
    struct icon_buffer *buf, *tmp;
    wl_list_for_each_safe(buf, tmp, &icon->buffers, link) {
        wl_list_remove(&buf->link);
        wlr_buffer_drop(buf->buffer);
        free(buf);
    }

    struct icon_name *iname, *itmp;
    wl_list_for_each_safe(iname, itmp, &icon->names, link) {
        wl_list_remove(&iname->link);
        free(iname->name);
        free(iname);
    }

    wl_list_remove(&icon->link);
    free(icon->svg);
    free(icon);
}

static struct icon *icon_fallback_create(struct icon_theme *theme)
{
    struct icon *icon = calloc(1, sizeof(struct icon));
    if (!icon) {
        return NULL;
    }

    struct icon_name *iname = calloc(1, sizeof(struct icon_name));
    if (!iname) {
        free(icon);
        return NULL;
    }

    iname->name = strdup("fallback");
    wl_list_init(&icon->names);
    wl_list_insert(&icon->names, &iname->link);
    icon->svg = strdup(unknown_svg_src);

    wl_list_init(&icon->buffers);
    wl_list_insert(&theme->icons, &icon->link);
    return icon;
}

static void desktop_load(FILE *fp, char *name, void *data)
{
    size_t size = strlen(name) - 8;
    if (strcasecmp(name + size, ".desktop")) {
        return;
    }

    /* get Icon entry in desktop file */
    char *line = NULL;
    size_t line_size = 0;
    char *result = NULL;
    char *p;

    while (getline(&line, &line_size, fp) >= 0) {
        if (strncmp(line, "Icon", 4)) {
            continue;
        }

        p = line + 4;
        while (*p == ' ') {
            p++;
        }
        if (*p != '=') {
            continue;
        }

        p++;
        while (*p == ' ') {
            p++;
        }

        if (*p == '/') {
            continue;
        }

        result = malloc(strlen(p) + 1);
        if (!result) {
            break;
        }

        char *r = result;
        while (*p && *p != '\r' && *p != '\t' && *p != '\n') {
            *r++ = *p++;
        }
        *r++ = '\0';

        break;
    }

    free(line);

    if (!result || strncmp(name, result, size) == 0) {
        free(result);
        return;
    }

    struct icon_theme *theme = data;
    struct icon *icon = icon_theme_get_icon(theme, result);
    free(result);
    if (!icon) {
        return;
    }

    struct icon_name *iname = calloc(1, sizeof(struct icon_name));
    if (!iname) {
        return;
    }

    name[size] = '\0';
    iname->name = strdup(name);
    wl_list_insert(&icon->names, &iname->link);
}

static void icon_load_desktop(struct icon_theme *theme)
{
    fscan_start(APPPATH, "", desktop_load, theme);
}

struct icon_theme *icon_theme_load(const char *name)
{
    struct icon_theme *theme = calloc(1, sizeof(struct icon_theme));
    if (!theme) {
        return NULL;
    }
    if (!name) {
        name = "default";
    }

    theme->name = strdup(name);
    if (!theme->name) {
        free(theme);
        return NULL;
    }

    wl_list_init(&theme->icons);
    icon_load_theme(theme);

    if (wl_list_empty(&theme->icons)) {
        free(theme->name);
        theme->name = strdup("default");
    }

    /* load all desktop files and get icon name */
    icon_load_desktop(theme);

    struct icon *fallback = icon_fallback_create(theme);
    theme->fallback = fallback;

    return theme;
}

void icon_theme_destroy(struct icon_theme *theme)
{
    struct icon *icon, *tmp;
    wl_list_for_each_safe(icon, tmp, &theme->icons, link) {
        icon_destroy(icon);
    }

    free(theme->name);
    free(theme);
}

static bool icon_check_name(struct icon *icon, const char *name)
{
    struct icon_name *iname;
    wl_list_for_each(iname, &icon->names, link) {
        if (strcmp(name, iname->name) == 0) {
            return true;
        }
    }
    return false;
}

struct icon *icon_theme_get_icon(struct icon_theme *theme, const char *name)
{
    struct icon *icon;
    wl_list_for_each(icon, &theme->icons, link) {
        if (icon_check_name(icon, name)) {
            return icon;
        }
    }
    return NULL;
}
