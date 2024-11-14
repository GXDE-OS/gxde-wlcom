// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <kywc/log.h>

#include "output_p.h"
#include "util/sysfs.h"

#define MAX_NAME 512
#define BRIGHTNESS_CLAMP(val) ((val) > 100 ? 100 : (val))

bool output_support_brightness(struct output *output)
{
    if (!strncmp(output->base.name, "LVDS", 4) || !strncmp(output->base.name, "eDP", 3)) {
        return true;
    }

    return false;
}

static bool get_backlight_path(const char *path, char **path_name)
{
    bool ret = false;

    struct stat stat_buf;
    lstat(path, &stat_buf);
    if (!S_ISDIR(stat_buf.st_mode)) {
        return false;
    }
    DIR *dir;
    if ((dir = opendir(path)) == NULL) {
        kywc_log(KYWC_ERROR, "Can't open %s", path);
        return false;
    }

    struct dirent *dirp;
    while ((dirp = readdir(dir)) != NULL) {
        if (strcmp(".", dirp->d_name) == 0 || strcmp("..", dirp->d_name) == 0) {
            continue;
        }

        char fpath[MAX_NAME] = { 0x00 };
        sprintf(fpath, "%s/%s", path, dirp->d_name);

        struct stat stat_buf;
        lstat(fpath, &stat_buf);
        if (!S_ISREG(stat_buf.st_mode)) {
            ret = true;
            *path_name = strdup(fpath);
            break;
        }
    }
    closedir(dir);

    return ret;
}

static bool get_backlight_info(char **ddir, uint64_t *cur_brightness, uint64_t *max_brightness)
{
    char *dir = NULL;
    bool dret = get_backlight_path("/sys/class/backlight", &dir);
    if (!dret || !dir) {
        return false;
    }

    *ddir = dir;
    char file_name[MAX_NAME] = { 0x00 };

    // TODO: fix multiple outputs support brightness match dev name
    sprintf(file_name, "%s/brightness", dir);

    bool ret = sysfs_read_uint64(file_name, cur_brightness);
    if (!ret) {
        return false;
    }

    memset(file_name, 0, MAX_NAME);
    sprintf(file_name, "%s/max_brightness", dir);

    return sysfs_read_uint64(file_name, max_brightness);

    return false;
}

bool output_set_backlight(uint32_t value)
{
    char *ddir = NULL;
    uint64_t cur_brightness = 0;
    uint64_t max_brightness = 0;
    bool ret = get_backlight_info(&ddir, &cur_brightness, &max_brightness);
    if (!ret) {
        kywc_log(KYWC_ERROR, "cannot get the sysfs information");
        return false;
    }

    char file_name[MAX_NAME] = { 0x00 };
    // TODO: fix multiple outputs support brightness match dev name
    sprintf(file_name, "%s/brightness", ddir);

    double temp = (double)max_brightness * (double)value / 100.0;
    uint64_t brightness = temp;
    if (brightness + 1 <= temp + 0.5) {
        brightness += 1;
    }

    ret = sysfs_write_uint64(file_name, brightness);
    if (ddir) {
        free(ddir);
    }

    kywc_log(KYWC_DEBUG, "%s-set: %lu", file_name, brightness);

    return ret;
}

static bool brightness_get(uint32_t *brightness)
{
    char *ddir = NULL;
    uint64_t cur_brightness = 0;
    uint64_t max_brightness = 0;

    bool ret = get_backlight_info(&ddir, &cur_brightness, &max_brightness);
    if (!ret) {
        kywc_log(KYWC_ERROR, "cannot get the sysfs information");
        if (ddir) {
            free(ddir);
        }
        return false;
    }

    *brightness = (cur_brightness * 100) / max_brightness;
    kywc_log(KYWC_DEBUG, "%s-get: %u", ddir, *brightness);

    free(ddir);

    return true;
}

bool output_get_backlight(struct kywc_output *kywc_output, uint32_t *brightness)
{
    if (!kywc_output->prop.brightness_support) {
        return false;
    }

    return brightness_get(brightness);
}

bool output_set_brightness(struct kywc_output *kywc_output, uint32_t brightness)
{
    if (!kywc_output->state.enabled) {
        return false;
    }

    struct kywc_output_state state = kywc_output->state;
    state.brightness = BRIGHTNESS_CLAMP(brightness);
    kywc_output_set_state(kywc_output, &state);
    return true;
}
