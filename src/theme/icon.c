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

// https://specifications.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html

#define ICONPATH "~/.icons:~/.local/share/icons:/usr/share/icons"
#define APPPATH "~/.local/share/applications:/usr/local/share/applications:/usr/share/applications"
#define PIXMAPPATH "/usr/share/pixmaps"

// TODO: XPM
// look at the mtime of the toplevel icon directories

static void get_icon_png_size(const char *path, struct icon_png *icon_png)
{
    uint32_t scale = 1;
    uint32_t width = 0, height = 0;
    char *str = strdup(path);

    char *p = strrchr(str, '/');
    if (!p) {
        goto exit;
    }
    for (int i = 0; i < 2; i++) {
        *p = '\0';
        p = strrchr(str, '/');
        if (!p) {
            goto exit;
        }
    }

    /* get width height */
    char *size_str = p + 1;
    p = strchr(size_str, 'x');
    if (!p) {
        goto exit;
    }
    *p = '\0';
    width = height = atoi(size_str);

    /* get scale */
    char *scale_str = p + 1;
    p = strchr(scale_str, '@');
    if (!p) {
        goto exit;
    }
    scale_str = p + 1;
    p = strchr(scale_str, 'x');
    if (!p) {
        goto exit;
    }
    *p = '\0';
    scale = atoi(scale_str);

exit:
    free(str);
    icon_png->scale = scale;
    icon_png->width = width;
    icon_png->height = height;
}

static void icon_create(struct icon_theme *theme, const char *path, const char *full_name)
{
    size_t index = strlen(full_name) - 4;
    const char *suffix = full_name + index;
    if (strcasecmp(suffix, ".svg") != 0 && strcasecmp(suffix, ".png") != 0 &&
        strcasecmp(suffix, ".xpm") != 0) {
        return;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    char *name = strndup(full_name, index);
    if (!name) {
        fclose(fp);
        return;
    }

    struct icon *icon = icon_theme_get_icon(theme, name, false);
    if (!icon) {
        icon = calloc(1, sizeof(struct icon));
        if (!icon) {
            fclose(fp);
            free(name);
            return;
        }
        struct icon_name *iname = calloc(1, sizeof(struct icon_name));
        if (!iname) {
            fclose(fp);
            free(icon);
            free(name);
            return;
        }
        wl_list_init(&icon->names);
        wl_list_init(&icon->buffers);
        wl_list_init(&icon->pngs);
        wl_list_insert(&icon->names, &iname->link);
        wl_list_insert(&theme->icons, &icon->link);
        iname->name = name;
    } else {
        free(name);
    }

    if (strcasecmp(suffix, ".svg") == 0) {
        if (!icon->svg) {
            fseek(fp, 0, SEEK_END);
            long size = ftell(fp);
            rewind(fp);

            icon->svg = malloc(size + 1);
            fread(icon->svg, 1, size, fp);
            icon->svg[size] = '\0';
        }
    } else if (strcasecmp(suffix, ".png") == 0) {
        struct icon_png *icon_png = malloc(sizeof(struct icon_png));
        get_icon_png_size(path, icon_png);
        icon_png->path = strdup(path);
        wl_list_insert(&icon->pngs, &icon_png->link);
    } else if (strcasecmp(suffix, ".xpm") == 0) {
        if (!icon->xpm_path) {
            icon->xpm_path = strdup(path);
        }
    }
    fclose(fp);
}

void icon_destroy(struct icon *icon)
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

    struct icon_png *icon_png, *ptmp;
    wl_list_for_each_safe(icon_png, ptmp, &icon->pngs, link) {
        wl_list_remove(&icon_png->link);
        free(icon_png->path);
        free(icon_png);
    }

    wl_list_remove(&icon->link);
    free(icon->svg);
    free(icon);
}

struct icon *icon_fallback_create(void)
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
    return icon;
}

