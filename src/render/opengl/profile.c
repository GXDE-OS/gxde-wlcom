// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "render/opengl.h"

#ifdef TRACY_ENABLE
#include <epoxy/gl.h>

#define QUERY_COUNT 65536

static struct ky_gl_profile {
    struct ky_opengl_renderer *gl_renderer;
    uint8_t context_id;
    uint32_t queries[QUERY_COUNT];
    uint32_t head;
    uint32_t tail;
} profile = { 0 };

static uint32_t next_query_id(void)
{
    uint32_t id = profile.head;
    profile.head = (profile.head + 1) % QUERY_COUNT;
    return id;
}

void ky_profile_gl_create(struct wlr_renderer *renderer)
{
    struct ky_opengl_renderer *gl_renderer = ky_opengl_renderer_from_wlr_renderer(renderer);
    if (!gl_renderer->exts.EXT_disjoint_timer_query) {
        return;
    }
    
    ky_egl_make_current(gl_renderer->egl, NULL);

    profile.gl_renderer = gl_renderer;
    profile.context_id = 0;
    profile.head = 0;
    profile.tail = 0;

    glGenQueries(QUERY_COUNT, profile.queries);

    int64_t gpu_time;
    glGetInteger64v(GL_TIMESTAMP, &gpu_time);

    const struct ___tracy_gpu_new_context_data context_data = {
        .gpuTime = gpu_time,
        .period = 1.f,
        .context = profile.context_id,
        .type = 1,
    };
    ___tracy_emit_gpu_new_context(context_data);

    ky_egl_unset_current(gl_renderer->egl);
}

void ky_profile_gl_destroy(void)
{
    if (!profile.gl_renderer) {
        return;
    }

    ky_egl_make_current(profile.gl_renderer->egl, NULL);

    glDeleteQueries(QUERY_COUNT, profile.queries);

    ky_egl_unset_current(profile.gl_renderer->egl);
}

void ky_profile_gl_begin(const struct ___tracy_source_location_data *data)
{
    if (!profile.gl_renderer) {
        return;
    }

    uint32_t query_id = next_query_id();

    glQueryCounter(profile.queries[query_id], GL_TIMESTAMP);

    const struct ___tracy_gpu_zone_begin_data begin_data = {
        .srcloc = (uint64_t)data,
        .queryId = query_id,
        .context = profile.context_id,
    };
    ___tracy_emit_gpu_zone_begin(begin_data);
}

void ky_profile_gl_end(void)
{
    if (!profile.gl_renderer) {
        return;
    }

    uint32_t query_id = next_query_id();

    glQueryCounter(profile.queries[query_id], GL_TIMESTAMP);

    const struct ___tracy_gpu_zone_end_data end_data = {
        .queryId = query_id,
        .context = profile.context_id,
    };
    ___tracy_emit_gpu_zone_end(end_data);
}

void ky_profile_gl_collect(void)
{
    if (!profile.gl_renderer) {
        return;
    }

    ky_egl_make_current(profile.gl_renderer->egl, NULL);

    if (profile.tail == profile.head) {
        return;
    }

    int64_t gpu_time;
    glGetInteger64v(GL_TIMESTAMP, &gpu_time);
    struct ___tracy_gpu_time_sync_data sync_data = {
        .gpuTime = gpu_time,
        .context = profile.context_id,
    };
    ___tracy_emit_gpu_time_sync(sync_data);

    while (profile.tail != profile.head) {
        uint32_t query = profile.queries[profile.tail];

        GLint available;
        glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) {
            return;
        }

        uint64_t time;
        glGetQueryObjectui64v(query, GL_QUERY_RESULT, &time);

        const struct ___tracy_gpu_time_data time_data = {
            .gpuTime = time,
            .queryId = profile.tail,
            .context = profile.context_id,
        };
        ___tracy_emit_gpu_time(time_data);

        profile.tail = (profile.tail + 1) % QUERY_COUNT;
    }

    ky_egl_unset_current(profile.gl_renderer->egl);
}

#endif /* TRACY_ENABLE */
