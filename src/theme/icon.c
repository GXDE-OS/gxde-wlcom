// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <sys/inotify.h>
#include <wlr/types/wlr_buffer.h>

#include <kywc/log.h>

#include "painter.h"
#include "server.h"
#include "theme_p.h"
#include "util/file.h"
#include "util/hash_table.h"
#include "util/string.h"

#define ICONPATH "~/.icons:~/.local/share/icons:/usr/share/icons:" EXTRA_ICON_PATH
#define APPPATH                                                                                    \
    "~/.local/share/applications:/usr/local/share/applications:/usr/share/applications:/etc/xdg/"  \
    "autostart:" EXTRA_APPS_PATH
#define PIXMAPPATH "/usr/share/pixmaps"

// TODO: IN_DELETE support

enum icon_type {
    ICON_TYPE_UNKOWN = 0,
    ICON_TYPE_SVG = 1 << 0,
    ICON_TYPE_PNG = 1 << 1,
    ICON_TYPE_XPM = 1 << 2,
    ICON_TYPE_ICO = 1 << 3,
};

struct icon_buffer {
    struct wl_list link;
    struct wlr_buffer *buffer;
    int size;
    float scale;
};

struct icon_entry {
    struct wl_list link;
    char *path;
    int size, scale;
};

struct icon {
    struct icon_theme *theme;
    const char *name;
    struct wl_signal destroy;
    struct wl_list buffers;
    struct wl_list entries;
    char *svg_path, *svg_data;
    char *png_path; // no size hint
    char *xpm_path;
    char *ico_path;
    uint32_t types;
    uint32_t hash;
};

struct icon_theme {
    struct wl_list link;
    struct icon_manager *manager;
    const char *name; // may be NULL
    size_t name_size;
    struct wl_array parents;
    struct hash_table *icons;
};

struct desktop {
    struct wl_list link;
    const char *name;
    char *icon_name;
    char *startup_name;
    char *app_name;
    char *exec_name;
};

struct icon_pair {
    const char *name;
    struct icon *icon;
    // remove from hash table if icon destroyed
    struct wl_listener icon_destroy;
    uint32_t hash;
};

struct watch_fd {
    char *path;
    int fd;

    void (*handler)(struct watch_fd *wd, const struct inotify_event *event, void *data);
    void *data;
};

struct icon_manager {
    struct server *server;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;

    struct wl_list themes;
    struct wl_list desktops;
    struct hash_table *icon_pairs;

    struct icon_theme *current;
    struct icon_theme *fallback;
    // special theme for pixmap icons
    struct icon_theme *pixmap;
    // specific path icons
    struct icon_theme *specific;

    struct {
        struct wl_event_source *source;
        struct hash_table *wds;
        int event_fd;
    } watcher;
};

static struct icon_theme *icon_theme_get_or_create(struct icon_manager *manager, const char *name);

static void icon_manager_add_watcher_fd(struct icon_manager *manager, int fd, const char *path,
                                        void (*handler)(struct watch_fd *wd,
                                                        const struct inotify_event *event,
                                                        void *data),
                                        void *data);

static size_t desktop_verify_name(const char *name)
{
    int size = (int)strlen(name) - 8;
    if (size > 0 && strcmp(name + size, ".desktop") == 0) {
        return size;
    }
    return 0;
}

static struct desktop *icon_manager_add_desktop(struct icon_manager *manager, const char *fullpath,
                                                const char *name, size_t size)
{
    struct desktop *desktop;
    // no need to check for duplicate desktops
#if 0
    wl_list_for_each(desktop, &manager->desktops, link) {
        if (strncmp(desktop->name, name, size) == 0) {
            return false;
        }
    }
#endif
    desktop = calloc(1, sizeof(*desktop));
    if (!desktop) {
        return false;
    }

    struct file *file = file_open(fullpath, "\n", "=");
    if (!file) {
        free(desktop);
        return false;
    }

    desktop->icon_name = file_get_value_copy(file, "Icon");
    if (!desktop->icon_name || !*desktop->icon_name) {
        free(desktop->icon_name);
        free(desktop);
        file_close(file);
        return false;
    }

    desktop->name = strndup(name, size);
    desktop->startup_name = file_get_value_copy(file, "StartupWMClass");
    desktop->app_name = file_get_value_copy(file, "Name");
    desktop->exec_name = file_get_value_copy(file, "Exec");
    if (desktop->exec_name) {
        string_strip_path(desktop->exec_name);
    }

    wl_list_insert(manager->desktops.prev, &desktop->link);
    file_close(file);
    return false;
}

