// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _DEFAULT_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <wayland-util.h>

#include "util/file.h"
#include "util/string.h"

struct pair {
    const char *key, *value;
};

struct file {
    int fd;
    char *data, *last;
    size_t size;

    char *delim, *needle;
    size_t needle_len;
    struct wl_array pairs;
};

static bool pair_parse(struct file *file, char *pair, file_parse_func_t parse, void *data)
{
    if (!*pair || *pair == '#') {
        return false;
    }

    if (file->needle_len == 0) {
        char *key = string_skip_space(pair);
        return parse(file, key, NULL, data);
    }

    char *value = strstr(pair, file->needle);
    if (!value) {
        return false;
    }

    *value = '\0';
    char *key = string_skip_space(pair);
    value = string_skip_space(value + file->needle_len);

    return parse(file, key, value, data);
}

void file_parse(struct file *file, file_parse_func_t parse, void *data)
{
    if (!file->last || !file->delim) {
        return;
    }

    char *save, *pair = strtok_r(file->last, file->delim, &save);

    while (pair) {
        file->last = pair + strlen(pair) + 1;

        pair = string_skip_space(pair);
        if (pair_parse(file, pair, parse, data)) {
            break;
        }

        pair = strtok_r(NULL, file->delim, &save);
    }

    if (!pair || file->last >= file->data + file->size) {
        file->last = NULL;
    }
}

static bool get_value(struct file *file, const char *key, const char *value, void *data)
{
    struct pair *pair = wl_array_add(&file->pairs, sizeof(*pair));
    if (pair) {
        *pair = (struct pair){ key, value };
    }

    const char **need = data;
    if (strcmp(key, *need) == 0) {
        *need = value;
        return true;
    }
    return false;
}

const char *file_get_value(struct file *file, const char *key)
{
    struct pair *pair;
    wl_array_for_each(pair, &file->pairs) {
        if (strcmp(pair->key, key) == 0) {
            return pair->value;
        }
    }

    const char *value = key;
    file_parse(file, get_value, &value);
    return value == key ? NULL : value;
}

char *file_get_value_copy(struct file *file, const char *key)
{
    const char *value = file_get_value(file, key);
    return value ? strdup(value) : NULL;
}

struct file *file_open(const char *name, const char *delim, const char *needle)
{
    struct file *file = calloc(1, sizeof(*file));
    if (!file) {
        return NULL;
    }

    file->fd = open(name, O_RDONLY);
    if (file->fd == -1) {
        free(file);
        return NULL;
    }

    struct stat st;
    if (fstat(file->fd, &st) == -1) {
        close(file->fd);
        free(file);
        return NULL;
    }

    file->size = st.st_size;
    file->data = mmap(NULL, file->size, PROT_READ | PROT_WRITE, MAP_PRIVATE, file->fd, 0);
    if (file->data == MAP_FAILED) {
        close(file->fd);
        free(file);
        return NULL;
    }

    file->last = file->data;
    wl_array_init(&file->pairs);

    if (delim && *delim) {
        file->delim = strdup(delim);
    }
    if (needle && *needle) {
        file->needle = strdup(needle);
        file->needle_len = strlen(file->needle);
    }

    return file;
}

char *file_get_data(struct file *file, size_t *size)
{
    if (size) {
        *size = file->size;
    }
    return file->data;
}

void file_close(struct file *file)
{
    if (!file) {
        return;
    }

    munmap(file->data, file->size);
    close(file->fd);

    wl_array_release(&file->pairs);
    free(file->needle);
    free(file->delim);
    free(file);
}

static bool is_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

static bool is_regular_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISREG(st.st_mode);
    }
    return false;
}

static bool for_each(const char *path, int len, file_iterator_func_t iterator, void *data)
{
    DIR *dir = opendir(path);
    if (!dir) {
        return false;
    }

    int offset = (int)strlen(path) - len;
    const char *subpath = offset > 0 ? path + len : NULL;
    if (iterator(path, subpath, NULL, data)) {
        closedir(dir);
        // return false to skip the subpath but not the iterator
        return false;
    }

    bool need_break = false;

    for (struct dirent *ent = readdir(dir); ent; ent = readdir(dir)) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        char *fullpath = string_join_path(path, NULL, ent->d_name);
        if (!fullpath) {
            continue;
        }

        if (ent->d_type == DT_DIR || (ent->d_type == DT_UNKNOWN && is_directory(fullpath))) {
            need_break = for_each(fullpath, len, iterator, data);
        } else if ((ent->d_type == DT_REG || ent->d_type == DT_LNK) ||
                   (ent->d_type == DT_UNKNOWN && is_regular_file(fullpath))) {
            need_break = iterator(fullpath, subpath, ent->d_name, data);
        }

        free(fullpath);
        if (need_break) {
            break;
        }
    }

    closedir(dir);
    return need_break;
}

void file_for_each(const char *dirs, const char *subdir, file_iterator_func_t iterator, void *data)
{
    char *realpath = string_expand_path(dirs);
    if (!realpath) {
        return;
    }

    size_t len = 0;
    char **paths = string_split(realpath, ":", &len);
    if (!paths) {
        free(realpath);
        return;
    }

    for (size_t i = 0; i < len; i++) {
        char *fullpath = string_join_path(paths[i], subdir, NULL);
        if (!fullpath) {
            continue;
        }

        bool need_break = for_each(fullpath, strlen(fullpath) + 1, iterator, data);
        free(fullpath);
        if (need_break) {
            break;
        }
    }

    string_free_split(paths);
    free(realpath);
}

bool file_exists(const char *path)
{
    return path && access(path, F_OK) != -1;
}

const char *file_get_xdg_pictures_dir(void)
{
    struct file *file = NULL;
    const char *dir = NULL;
    char *user_dirs_file = NULL, *pictures_dir;

    user_dirs_file = string_expand_path("~/.config/user-dirs.dirs");
    if (!user_dirs_file) {
        goto out;
    }

    file = file_open(user_dirs_file, "\n", "=");
    if (!file) {
        goto out;
    }

    dir = file_get_value(file, "XDG_PICTURES_DIR");
    if (!dir) {
        goto out;
    }

out:
    pictures_dir = string_expand_path(dir ? dir : "~");
    file_close(file);
    free(user_dirs_file);

    return pictures_dir;
}
