// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include "util/fscan.h"

static const char *fscan_next_path(const char *path)
{
    char *colon = strchr(path, ':');
    if (!colon) {
        return NULL;
    }
    return colon + 1;
}

static char *fscan_build_dir(const char *dir, const char *subdir)
{
    if (!dir || !subdir) {
        return NULL;
    }

    const char *colon = strchr(dir, ':');
    if (!colon) {
        colon = dir + strlen(dir);
    }
    size_t dirlen = colon - dir;

    const char *scolon = strchr(subdir, ':');
    if (!scolon) {
        scolon = subdir + strlen(subdir);
    }
    size_t subdirlen = scolon - subdir;

    const char *home = "";
    const char *homesep = "";
    size_t homelen = 0;
    if (*dir == '~') {
        home = getenv("HOME");
        if (!home) {
            return NULL;
        }
        homelen = strlen(home);
        homesep = "/";
        dir++;
        dirlen--;
    }

    size_t full_size = 1 + homelen + 1 + dirlen + 1 + subdirlen + 1;
    char *full = malloc(full_size);
    if (!full) {
        return NULL;
    }
    snprintf(full, full_size, "%s%s%.*s/%.*s", home, homesep, (int)dirlen, dir, (int)subdirlen,
             subdir);
    return full;
}

char *fscan_build_fullname(const char *dir, const char *subdir, const char *file)
{
    char *full;
    size_t full_size;

    if (!dir || !subdir || !file)
        return NULL;

    full_size = strlen(dir) + 1 + strlen(subdir) + 1 + strlen(file) + 1;
    full = malloc(full_size);
    if (!full)
        return NULL;
    snprintf(full, full_size, "%s/%s/%s", dir, subdir, file);
    return full;
}

static void fscan_foreach_files(const char *path,
                                void (*load_callback)(const char *, const char *, void *),
                                void *user_data)
{
    DIR *dir = opendir(path);
    if (!dir) {
        return;
    }

    struct dirent *ent;
    char *full;

    for (ent = readdir(dir); ent; ent = readdir(dir)) {
#ifdef _DIRENT_HAVE_D_TYPE
        if (ent->d_type != DT_UNKNOWN && ent->d_type != DT_REG && ent->d_type != DT_LNK) {
            continue;
        }
#endif
        full = fscan_build_fullname(path, "", ent->d_name);
        if (!full) {
            continue;
        }

        load_callback(full, ent->d_name, user_data);

        free(full);
    }

    closedir(dir);
}

void fscan_start(const char *scan_path, const char *subdir,
                 void (*load_callback)(const char *, const char *, void *), void *user_data)
{
    const char *path;
    char *dir;

    for (path = scan_path; path; path = fscan_next_path(path)) {
        dir = fscan_build_dir(path, subdir);
        if (!dir) {
            continue;
        }
        fscan_foreach_files(dir, load_callback, user_data);
        free(dir);
    }
}

void fscan_file(const char *scan_path, const char *subdir, const char *file_name,
                void (*load_callback)(const char *, void *), void *user_data)
{
    const char *path;
    char *dir;
    char *full;

    for (path = scan_path; path; path = fscan_next_path(path)) {
        dir = fscan_build_dir(path, subdir);
        if (!dir) {
            continue;
        }

        full = fscan_build_fullname(dir, "", file_name);
        if (!full) {
            continue;
        }

        load_callback(full, user_data);

        free(dir);
        free(full);
    }
}

char *fscan_search_keyword(FILE *fp, const char *keyword)
{
    char *result = NULL;
    char *line = NULL;
    char *p;
    size_t line_size = 0;

    if (!keyword) {
        return NULL;
    }

    size_t keyword_size = strlen(keyword);
    while (getline(&line, &line_size, fp) >= 0) {
        if (strncmp(line, keyword, keyword_size)) {
            continue;
        }

        p = line + keyword_size;
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
            free(line);
            return NULL;
        }

        char *r = result;
        while (*p && *p != '\r' && *p != '\t' && *p != '\n') {
            *r++ = *p++;
        }
        *r++ = '\0';

        break;
    }

    free(line);
    return result;
}