static void handle_desktop(struct watch_fd *wd, const struct inotify_event *event, void *data)
{
    size_t size = desktop_verify_name(event->name);
    if (size == 0) {
        return;
    }

    char *fullpath = string_join_path(wd->path, NULL, event->name);
    if (fullpath) {
        icon_manager_add_desktop(data, fullpath, event->name, size);
        free(fullpath);
    }
}

static bool load_desktop(const char *fullpath, const char *subpath, const char *name, void *data)
{
    struct icon_manager *manager = data;

    if (name) {
        size_t size = desktop_verify_name(name);
        if (size > 0) {
            icon_manager_add_desktop(manager, fullpath, name, size);
        }
        return false;
    }

    if (!manager->watcher.source) {
        return false;
    }

    int wd = inotify_add_watch(manager->watcher.event_fd, fullpath, IN_CREATE | IN_MOVED_TO);
    if (wd >= 0) {
        icon_manager_add_watcher_fd(manager, wd, fullpath, handle_desktop, manager);
    }
    return false;
}

static char *icon_theme_is_valid(const char *name)
{
    char *realpath = string_expand_path(ICONPATH);
    if (!realpath) {
        return NULL;
    }

    size_t len = 0;
    char **paths = string_split(realpath, ":", &len);
    if (!paths) {
        free(realpath);
        return NULL;
    }

    char *fullpath = NULL;
    for (size_t i = 0; i < len; i++) {
        fullpath = string_join_path(paths[i], name, "index.theme");
        if (!fullpath) {
            continue;
        }
        if (file_exists(fullpath)) {
            break;
        }
        free(fullpath);
        fullpath = NULL;
    }

    string_free_split(paths);
    free(realpath);

    return fullpath;
}

static bool icon_theme_load_parents(struct icon_theme *theme, const char *path)
{
    struct file *file = file_open(path, "\n", "=");
    if (!file) {
        return false;
    }

    const char *parents = file_get_value(file, "Inherits");
    if (!parents) {
        file_close(file);
        return false;
    }

    size_t len = 0;
    char **split = string_split(parents, ",", &len);

    for (size_t i = 0; i < len; i++) {
        char *parent = split[i];
        // skip fallback theme
        if (strcmp(parent, FALLBACK_ICON_THEME_NAME) == 0) {
            continue;
        }
        struct icon_theme *parent_theme = icon_theme_get_or_create(theme->manager, parent);
        if (!parent_theme) {
            continue;
        }
        struct icon_theme **ptr = wl_array_add(&theme->parents, sizeof(*ptr));
        *ptr = parent_theme;
    }

    string_free_split(split);
    file_close(file);
    return true;
}

static enum icon_type icon_get_type(const char *name, char **body)
{
    int index = (int)strlen(name) - 4;
    if (index <= 0) {
        return ICON_TYPE_UNKOWN;
    }

    enum icon_type type = ICON_TYPE_UNKOWN;
    const char *suffix = name + index;

    if (strcasecmp(suffix, ".svg") == 0) {
        type = ICON_TYPE_SVG;
    } else if (strcasecmp(suffix, ".png") == 0) {
        type = ICON_TYPE_PNG;
    } else if (strcasecmp(suffix, ".xpm") == 0) {
        type = ICON_TYPE_XPM;
    } else if (strcasecmp(suffix, ".ico") == 0) {
        type = ICON_TYPE_ICO;
    } else {
        return type;
    }

    if (body) {
        *body = *name == '/' ? strdup(name) : strndup(name, index);
    }

    return type;
}

