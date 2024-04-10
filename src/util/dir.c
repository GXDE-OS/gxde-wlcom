// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wordexp.h>

#include "util/dir.h"
#include "util/fscan.h"

bool dir_exists(const char *path)
{
    return path && access(path, F_OK) != -1;
}

const char *dir_get_xdg_config(void)
{
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        return NULL;
    }

    size_t size = strlen(home_dir) + strlen("/.config") + 1;
    char *config_home = malloc(size);
    if (config_home == NULL) {
        return NULL;
    }
    snprintf(config_home, size, "%s/.config", home_dir);

    return config_home;
}

const char *dir_get_xdg_pictures(void)
{
    const char *config_home = dir_get_xdg_config();
    if (!config_home) {
        return NULL;
    }

    const char user_dirs_file[] = "user-dirs.dirs";
    size_t size = strlen(config_home) + strlen(user_dirs_file) + 2;
    char *config_file = malloc(size);
    if (!config_file) {
        return NULL;
    }

    snprintf(config_file, size, "%s/%s", config_home, user_dirs_file);

    FILE *file = fopen(config_file, "r");
    free(config_file);
    free((void *)config_home);
    if (file == NULL) {
        return NULL;
    }

    char *dir = fscan_search_keyword(file, "XDG_PICTURES_DIR");
    fclose(file);
    if (!dir) {
        return NULL;
    }

    wordexp_t p;
    char *pictures_dir = NULL;
    if (wordexp(dir, &p, WRDE_UNDEF) == 0) {
        pictures_dir = strdup(p.we_wordv[0]);
        wordfree(&p);
    }
    free(dir);

    return pictures_dir;
}
