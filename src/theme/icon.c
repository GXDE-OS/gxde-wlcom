// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-util.h>
#include <wlr/types/wlr_buffer.h>

#include "theme_p.h"
#include "unknown_svg_src.h"
#include "util/fscan.h"

// https://specifications.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html

// TODO: XPM

static char *search_digit_from_str(const char *str)
{
    const char *digit_str = NULL;
    int len = 0;
    size_t i;

    for (i = 0; i < strlen(str) + 1; i++) {
        if (isdigit(str[i])) {
            digit_str = &str[i];
            break;
        }
    }
    if (!digit_str) {
        return NULL;
    }

    for (i = 0; i < strlen(digit_str); i++) {
        if (!isdigit(digit_str[i])) {
            len = i;
            break;
        }
    }

    return strndup(digit_str, len);
}

static void get_icon_png_size(const char *path, struct icon_png *icon_png)
{
    size_t i = 0;
    uint32_t scale = 1;
    uint32_t width = 0, height = 0;
    char *width_str = NULL;
    char *scale_str = NULL;
    const char *str_type1 = NULL;
    const char *str_type2 = NULL;
    const char *p = NULL;

    for (i = 0; i < strlen(path); i++) {
        if (path[i] == '@') {
            str_type1 = &path[i];
            break;
        } else if (isdigit(path[i])) {
            str_type2 = &path[i];
            break;
        }
    }

    if (str_type1) {
        scale_str = search_digit_from_str(str_type1);
        if (!scale_str) {
            goto exit;
        }
        scale = atoi(scale_str);

        p = str_type1 + strlen(scale_str);
        free(scale_str);
        width_str = search_digit_from_str(p);
        if (!width_str) {
            goto exit;
        }
        width = height = atoi(width_str);
        free(width_str);
    } else if (str_type2) {
        width_str = search_digit_from_str(str_type2);
        if (!width_str) {
            goto exit;
        }
        width = height = atoi(width_str);

        for (i = strlen(width_str); i < strlen(str_type2); i++) {
            if (str_type2[i] == '@') {
                p = &str_type2[i];
                break;
            }
        }
        free(width_str);
        if (p) {
            scale_str = search_digit_from_str(p);
            if (!scale_str) {
                goto exit;
            }
            scale = atoi(scale_str);
            free(scale_str);
        }
    } else {
        goto exit;
    }

exit:
    icon_png->scale = scale;
    icon_png->width = width;
    icon_png->height = height;
}

struct icon *icon_create(struct icon_theme *theme, const char *path, const char *full_name)
{
    size_t index = strlen(full_name) - 4;
    const char *suffix = full_name + index;
    if (strcasecmp(suffix, ".svg") != 0 && strcasecmp(suffix, ".png") != 0 &&
        strcasecmp(suffix, ".xpm") != 0) {
        return NULL;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return NULL;
    }

    char *name = strndup(full_name, index);
    if (!name) {
        fclose(fp);
        return NULL;
    }

    struct icon *icon = NULL;
    if (theme) {
        icon = icon_theme_get_icon(theme, name, false);
    }

    if (!icon) {
        icon = calloc(1, sizeof(struct icon));
        if (!icon) {
            fclose(fp);
            free(name);
            return NULL;
        }
        icon->name = name;
        wl_list_init(&icon->buffers);
        wl_list_init(&icon->pngs);
        if (theme) {
            wl_list_insert(&theme->icons, &icon->link);
        }
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

    return icon;
}

void icon_destroy(struct icon *icon)
{
    struct icon_buffer *buf, *tmp;
    wl_list_for_each_safe(buf, tmp, &icon->buffers, link) {
        wl_list_remove(&buf->link);
        wlr_buffer_drop(buf->buffer);
        free(buf);
    }

    struct icon_png *icon_png, *ptmp;
    wl_list_for_each_safe(icon_png, ptmp, &icon->pngs, link) {
        wl_list_remove(&icon_png->link);
        free(icon_png->path);
        free(icon_png);
    }

    wl_list_remove(&icon->link);
    free(icon->name);
    free(icon->xpm_path);
    free(icon->svg);
    free(icon);
}

struct icon *icon_fallback_create(void)
{
    struct icon *icon = calloc(1, sizeof(struct icon));
    if (!icon) {
        return NULL;
    }