static bool icon_add_image(struct icon *icon, const char *path, enum icon_type type)
{
    if (type == ICON_TYPE_UNKOWN) {
        return false;
    }
    if (type != ICON_TYPE_PNG) {
        // skip if already have this type image
        if (icon->types & type) {
            return false;
        }
        if (type == ICON_TYPE_SVG) {
            icon->svg_path = strdup(path);
        } else if (type == ICON_TYPE_XPM) {
            icon->xpm_path = strdup(path);
        } else if (type == ICON_TYPE_ICO) {
            icon->ico_path = strdup(path);
        }
        icon->types |= type;
        return true;
    }

    // png image left
    int size = 0, scale = 1;
    const char *subpath = NULL;
    if (icon->theme->name) {
        subpath = strstr(path, icon->theme->name);
        if (subpath) { // skip name and  '/'
            subpath += icon->theme->name_size + 1;
        }
    };
    if (subpath && sscanf(subpath, "%dx%*d@%d", &size, &scale) > 0) {
        struct icon_entry *entry;
        wl_list_for_each(entry, &icon->entries, link) {
            if (entry->size == size && entry->scale == scale) {
                return false;
            }
        }
        entry = calloc(1, sizeof(*entry));
        if (entry) {
            entry->size = size;
            entry->scale = scale;
            entry->path = strdup(path);
            wl_list_insert(&icon->entries, &entry->link);
        }
    } else {
        if (icon->types & type) {
            return false;
        }
        icon->png_path = strdup(path);
    }

    icon->types |= type;
    return true;
}

static void icon_destroy(struct icon *icon)
{
    if (!icon) {
        return;
    }

    wl_signal_emit_mutable(&icon->destroy, NULL);

    struct icon_buffer *buf, *tmp;
    wl_list_for_each_safe(buf, tmp, &icon->buffers, link) {
        wl_list_remove(&buf->link);
        wlr_buffer_drop(buf->buffer);
        free(buf);
    }

    struct icon_entry *entry, *entry_tmp;
    wl_list_for_each_safe(entry, entry_tmp, &icon->entries, link) {
        wl_list_remove(&entry->link);
        free(entry->path);
        free(entry);
    }

    hash_table_remove_hash(icon->theme->icons, icon->hash, icon->name);

    free((void *)icon->name);
    free(icon->xpm_path);
    free(icon->svg_path);
    free(icon->png_path);
    free(icon->svg_data);
    free(icon);
}

static struct icon *icon_theme_add_icon(struct icon_theme *theme, const char *fullpath,
                                        enum icon_type type, const char *name)
{
    struct icon *icon = NULL;
    struct hash_entry *entry = hash_table_search(theme->icons, name);
    if (entry) {
        icon = entry->data;
    }

    if (!icon) {
        icon = calloc(1, sizeof(*icon));
        if (!icon) {
            return NULL;
        }

        wl_list_init(&icon->buffers);
        wl_list_init(&icon->entries);
        wl_signal_init(&icon->destroy);

        icon->name = name;
        icon->theme = theme;
        // store the hash to skip duplicate hash calculation
        entry = hash_table_insert(theme->icons, icon->name, icon);
        icon->hash = entry->hash;
    } else {
        // name is not used if icon is already created
        free((void *)name);
    }

    icon_add_image(icon, fullpath, type);

    return icon;
}

static void handle_icon(struct watch_fd *wd, const struct inotify_event *event, void *data)
{
    char *icon_name = NULL;
    enum icon_type type = icon_get_type(event->name, &icon_name);
    if (type == ICON_TYPE_UNKOWN) {
        return;
    }

    char *fullpath = string_join_path(wd->path, NULL, event->name);
    if (!fullpath) {
        free(icon_name);
        return;
    }

    struct icon_theme *theme = data;
    icon_theme_add_icon(theme, fullpath, type, icon_name);
    free(fullpath);
}