static void desktop_load(const char *path, const char *name, void *data)
{
    struct icon_theme *theme = data;
    size_t size = strlen(name) - 8;
    if (strcasecmp(name + size, ".desktop")) {
        return;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    char *result = fscan_search_keyword(fp, "Icon");
    if (!result) {
        fclose(fp);
        return;
    }

    if (strncmp(name, result, size) == 0) {
        goto cleanup;
    }

    struct icon *icon = icon_theme_get_icon(theme, result, false);
    if (!icon) {
        goto cleanup;
    }

    struct icon_name *tmp_iname = calloc(1, sizeof(struct icon_name));
    if (!tmp_iname) {
        free(icon);
        goto cleanup;
    }
    tmp_iname->name = strndup(name, size);
    wl_list_insert(&icon->names, &tmp_iname->link);

cleanup:
    free(result);
    fclose(fp);
}

static void parents_icon_theme_load(char *parents_name, struct icon_theme *theme)
{
    struct icon_theme *parent_theme = NULL;
    char *s_ptr = NULL;
    char *parent_name = strtok_r(parents_name, ",", &s_ptr);
    while (parent_name) {
        if (strcmp(parent_name, DEFAULT_ICON_THEME_NAME)) {
            parent_theme = icon_theme_load(parent_name);
            if (parent_theme) {
                wl_list_insert(&theme->parents_theme, &parent_theme->link);
            }
        }
        parent_name = strtok_r(NULL, ",", &s_ptr);
    }
}

static void icon_theme_dir_load(char *dir_name, struct icon_theme *theme)
{
    struct icon_subdir *icon_subdir = NULL;
    char *s_ptr = NULL;
    char *subdir = strtok_r(dir_name, ",", &s_ptr);
    while (subdir) {
        size_t index = strlen(subdir) - 4;
        const char *suffix = subdir + index;
        if (strcasecmp(suffix, "apps") == 0) {
            icon_subdir = malloc(sizeof(struct icon_subdir));
            icon_subdir->subdir = fscan_build_fullname(theme->name, subdir, "");
            wl_list_insert(&theme->icons_subdir, &icon_subdir->link);
        }
        subdir = strtok_r(NULL, ",", &s_ptr);
    }
}

static void index_theme_file_load(const char *path, void *data)
{
    struct icon_theme *theme = data;
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return;
    }
    char *result = fscan_search_keyword(fp, "Inherits");
    if (result) {
        parents_icon_theme_load(result, theme);
        free(result);
    }
    rewind(fp);
    result = fscan_search_keyword(fp, "Directories");
    if (result) {
        icon_theme_dir_load(result, theme);
        free(result);
    }

    fclose(fp);
}

static void icon_load_index_theme_file(struct icon_theme *theme)
{
    fscan_file(ICONPATH, theme->name, "index.theme", index_theme_file_load, theme);
}

static void icon_load_desktop(struct icon_theme *theme)
{
    fscan_start(APPPATH, "", desktop_load, theme);
}

static void icon_load(const char *path, const char *full_name, void *data)
{
    struct icon_theme *theme = data;
    icon_create(theme, path, full_name);
}

static void icon_load_theme(struct icon_theme *theme)
{
    struct icon_subdir *icon_subdir;
    wl_list_for_each(icon_subdir, &theme->icons_subdir, link) {
        fscan_start(ICONPATH, icon_subdir->subdir, icon_load, theme);
    }

    fscan_start(PIXMAPPATH, "", icon_load, theme);
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

    wl_list_init(&theme->parents_theme);
    wl_list_init(&theme->icons_subdir);
    icon_load_index_theme_file(theme);
    wl_list_init(&theme->icons);
    icon_load_theme(theme);

    if (wl_list_empty(&theme->icons)) {
        free(theme->name);
        theme->name = strdup("default");
    }

    /* load all desktop files and get icon name */
    icon_load_desktop(theme);

    return theme;
}

void icon_theme_destroy(struct icon_theme *theme)
{
    if (!theme) {
        return;
    }

    struct icon_theme *parent_theme, *parent_tmp;
    wl_list_for_each_safe(parent_theme, parent_tmp, &theme->parents_theme, link) {
        icon_theme_destroy(parent_theme);
    }

    struct icon *icon, *icon_tmp;
    wl_list_for_each_safe(icon, icon_tmp, &theme->icons, link) {
        icon_destroy(icon);
    }

    struct icon_subdir *subdir, *dir_tmp;
    wl_list_for_each_safe(subdir, dir_tmp, &theme->icons_subdir, link) {
        free(subdir->subdir);
        free(subdir);
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

struct icon *icon_theme_get_icon(struct icon_theme *theme, const char *name, bool search_parents)
{
    struct icon *icon;
    wl_list_for_each(icon, &theme->icons, link) {
        if (icon_check_name(icon, name)) {
            return icon;
        }
    }

    if (search_parents) {
        struct icon_theme *parents_theme;
        wl_list_for_each(parents_theme, &theme->parents_theme, link) {
            icon = icon_theme_get_icon(parents_theme, name, true);
            if (icon) {
                return icon;
            }
        }
    }

    return NULL;
}