    wl_list_init(&icon->link);
    wl_list_init(&icon->pngs);
    wl_list_init(&icon->buffers);

    icon->name = strdup("fallback");
    icon->svg = strdup(unknown_svg_src);

    return icon;
}

static char *get_exec_name_from_path(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }

    const char *p = path;
    while (*p == ' ') {
        p++;
    }

    const char *space = strchr(p, ' ');
    if (!space) {
        space = p + strlen(p);
    }
    size_t end = space - p;
    size_t first = 0;
    for (int i = end; i >= 0; i--) {
        if (p[i] == '/') {
            first = i + 1;
            break;
        }
    }
    size_t size = end - first;

    return strndup(&p[first], size);
}

static void desktop_load(const char *path, const char *name, void *data)
{
    struct wl_list *desktop_infos = data;
    size_t size = strlen(name) - 8;
    if (strcasecmp(name + size, ".desktop")) {
        return;
    }
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    struct desktop_info *info = malloc(sizeof(struct desktop_info));
    if (!info) {
        goto close;
    }
    info->app_id = strndup(name, size);
    if (!info->app_id) {
        free(info);
        goto close;
    }
    info->icon_name = fscan_search_keyword(fp, "Icon");
    if (!info->icon_name || !*info->icon_name) {
        free(info->icon_name);
        free(info->app_id);
        free(info);
        goto close;
    }
    info->startup_name = fscan_search_keyword(fp, "StartupWMClass");
    char *exec_path = fscan_search_keyword(fp, "Exec");
    info->exec_name = get_exec_name_from_path(exec_path);
    free(exec_path);

    wl_list_insert(desktop_infos, &info->link);

close:
    fclose(fp);
}