static bool load_icon(const char *fullpath, const char *subpath, const char *name, void *data)
{
    struct icon_theme *theme = data;

    if (name) {
        char *icon_name = NULL;
        enum icon_type type = icon_get_type(name, &icon_name);
        if (type != ICON_TYPE_UNKOWN) {
            icon_theme_add_icon(theme, fullpath, type, icon_name);
        }
        return false;
    }

    if (subpath) {
        // filter subpath
        int size, scale = 1;
        int num = sscanf(subpath, "%dx%*d@%d", &size, &scale);
#if 0
        if (num >= 1 && (size < 24 || size > 128)) {
            return true;
        }
#endif
        if (num >= 1 || strncmp(subpath, "scalable", 8) == 0) {
            char *slash = strchr(subpath, '/');
            if (slash && !strstr(slash, "apps") && !strstr(slash, "categories") &&
                !strstr(slash, "status")) {
                return true;
            }
        }
    }

    // !name && !subpath
    struct icon_manager *manager = theme->manager;
    if (!manager->watcher.source) {
        return false;
    }

    // monitor pixmap and fallback icon theme
    if (!theme->name || strcmp(theme->name, FALLBACK_ICON_THEME_NAME) == 0) {
        int wd = inotify_add_watch(manager->watcher.event_fd, fullpath, IN_CREATE | IN_MOVED_TO);
        if (wd >= 0) {
            icon_manager_add_watcher_fd(manager, wd, fullpath, handle_icon, theme);
        }
    }

    return false;
}

static struct icon_theme *icon_theme_create_empty(struct icon_manager *manager, const char *name)
{
    struct icon_theme *theme = calloc(1, sizeof(*theme));
    if (!theme) {
        return NULL;
    }

    wl_list_init(&theme->link);
    wl_array_init(&theme->parents);

    theme->manager = manager;
    theme->icons = hash_table_create_string(NULL);
    hash_table_set_max_entries(theme->icons, name ? 2000 : 200);

    if (name) {
        theme->name = strdup(name);
        theme->name_size = strlen(name);
        wl_list_insert(&manager->themes, &theme->link);
    }

    return theme;
}

static struct icon_theme *icon_theme_get_or_create(struct icon_manager *manager, const char *name)
{
    struct icon_theme *theme;
    wl_list_for_each(theme, &manager->themes, link) {
        if (strcmp(name, theme->name) == 0) {
            return theme;
        }
    }

    // check theme is valid
    char *theme_path = icon_theme_is_valid(name);
    if (!theme_path) {
        return NULL;
    }

    theme = icon_theme_create_empty(manager, name);
    if (!theme) {
        free(theme_path);
        return NULL;
    }

    icon_theme_load_parents(theme, theme_path);
    // only load the first theme in ICONPATH
    theme_path[strlen(theme_path) - strlen("index.theme") - 1] = '\0';
    file_for_each(theme_path, NULL, load_icon, theme);
    free(theme_path);

    return theme;
}

static void icon_theme_destroy(struct icon_theme *theme)
{
    if (!theme) {
        return;
    }

    struct hash_entry *entry;
    hash_table_for_each(entry, theme->icons) {
        icon_destroy(entry->data);
    }
    hash_table_destroy(theme->icons);

    wl_list_remove(&theme->link);
    wl_array_release(&theme->parents);

    free((void *)theme->name);
    free(theme);
}

static struct icon *icon_theme_get_icon(struct icon_theme *theme, const char *name)
{
    if (!theme) {
        return NULL;
    }

    struct hash_entry *entry = hash_table_search(theme->icons, name);
    if (entry && entry->data) {
        return entry->data;
    }

    struct icon_theme **parent;
    wl_array_for_each(parent, &theme->parents) {
        struct icon *icon = icon_theme_get_icon(*parent, name);
        if (icon) {
            return icon;
        }
    }

    return NULL;
}

static struct icon *icon_manager_get_icon(struct icon_manager *manager, const char *name)
{
    // try specific path icon
    if (*name == '/') {
        if (!file_exists(name)) {
            return NULL;
        }
        char *icon_name = NULL;
        enum icon_type type = icon_get_type(name, &icon_name);
        if (type == ICON_TYPE_UNKOWN) {
            return NULL;
        }
        return icon_theme_add_icon(manager->specific, name, type, icon_name);
    }

