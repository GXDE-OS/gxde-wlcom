// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _RENDER_PROFILE_H_
#define _RENDER_PROFILE_H_

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>
#include <wlr/render/wlr_renderer.h>

// clang-format off
#define KY_PROFILE_RENDER_CREATE(renderer) do { if (renderer) ky_profile_render_create(renderer); } while (0)
#define KY_PROFILE_RENDER_ZONE_COLOR(renderer, var, name, color) do { static const struct ___tracy_source_location_data var = { name, __func__, __FILE__, __LINE__, color }; if (renderer) ky_profile_render_begin(renderer, &var); } while (0)
#define KY_PROFILE_RENDER_ZONE(renderer, var, name) do { if (renderer) KY_PROFILE_RENDER_ZONE_COLOR(renderer, var, name, 0x8b0000); } while (0)
#define KY_PROFILE_RENDER_ZONE_END(renderer) do { if (renderer) ky_profile_render_end(renderer); } while (0)
#define KY_PROFILE_RENDER_COLLECT(renderer) do { if (renderer) ky_profile_render_collect(renderer); } while (0)
// clang-format on

void ky_profile_render_create(struct wlr_renderer *renderer);
void ky_profile_render_destroy(struct wlr_renderer *renderer);
void ky_profile_render_begin(struct wlr_renderer *renderer,
                             const struct ___tracy_source_location_data *data);
void ky_profile_render_end(struct wlr_renderer *renderer);
void ky_profile_render_collect(struct wlr_renderer *renderer);

#else

#define KY_PROFILE_RENDER_CREATE(renderer)
#define KY_PROFILE_RENDER_ZONE_COLOR(renderer, var, name, color)
#define KY_PROFILE_RENDER_ZONE(renderer, var, name)
#define KY_PROFILE_RENDER_ZONE_END(renderer)
#define KY_PROFILE_RENDER_COLLECT(renderer)

#endif /* TRACY_ENABLE */

#endif /* _RENDER_PROFILE_H_ */