static void parents_icon_theme_load(char *parents_name, struct icon_theme *theme)
{
    struct icon_theme *parent_theme = NULL;
    char *s_ptr = NULL;
    char *parent_name = strtok_r(parents_name, ",", &s_ptr);
    while (parent_name) {
        if (strcmp(parent_name, FALLBACK_ICON_THEME_NAME)) {
            parent_theme = icon_theme_load(parent_name);
            if (parent_theme) {
                wl_list_insert(&theme->parents, &parent_theme->link);
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
        if (strstr(subdir, "apps") || strstr(subdir, "categories") || strstr(subdir, "status")) {
            icon_subdir = malloc(sizeof(struct icon_subdir));
            icon_subdir->subdir = fscan_build_fullname(theme->name, subdir, "");
            wl_list_insert(&theme->subdirs, &icon_subdir->link);
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

void icon_load_desktop(struct wl_list *desktop_infos)
{
    fscan_start(APPPATH, "", desktop_load, desktop_infos);
}

void desktop_infos_destroy(struct wl_list *desktop_infos)
{
    if (!desktop_infos) {
        return;
    }

    struct desktop_info *desktop_info, *tmp;
    wl_list_for_each_safe(desktop_info, tmp, desktop_infos, link) {
        wl_list_remove(&desktop_info->link);
        free(desktop_info->app_id);
        free(desktop_info->icon_name);
        free(desktop_info->exec_name);
        free(desktop_info->startup_name);
        free(desktop_info);
    }
}

void pixmaps_icon_destroy(struct wl_list *pixmap_icons)
{
    struct icon *icon, *tmp;
    wl_list_for_each_safe(icon, tmp, pixmap_icons, link) {
        icon_destroy(icon);
    }
}

static void pixmaps_path_load(const char *path, const char *name, void *data)
{
    struct wl_list *pixmap_icons = data;
    size_t index = strlen(name) - 4;
    const char *suffix = name + index;
    if (strcasecmp(suffix, ".svg") != 0 && strcasecmp(suffix, ".png") != 0 &&
        strcasecmp(suffix, ".xpm") != 0) {
        return;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    struct icon *icon_tmp = NULL, *icon = NULL;
    wl_list_for_each(icon_tmp, pixmap_icons, link) {
        if (strncmp(icon_tmp->name, name, index) == 0) {
            icon = icon_tmp;
            break;
        }
    }
    if (!icon) {
        icon = calloc(1, sizeof(struct icon));
        if (!icon) {
            fclose(fp);
            return;
        }
        icon->name = strndup(name, index);
        wl_list_init(&icon->link);
        wl_list_init(&icon->buffers);
        wl_list_init(&icon->pngs);
        wl_list_insert(pixmap_icons, &icon->link);
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

void icon_load_pixmaps_path(struct wl_list *pixmap_icons)
{
    fscan_start(PIXMAPPATH, "", pixmaps_path_load, pixmap_icons);
}

static void icon_load(const char *path, const char *full_name, void *data)
{
    struct icon_theme *theme = data;
    icon_create(theme, path, full_name);
}

static void icon_load_theme(struct icon_theme *theme)
{
    struct icon_subdir *icon_subdir;
    wl_list_for_each(icon_subdir, &theme->subdirs, link) {
        fscan_start(ICONPATH, icon_subdir->subdir, icon_load, theme);
    }
}

struct icon_theme *icon_theme_load(const char *name)
{
    struct icon_theme *theme = calloc(1, sizeof(struct icon_theme));
    if (!theme) {
        return NULL;
    }
    if (!name) {
        name = DEFAULT_ICON_THEME_NAME;
    }

    theme->name = strdup(name);
    if (!theme->name) {
        free(theme);
        return NULL;
    }

    wl_list_init(&theme->parents);
    wl_list_init(&theme->subdirs);
    icon_load_index_theme_file(theme);
    wl_list_init(&theme->icons);
    icon_load_theme(theme);

    if (wl_list_empty(&theme->icons)) {
        free(theme->name);
        theme->name = strdup(DEFAULT_ICON_THEME_NAME);
    }

    return theme;
}

void icon_theme_destroy(struct icon_theme *theme)
{
    if (!theme) {
        return;
    }

    struct icon_theme *parent_theme, *parent_tmp;
    wl_list_for_each_safe(parent_theme, parent_tmp, &theme->parents, link) {
        icon_theme_destroy(parent_theme);
    }

    struct icon *icon, *icon_tmp;
    wl_list_for_each_safe(icon, icon_tmp, &theme->icons, link) {
        icon_destroy(icon);
    }

    struct icon_subdir *subdir, *dir_tmp;
    wl_list_for_each_safe(subdir, dir_tmp, &theme->subdirs, link) {
        wl_list_remove(&subdir->link);
        free(subdir->subdir);
        free(subdir);
    }

    free(theme->name);
    free(theme);
}

struct icon *icon_theme_get_icon(struct icon_theme *theme, const char *name, bool search_parents)
{
    struct icon *icon;
    wl_list_for_each(icon, &theme->icons, link) {
        if (strcasecmp(icon->name, name) == 0) {
            return icon;
        }
    }

    if (search_parents) {
        struct icon_theme *parent;
        wl_list_for_each(parent, &theme->parents, link) {
            icon = icon_theme_get_icon(parent, name, true);
            if (icon) {
                return icon;
            }
        }
    }

    return NULL;
}

bool icon_need_reload(const char *path, struct icon_theme *theme, time_t threshold)
{
    time_t mtime = 0;
    bool ret = false;

    if (!theme) {
        mtime = fscan_get_latest_mtime(path, "");
        ret = mtime > threshold ? true : false;
        return ret;
    }

    mtime = fscan_get_latest_mtime(path, theme->name);
    ret = mtime > threshold ? true : false;
    if (ret) {
        return ret;
    }
    struct icon_subdir *tmp_subdir;
    wl_list_for_each(tmp_subdir, &theme->subdirs, link) {
        mtime = fscan_get_latest_mtime(path, tmp_subdir->subdir);
        ret = mtime > threshold ? true : false;
        if (ret) {
            return ret;
        }
    }

    struct icon_theme *tmp_theme;
    wl_list_for_each(tmp_theme, &theme->parents, link) {
        mtime = fscan_get_latest_mtime(path, tmp_theme->name);
        ret = mtime > threshold ? true : false;
        if (ret) {
            return ret;
        }
        ret = icon_need_reload(path, tmp_theme, threshold);
        if (ret) {
            return ret;
        }
    }

    return ret;
}