    struct icon *icon = NULL;

    if (manager->current) {
        icon = icon_theme_get_icon(manager->current, name);
    }
    if (!icon) {
        icon = icon_theme_get_icon(manager->fallback, name);
    }
    if (!icon) {
        icon = icon_theme_get_icon(manager->pixmap, name);
    }

    return icon;
}

static const char *get_icon_name_from_desktop(struct icon_manager *manager, const char *name)
{
    // TODO: optimize this
    struct desktop *desktop;
    wl_list_for_each(desktop, &manager->desktops, link) {
        if (strcasecmp(desktop->name, name) == 0 ||
            (desktop->startup_name && strcasecmp(desktop->startup_name, name) == 0) ||
            (desktop->app_name && strcasecmp(desktop->app_name, name) == 0)) {
            return desktop->icon_name;
        }
    }
    return NULL;
}

static const char *fuzzy_get_icon_name_from_desktop(struct icon_manager *manager, const char *name)
{
    struct desktop *desktop, *available_desktop = NULL;
    wl_list_for_each(desktop, &manager->desktops, link) {
        if (desktop->exec_name && strcasecmp(desktop->exec_name, name) == 0) {
            // abandon this search if find multiple results
            if (available_desktop) {
                available_desktop = NULL;
                break;
            }
            available_desktop = desktop;
        }
    }
    return available_desktop ? available_desktop->icon_name : NULL;
}

static void pair_handle_icon_destroy(struct wl_listener *listener, void *data)
{
    struct icon_pair *pair = wl_container_of(listener, pair, icon_destroy);
    wl_list_remove(&pair->icon_destroy.link);
    struct hash_table *table = pair->icon->theme->manager->icon_pairs;
    hash_table_remove_hash(table, pair->hash, pair->name);
    free((void *)pair->name);
    free(pair);
}

static void icon_manager_add_icon_pair(struct icon_manager *manager, const char *name,
                                       struct icon *icon)
{
    struct icon_pair *pair = calloc(1, sizeof(*pair));
    if (!pair) {
        return;
    }

    pair->name = strdup(name);
    pair->icon_destroy.notify = pair_handle_icon_destroy;
    wl_list_init(&pair->icon_destroy.link);

    if (icon) {
        pair->icon = icon;
        wl_signal_add(&icon->destroy, &pair->icon_destroy);
    } else {
        kywc_log(KYWC_DEBUG, "cannot find icon for %s", name);
    }

    struct hash_entry *entry = hash_table_insert(manager->icon_pairs, pair->name, pair);
    pair->hash = entry->hash;
}

static void icon_manager_reset_icon_pairs(struct icon_manager *manager)
{
    struct hash_entry *entry;
    hash_table_for_each(entry, manager->icon_pairs) {
        struct icon_pair *pair = entry->data;
        // don't remove icons in specific theme
        if (pair->icon && pair->icon->theme == manager->specific) {
            continue;
        }
        hash_table_remove(manager->icon_pairs, entry);
        wl_list_remove(&pair->icon_destroy.link);
        free((void *)pair->name);
        free(pair);
    }
}

static bool set_icon_theme(struct icon_manager *manager, const char *name)
{
    struct icon_theme *theme = manager->current;
    /* current icon_theme is not changed */
    if (theme && strcmp(name, theme->name) == 0) {
        return false;
    }

    theme = icon_theme_get_or_create(manager, name);
    if (!theme) {
        return false;
    }

    manager->current = theme;
    icon_manager_reset_icon_pairs(manager);

    return true;
}

static struct icon *get_icon(struct icon_manager *manager, const char *name)
{
    struct hash_entry *entry = hash_table_search(manager->icon_pairs, name);
    if (entry && entry->data) {
        struct icon_pair *pair = entry->data;
        // may be NULL if no icon found
        return pair->icon;
    }

