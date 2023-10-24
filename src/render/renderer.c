// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/backend.h>
#include <wlr/render/pixman.h>

#include <kywc/log.h>

#include "render/opengl.h"
#include "render/renderer.h"

struct wlr_renderer *ky_renderer_autocreate(struct wlr_backend *backend)
{
    struct wlr_renderer *renderer = NULL;

    /* get drm fd from backend */
    int drm_fd = wlr_backend_get_drm_fd(backend);
    if (drm_fd < 0) {
        kywc_log(KYWC_ERROR, "Cannot create OpenGL renderer: no DRM fd available");
    } else {
        /* try opengl renderer first */
        renderer = ky_opengl_renderer_create_with_drm_fd(drm_fd);
    }

    if (!renderer) {
        kywc_log(KYWC_ERROR, "Failed to create a OpenGL renderer");
        renderer = wlr_pixman_renderer_create();
    }

    if (!renderer) {
        kywc_log(KYWC_ERROR, "Failed to create a pixman renderer");
        kywc_log(KYWC_ERROR, "Could not initialize renderer");
    }

    return renderer;
}
