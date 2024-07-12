// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <string.h>
#include <xf86drm.h>

#include "util/quirks.h"

/* graphics quirks */
struct quirks {
    const char *name;   /* dc name */
    const char *vendor; /* gpu egl-vendor */
    uint32_t drm_mask;
    uint32_t render_mask;
};

static const struct quirks quirks_table[] = {
    {
        "hisi",
        "ARM",
        .render_mask = QUIRKS_MASK_MASTER_FD | QUIRKS_MASK_EGL_WAYLAND,
    },
    {
        "nvidia-drm",
        "NVIDIA",
        .drm_mask = QUIRKS_MASK_SOFTWARE_CURSOR,
        .render_mask = QUIRKS_MASK_EXPLICIT_SYNC,
    },
    {
        "nouveau",
        NULL,
        .drm_mask = QUIRKS_MASK_SOFTWARE_CURSOR,
    },
    {
        "virtio_gpu",
        "Mesa Project",
        .drm_mask = QUIRKS_MASK_SOFTWARE_CURSOR,
        .render_mask = QUIRKS_MASK_EXPLICIT_SYNC,
    },
    {
        "vmwgfx",
        NULL,
        .drm_mask = QUIRKS_MASK_SOFTWARE_CURSOR,
    },
    {
        "mtgpu",
        "MTT Mesa Client",
        .render_mask = QUIRKS_MASK_NO_MODIFIFIERS,
    },
    {
        "mwv207",
        "Mesa Project",
        .render_mask = QUIRKS_MASK_NO_MODIFIFIERS,
    },
};

uint32_t quirks_by_backend(int drm_fd)
{
    drmVersion *version = drmGetVersion(drm_fd);
    if (!version) {
        return 0;
    }

    const struct quirks *quirks = NULL;
    for (size_t i = 0; i < sizeof(quirks_table) / sizeof(quirks_table[0]); ++i) {
        if (!quirks_table[i].name || strcmp(quirks_table[i].name, version->name)) {
            continue;
        }
        quirks = &quirks_table[i];
        break;
    }

    drmFreeVersion(version);

    return quirks ? quirks->drm_mask : 0;
}

uint32_t quirks_by_renderer(int drm_fd, const char *vendor_name)
{
    drmVersion *version = drmGetVersion(drm_fd);
    if (!version) {
        return 0;
    }

    const struct quirks *quirks = NULL;
    for (size_t i = 0; i < sizeof(quirks_table) / sizeof(quirks_table[0]); ++i) {
        if (!quirks_table[i].vendor || strcmp(vendor_name, quirks_table[i].vendor)) {
            continue;
        }

        if (quirks_table[i].name && strcmp(quirks_table[i].name, version->name)) {
            continue;
        }

        quirks = &quirks_table[i];
        break;
    }

    drmFreeVersion(version);

    return quirks ? quirks->render_mask : 0;
}