    struct icon *icon = NULL;
    // find icon name from desktops
    const char *icon_name = get_icon_name_from_desktop(manager, name);
    if (icon_name) {
        icon = icon_manager_get_icon(manager, icon_name);
    }
    // fallback to use orig name
    if (!icon) {
        icon = icon_manager_get_icon(manager, name);
    }
    // fuzzy search desktops from exec name
    if (!icon) {
        icon_name = fuzzy_get_icon_name_from_desktop(manager, name);
        if (icon_name) {
            icon = icon_manager_get_icon(manager, icon_name);
        }
    }

    icon_manager_add_icon_pair(manager, name, icon);

    return icon;
}

static const char *get_icon_name(struct icon *icon)
{
    return icon->name;
}

static struct wlr_buffer *get_icon_buffer(struct icon *icon, int size, float scale)
{
    struct icon_buffer *buffer;
    wl_list_for_each(buffer, &icon->buffers, link) {
        // maybe  ceil(buffer->scale) == ceil(scale) && buffer->width > size * scale
        if (buffer->size == size && buffer->scale == scale) {
            return buffer->buffer;
        }
    }

    const char *image_path = NULL, *image_data = NULL;

    if (icon->types & ICON_TYPE_PNG) {
        int target_scale = ceil(scale);
        int target_scale_size = size * target_scale;
        int scale_size, max_scale_size = 0, min_scale_size = 1024;
        struct icon_entry *similar_entry = NULL, *max_entry = NULL;

        struct icon_entry *entry;
        wl_list_for_each(entry, &icon->entries, link) {
            if (entry->size == size && entry->scale == target_scale) {
                image_path = entry->path;
                break;
            }
            scale_size = entry->size * entry->scale;
            if (scale_size >= target_scale_size && scale_size < min_scale_size) {
                min_scale_size = scale_size;
                similar_entry = entry;
            }
            if (scale_size > max_scale_size) {
                max_scale_size = scale_size;
                max_entry = entry;
            }
        }

        if (!image_path && (similar_entry || max_entry)) {
            // TODO: drop if the png size is much large than target
            image_path = similar_entry ? similar_entry->path : max_entry->path;
        }
        if (!image_path) {
            image_path = icon->png_path;
        }
    }

    if (!image_path && icon->types & ICON_TYPE_SVG) {
        // preload svg data
        if (!icon->svg_data) {
            struct file *file = file_open(icon->svg_path, NULL, NULL);
            if (file) {
                icon->svg_data = strdup(file_get_data(file, NULL));
                file_close(file);
            }
        }
        image_data = icon->svg_data;
    }

    if (!image_path && !image_data) {
        if (icon->ico_path) {
            image_path = icon->ico_path;
        } else if (icon->xpm_path) {
            image_path = icon->xpm_path;
        } else {
            return NULL;
        }
    }

    buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        return NULL;
    }

    struct draw_info info = {
        .width = size, .height = size, .scale = scale, .image = image_path, .svg = image_data
    };
    buffer->buffer = painter_draw_buffer(&info);
    if (!buffer->buffer) {
        free(buffer);
        return NULL;
    }

    buffer->size = size;
    buffer->scale = scale;
    wl_list_insert(&icon->buffers, &buffer->link);

    return buffer->buffer;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct icon_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);

    struct desktop *desktop, *desktop_tmp;
    wl_list_for_each_safe(desktop, desktop_tmp, &manager->desktops, link) {
        wl_list_remove(&desktop->link);
        free((void *)desktop->name);
        free(desktop->icon_name);
        free(desktop->exec_name);
        free(desktop->startup_name);
        free(desktop->app_name);
        free(desktop);
    }

    icon_theme_destroy(manager->pixmap);
    icon_theme_destroy(manager->specific);

    struct icon_theme *theme, *theme_tmp;
    wl_list_for_each_safe(theme, theme_tmp, &manager->themes, link) {
        icon_theme_destroy(theme);
    }

    struct hash_entry *entry;
    hash_table_for_each(entry, manager->icon_pairs) {
        struct icon_pair *pair = entry->data;
        free((void *)pair->name);
        free(pair);
    }
    hash_table_destroy(manager->icon_pairs);

    free(manager);
}

