// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <kywc/log.h>

#include "util/sysfs.h"

bool sysfs_read_uint64(const char *filename, uint64_t *val)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        kywc_log(KYWC_ERROR, "reading %s failed", filename);
        return false;
    }

    uint64_t data;
    if (fscanf(fp, "%lu", &data) != 1) {
        kywc_log(KYWC_ERROR, "Couldn't parse an unsigned integer from '%s'", filename);
        fclose(fp);
        return false;
    }

    *val = data;

    fclose(fp);
    return true;
}

bool sysfs_write_uint64(const char *filename, uint64_t val)
{
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        kywc_log(KYWC_ERROR, "writing %s failed", filename);
        return false;
    }

    if (fprintf(fp, "%lu", val) < 0) {
        kywc_log(KYWC_ERROR, "Couldn't write an unsigned integer to '%s'", filename);
        fclose(fp);
        return false;
    }

    fclose(fp);
    return true;
}

size_t sysfs_read_data(const char *filename, void *buf, size_t size)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        kywc_log(KYWC_ERROR, "reading %s failed", filename);
        return 0;
    }

    size_t len = fread(buf, 1, size, fp);
    fclose(fp);

    return len;
}