static int watcher_process(int fd, uint32_t mask, void *data)
{
    if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) {
        return 0;
    }

    struct icon_manager *manager = data;
    const struct inotify_event *event;
    struct hash_entry *entry;
    struct watch_fd *wd;
    char buf[4096];
    ssize_t size;

    for (;;) {
        size = read(fd, buf, sizeof(buf));
        if (size == -1 && errno != EAGAIN) {
            break;
        }
        if (size <= 0) {
            break;
        }

        for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *)ptr;
            if (event->len == 0 || event->mask & IN_ISDIR) {
                continue;
            }
            entry = hash_table_search(manager->watcher.wds, (void *)(uintptr_t)event->wd);
            if (!entry || !entry->data) {
                continue;
            }

            wd = entry->data;
            if (wd->handler) {
                wd->handler(wd, event, wd->data);
            }
        }
    }

    return 0;
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct icon_manager *manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->display_destroy.link);

    struct hash_entry *entry;
    hash_table_for_each(entry, manager->watcher.wds) {
        struct watch_fd *wd = entry->data;
        inotify_rm_watch(manager->watcher.event_fd, wd->fd);
        free(wd->path);
        free(wd);
    }
    hash_table_destroy(manager->watcher.wds);

    wl_event_source_remove(manager->watcher.source);
    close(manager->watcher.event_fd);
}

static void icon_manager_add_watcher_fd(
    struct icon_manager *manager, int fd, const char *path,
    void (*handler)(struct watch_fd *wd, const struct inotify_event *event, void *data), void *data)
{
    struct watch_fd *wd = calloc(1, sizeof(*wd));
    if (!wd) {
        return;
    }

    wd->fd = fd;
    wd->path = strdup(path);
    wd->handler = handler;
    wd->data = data;
    hash_table_insert(manager->watcher.wds, (void *)(uintptr_t)fd, wd);
}

static void icon_manager_init_watcher(struct icon_manager *manager)
{
    manager->watcher.event_fd = inotify_init1(IN_NONBLOCK);
    if (manager->watcher.event_fd < 0) {
        return;
    }

    manager->watcher.source =
        wl_event_loop_add_fd(manager->server->event_loop, manager->watcher.event_fd,
                             WL_EVENT_READABLE, watcher_process, manager);
    if (!manager->watcher.source) {
        close(manager->watcher.event_fd);
        return;
    }

    manager->watcher.wds = hash_table_create_int(NULL);
    hash_table_set_max_entries(manager->watcher.wds, 200);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(manager->server->display, &manager->display_destroy);
}

struct icon_manager *icon_manager_create(struct theme_manager *theme_manager)
{
    struct icon_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return NULL;
    }

    manager->server = theme_manager->server;
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(manager->server, &manager->server_destroy);

    icon_manager_init_watcher(manager);

    wl_list_init(&manager->themes);
    wl_list_init(&manager->desktops);
    manager->icon_pairs = hash_table_create_string(NULL);
    hash_table_set_max_entries(manager->icon_pairs, 200);

    // create an empty theme for all specific path icons
    manager->specific = icon_theme_create_empty(manager, NULL);
    manager->pixmap = icon_theme_create_empty(manager, NULL);
    manager->fallback = icon_theme_create_empty(manager, FALLBACK_ICON_THEME_NAME);

    // load all desktop files
    file_for_each(APPPATH, NULL, load_desktop, manager);
    // load all icons in pixmap dir
    if (manager->pixmap) {
        file_for_each(PIXMAPPATH, NULL, load_icon, manager->pixmap);
    }
    // load fallback icon theme
    if (manager->fallback) {
        file_for_each(ICONPATH, FALLBACK_ICON_THEME_NAME, load_icon, manager->fallback);
    }

    theme_manager->icon_impl.set_icon_theme = set_icon_theme;
    theme_manager->icon_impl.get_icon = get_icon;
    theme_manager->icon_impl.get_icon_name = get_icon_name;
    theme_manager->icon_impl.get_icon_buffer = get_icon_buffer;

    return manager;
}
