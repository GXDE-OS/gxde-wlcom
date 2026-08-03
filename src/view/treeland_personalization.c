/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -----------------------------------------------------------------------------
 * Implementation of the treeland_personalization_manager_v1 protocol, letting
 * clients customise window appearance. It differs from Treeland's original in
 * places; those changes are tailored to GXDE's Wayland needs.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/types/wlr_compositor.h>

#include <kywc/log.h>

#include "scene/scene.h"
#include "scene/surface.h"
#include "treeland-personalization-manager-v1-protocol.h"
#include "view/view.h"
#include "view_p.h"

/**
 * @file treeland_personalization.c
 * @brief treeland_personalization_manager_v1 protocol implementation
 *
 * The protocol splits into two kinds of context:
 *
 * 1. @c window_context -- per-window appearance (blend mode, corner radius,
 *    shadow, border, titlebar). This maps onto the compositor's existing scene
 *    graph and SSD interfaces, and is the part that actually changes what gets
 *    rendered.
 *
 * 2. @c cursor_context / @c font_context / @c appearance_context /
 *    @c wallpaper_context -- global settings. Treeland writes these into
 *    DConfig and feeds them to its own QML shell; GXDE has no equivalent
 *    consumer, so this implementation keeps the state and broadcasts events to
 *    every bound context. That keeps the protocol semantics complete
 *    (set -> broadcast, get -> reply) without touching the existing
 *    @c theme_manager theme.
 *
 * Every interface here must keep the exact request/event layout of
 * treeland-personalization-manager-v1.xml as shipped in treeland-protocols,
 * including the parts GXDE does not act on: opcodes are positional, so dropping
 * a single request shifts every later one and clients end up calling something
 * completely different. That is why @c wallpaper_context exists below even
 * though the compositor does not own the wallpaper in GXDE.
 *
 * Every @c set_* broadcasts to *all* clients bound to that context, while
 * @c get_* replies only to the client that asked.
 */

#define TREELAND_PERSONALIZATION_MANAGER_VERSION 2

/* The protocol defines no defaults; these are GXDE's usual values, only ever
 * sent back when a client has never set anything. */
#define DEFAULT_CURSOR_SIZE 24
#define DEFAULT_FONT_SIZE 11
#define DEFAULT_WINDOW_OPACITY 255
#define DEFAULT_TITLEBAR_HEIGHT 40

struct treeland_personalization_manager {
    struct wl_global *global;

    /* Per-window contexts */
    struct wl_list window_contexts;

    /* Bound resources of the global contexts, used for broadcasting */
    struct wl_list wallpaper_contexts;
    struct wl_list cursor_contexts;
    struct wl_list font_contexts;
    struct wl_list appearance_contexts;

    /* Wallpaper, last committed metadata */
    char *wallpaper_metadata;

    /* Cursor */
    char *cursor_theme;
    uint32_t cursor_size;

    /* Font */
    char *font;
    char *monospace_font;
    uint32_t font_size;

    /* Appearance */
    int32_t round_corner_radius;
    char *icon_theme;
    char *active_color;
    uint32_t window_opacity;
    uint32_t window_theme_type;
    uint32_t window_titlebar_height;

    struct wl_listener server_destroy;
    struct wl_listener display_destroy;
};

static struct treeland_personalization_manager *manager = NULL;

/**
 * @brief Per-window personalization state
 *
 * The @c has_* flags distinguish "the client set this explicitly" from "never
 * set". The protocol says the compositor keeps its default behaviour when a
 * request was never issued, so the value alone is not enough.
 */
struct personalization_window_context {
    struct wl_resource *resource;
    struct wlr_surface *surface;
    struct wl_list link;

    struct wl_listener surface_destroy;
    struct wl_listener surface_map;

    bool has_blend_mode;
    int32_t blend_mode;

    bool has_radius;
    int32_t radius;

    bool has_shadow;
    struct {
        int32_t radius, offset_x, offset_y;
        int32_t r, g, b, a;
    } shadow;

    bool has_border;
    struct {
        int32_t width;
        int32_t r, g, b, a;
    } border;

    bool has_titlebar;
    int32_t titlebar_mode;
};

/* Global contexts only need the resource itself plus a list link */
struct personalization_context {
    struct wl_resource *resource;
    struct wl_list link;

    /* Cursor context only: values staged until commit */
    char *pending_theme;
    uint32_t pending_size;
    bool pending_theme_set;
    bool pending_size_set;

    /* Wallpaper context only: values staged until commit. @c fd is -1 on every
     * context, wallpaper or not, so the shared destroy path can close it
     * blindly without ever hitting fd 0 */
    struct {
        int fd;
        char *metadata;
        char *identifier;
        char *output;
        uint32_t options;
        bool isdark;
    } wallpaper;
};

/* Helpers */

static void string_replace(char **dst, const char *src)
{
    free(*dst);
    *dst = src ? strdup(src) : NULL;
}

/* Window context */

/**
 * @brief Apply the recorded personalization state to the window
 *
 * While the surface is unmapped its scene node does not exist yet, so the state
 * is only recorded and applied once the @c map event arrives.
 */
static void window_context_apply(struct personalization_window_context *ctx)
{
    if (!ctx->surface) {
        return;
    }

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_try_from_surface(ctx->surface);
    if (!scene_buffer) {
        return;
    }
    struct ky_scene_node *node = &scene_buffer->node;

    if (ctx->has_radius && ctx->radius >= 0) {
        int r = ctx->radius;
        ky_scene_node_set_radius(node, (int[4]){ r, r, r, r });
    }

    if (ctx->has_blend_mode) {
        switch (ctx->blend_mode) {
        case TREELAND_PERSONALIZATION_WINDOW_CONTEXT_V1_BLEND_MODE_BLUR: {
            /* Blur the whole window background, so the blur region is the
             * entire surface */
            pixman_region32_t region;
            pixman_region32_init_rect(&region, 0, 0, ctx->surface->current.width,
                                      ctx->surface->current.height);
            ky_scene_node_set_blur_region(node, &region);
            /* Same iteration count as the kde_blur default, so one rendering
             * stack does not end up with two different looks */
            ky_scene_node_set_blur_level(node, 3, 2.6f);
            pixman_region32_fini(&region);
            break;
        }
        case TREELAND_PERSONALIZATION_WINDOW_CONTEXT_V1_BLEND_MODE_TRANSPARENT:
        case TREELAND_PERSONALIZATION_WINDOW_CONTEXT_V1_BLEND_MODE_WALLPAPER:
        default: {
            /**
             * In Treeland the wallpaper mode composites the window over the
             * wallpaper layer, which relies on its own QML wallpaper. GXDE has
             * no equivalent, so this is treated like transparent and composited
             * with plain alpha -- at least that does not render something wrong.
             */
            pixman_region32_t empty;
            pixman_region32_init(&empty);
            ky_scene_node_set_blur_region(node, &empty);
            pixman_region32_fini(&empty);
            break;
        }
        }
    }

    struct view *view = view_try_from_wlr_surface(ctx->surface);
    if (!view) {
        return;
    }

    if (ctx->has_titlebar) {
        enum kywc_ssd ssd = view->base.ssd;
        if (ctx->titlebar_mode == TREELAND_PERSONALIZATION_WINDOW_CONTEXT_V1_ENABLE_MODE_ENABLE) {
            ssd |= KYWC_SSD_TITLE;
        } else {
            ssd &= ~KYWC_SSD_TITLE;
        }
        view_set_decoration(view, ssd);
    }

    /**
     * Shadow and border are drawn by the SSD frame, so the per-window overrides
     * go through @c ssd_set_*_override(). The protocol carries colours as 0-255
     * integers while the SSD side works in normalised floats, hence the convert.
     */
    if (ctx->has_shadow) {
        const float color[4] = {
            ctx->shadow.r / 255.0f,
            ctx->shadow.g / 255.0f,
            ctx->shadow.b / 255.0f,
            ctx->shadow.a / 255.0f,
        };
        ssd_set_shadow_override(&view->base, true, ctx->shadow.radius, ctx->shadow.offset_x,
                                ctx->shadow.offset_y, color);
    }

    if (ctx->has_border) {
        const float color[4] = {
            ctx->border.r / 255.0f,
            ctx->border.g / 255.0f,
            ctx->border.b / 255.0f,
            ctx->border.a / 255.0f,
        };
        ssd_set_border_override(&view->base, true, ctx->border.width, color);
    }
}

static void window_handle_surface_map(struct wl_listener *listener, void *data)
{
    struct personalization_window_context *ctx =
        wl_container_of(listener, ctx, surface_map);
    window_context_apply(ctx);
}

static void window_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct personalization_window_context *ctx =
        wl_container_of(listener, ctx, surface_destroy);

    wl_list_remove(&ctx->surface_destroy.link);
    wl_list_init(&ctx->surface_destroy.link);
    wl_list_remove(&ctx->surface_map.link);
    wl_list_init(&ctx->surface_map.link);
    ctx->surface = NULL;
}

static void window_handle_set_blend_mode(struct wl_client *client, struct wl_resource *resource,
                                         int32_t mode)
{
    struct personalization_window_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    ctx->has_blend_mode = true;
    ctx->blend_mode = mode;
    window_context_apply(ctx);
}

static void window_handle_set_round_corner_radius(struct wl_client *client,
                                                  struct wl_resource *resource, int32_t radius)
{
    struct personalization_window_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    ctx->has_radius = true;
    ctx->radius = radius;
    window_context_apply(ctx);
}

static void window_handle_set_shadow(struct wl_client *client, struct wl_resource *resource,
                                     int32_t radius, int32_t offset_x, int32_t offset_y, int32_t r,
                                     int32_t g, int32_t b, int32_t a)
{
    struct personalization_window_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    ctx->has_shadow = true;
    ctx->shadow.radius = radius;
    ctx->shadow.offset_x = offset_x;
    ctx->shadow.offset_y = offset_y;
    ctx->shadow.r = r;
    ctx->shadow.g = g;
    ctx->shadow.b = b;
    ctx->shadow.a = a;
    window_context_apply(ctx);
}

static void window_handle_set_border(struct wl_client *client, struct wl_resource *resource,
                                     int32_t width, int32_t r, int32_t g, int32_t b, int32_t a)
{
    struct personalization_window_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    ctx->has_border = true;
    ctx->border.width = width;
    ctx->border.r = r;
    ctx->border.g = g;
    ctx->border.b = b;
    ctx->border.a = a;
    window_context_apply(ctx);
}

static void window_handle_set_titlebar(struct wl_client *client, struct wl_resource *resource,
                                       int32_t mode)
{
    struct personalization_window_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    ctx->has_titlebar = true;
    ctx->titlebar_mode = mode;
    window_context_apply(ctx);
}

static void context_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct treeland_personalization_window_context_v1_interface window_impl = {
    .set_blend_mode = window_handle_set_blend_mode,
    .set_round_corner_radius = window_handle_set_round_corner_radius,
    .set_shadow = window_handle_set_shadow,
    .set_border = window_handle_set_border,
    .set_titlebar = window_handle_set_titlebar,
    .destroy = context_handle_destroy,
};

static void window_handle_resource_destroy(struct wl_resource *resource)
{
    struct personalization_window_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }

    wl_list_remove(&ctx->surface_destroy.link);
    wl_list_remove(&ctx->surface_map.link);
    wl_list_remove(&ctx->link);
    free(ctx);
}

/* Wallpaper context */

static void wallpaper_close_fd(struct personalization_context *ctx)
{
    if (ctx->wallpaper.fd >= 0) {
        close(ctx->wallpaper.fd);
        ctx->wallpaper.fd = -1;
    }
}

/**
 * @brief Take the wallpaper image and its metadata
 *
 * Ownership of @p fd is handed over by libwayland, so a previously staged one
 * is closed here rather than leaked.
 */
static void wallpaper_handle_set_fd(struct wl_client *client, struct wl_resource *resource,
                                    int32_t fd, const char *metadata)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        close(fd);
        return;
    }
    wallpaper_close_fd(ctx);
    ctx->wallpaper.fd = fd;
    string_replace(&ctx->wallpaper.metadata, metadata);
}

static void wallpaper_handle_set_identifier(struct wl_client *client, struct wl_resource *resource,
                                            const char *identifier)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    string_replace(&ctx->wallpaper.identifier, identifier);
}

static void wallpaper_handle_set_output(struct wl_client *client, struct wl_resource *resource,
                                        const char *output)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    string_replace(&ctx->wallpaper.output, output);
}

static void wallpaper_handle_set_on(struct wl_client *client, struct wl_resource *resource,
                                    uint32_t options)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    ctx->wallpaper.options = options;
}

static void wallpaper_handle_set_isdark(struct wl_client *client, struct wl_resource *resource,
                                        uint32_t isdark)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    ctx->wallpaper.isdark = isdark;
}

/**
 * @brief Commit the staged wallpaper
 *
 * In Treeland this writes the image out and points its QML shell at it. In GXDE
 * the wallpaper belongs to gxde-desktop-panel, which paints it from its own
 * config, so the compositor has nothing to draw and deliberately does not act
 * on the image. The metadata is still kept so that a later @c get_metadata --
 * from this client or another one -- reports what was last committed.
 */
static void wallpaper_handle_commit(struct wl_client *client, struct wl_resource *resource)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }

    kywc_log(KYWC_DEBUG,
             "(Treeland Shim) Personalization: wallpaper commit ignored, GXDE paints the "
             "wallpaper itself (identifier: %s, output: %s, options: %u, dark: %d)",
             ctx->wallpaper.identifier ? ctx->wallpaper.identifier : "",
             ctx->wallpaper.output ? ctx->wallpaper.output : "", ctx->wallpaper.options,
             ctx->wallpaper.isdark);

    string_replace(&manager->wallpaper_metadata, ctx->wallpaper.metadata);
    wallpaper_close_fd(ctx);
}

static void wallpaper_handle_get_metadata(struct wl_client *client, struct wl_resource *resource)
{
    treeland_personalization_wallpaper_context_v1_send_metadata(
        resource, manager->wallpaper_metadata ? manager->wallpaper_metadata : "");
}

static const struct treeland_personalization_wallpaper_context_v1_interface wallpaper_impl = {
    .set_fd = wallpaper_handle_set_fd,
    .set_identifier = wallpaper_handle_set_identifier,
    .set_output = wallpaper_handle_set_output,
    .set_on = wallpaper_handle_set_on,
    .set_isdark = wallpaper_handle_set_isdark,
    .commit = wallpaper_handle_commit,
    .get_metadata = wallpaper_handle_get_metadata,
    .destroy = context_handle_destroy,
};

/* Cursor context */

static void cursor_handle_set_theme(struct wl_client *client, struct wl_resource *resource,
                                    const char *name)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    string_replace(&ctx->pending_theme, name);
    ctx->pending_theme_set = true;
}

static void cursor_handle_get_theme(struct wl_client *client, struct wl_resource *resource)
{
    treeland_personalization_cursor_context_v1_send_theme(
        resource, manager->cursor_theme ? manager->cursor_theme : "");
}

static void cursor_handle_set_size(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t size)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    ctx->pending_size = size;
    ctx->pending_size_set = true;
}

static void cursor_handle_get_size(struct wl_client *client, struct wl_resource *resource)
{
    treeland_personalization_cursor_context_v1_send_size(resource, manager->cursor_size);
}

/**
 * @brief Commit the cursor configuration
 *
 * The protocol states that "if only one commit fails validation, the commit
 * will fail", so everything is validated before anything is stored. Once it
 * passes, the new values are broadcast to every client bound to a cursor
 * context.
 */
static void cursor_handle_commit(struct wl_client *client, struct wl_resource *resource)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }

    bool ok = true;
    if (ctx->pending_theme_set && (!ctx->pending_theme || ctx->pending_theme[0] == '\0')) {
        ok = false;
    }
    if (ctx->pending_size_set && ctx->pending_size == 0) {
        ok = false;
    }

    if (ok) {
        if (ctx->pending_theme_set) {
            string_replace(&manager->cursor_theme, ctx->pending_theme);
        }
        if (ctx->pending_size_set) {
            manager->cursor_size = ctx->pending_size;
        }

        struct personalization_context *other;
        wl_list_for_each(other, &manager->cursor_contexts, link) {
            if (ctx->pending_theme_set) {
                treeland_personalization_cursor_context_v1_send_theme(
                    other->resource, manager->cursor_theme ? manager->cursor_theme : "");
            }
            if (ctx->pending_size_set) {
                treeland_personalization_cursor_context_v1_send_size(other->resource,
                                                                     manager->cursor_size);
            }
        }
    }

    treeland_personalization_cursor_context_v1_send_verfity(resource, ok ? 1 : 0);

    string_replace(&ctx->pending_theme, NULL);
    ctx->pending_theme_set = false;
    ctx->pending_size_set = false;
}

static const struct treeland_personalization_cursor_context_v1_interface cursor_impl = {
    .set_theme = cursor_handle_set_theme,
    .get_theme = cursor_handle_get_theme,
    .set_size = cursor_handle_set_size,
    .get_size = cursor_handle_get_size,
    .commit = cursor_handle_commit,
    .destroy = context_handle_destroy,
};

/* Font context */

static void font_handle_set_font_size(struct wl_client *client, struct wl_resource *resource,
                                      uint32_t size)
{
    manager->font_size = size;

    struct personalization_context *ctx;
    wl_list_for_each(ctx, &manager->font_contexts, link) {
        treeland_personalization_font_context_v1_send_font_size(ctx->resource, manager->font_size);
    }
}

static void font_handle_get_font_size(struct wl_client *client, struct wl_resource *resource)
{
    treeland_personalization_font_context_v1_send_font_size(resource, manager->font_size);
}

static void font_handle_set_font(struct wl_client *client, struct wl_resource *resource,
                                 const char *font_name)
{
    string_replace(&manager->font, font_name);

    struct personalization_context *ctx;
    wl_list_for_each(ctx, &manager->font_contexts, link) {
        treeland_personalization_font_context_v1_send_font(ctx->resource,
                                                           manager->font ? manager->font : "");
    }
}

static void font_handle_get_font(struct wl_client *client, struct wl_resource *resource)
{
    treeland_personalization_font_context_v1_send_font(resource,
                                                       manager->font ? manager->font : "");
}

static void font_handle_set_monospace_font(struct wl_client *client, struct wl_resource *resource,
                                           const char *font_name)
{
    string_replace(&manager->monospace_font, font_name);

    struct personalization_context *ctx;
    wl_list_for_each(ctx, &manager->font_contexts, link) {
        treeland_personalization_font_context_v1_send_monospace_font(
            ctx->resource, manager->monospace_font ? manager->monospace_font : "");
    }
}

static void font_handle_get_monospace_font(struct wl_client *client, struct wl_resource *resource)
{
    treeland_personalization_font_context_v1_send_monospace_font(
        resource, manager->monospace_font ? manager->monospace_font : "");
}

static const struct treeland_personalization_font_context_v1_interface font_impl = {
    .set_font_size = font_handle_set_font_size,
    .get_font_size = font_handle_get_font_size,
    .set_font = font_handle_set_font,
    .get_font = font_handle_get_font,
    .set_monospace_font = font_handle_set_monospace_font,
    .get_monospace_font = font_handle_get_monospace_font,
    .destroy = context_handle_destroy,
};

/* Appearance context */

static void appearance_handle_set_round_corner_radius(struct wl_client *client,
                                                      struct wl_resource *resource, int32_t radius)
{
    if (radius < 0) {
        wl_resource_post_error(
            resource, TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_ROUND_CORNER_RADIUS,
            "invalid round corner radius %d", radius);
        return;
    }
    manager->round_corner_radius = radius;

    struct personalization_context *ctx;
    wl_list_for_each(ctx, &manager->appearance_contexts, link) {
        treeland_personalization_appearance_context_v1_send_round_corner_radius(
            ctx->resource, manager->round_corner_radius);
    }
}

static void appearance_handle_get_round_corner_radius(struct wl_client *client,
                                                      struct wl_resource *resource)
{
    treeland_personalization_appearance_context_v1_send_round_corner_radius(
        resource, manager->round_corner_radius);
}

static void appearance_handle_set_icon_theme(struct wl_client *client, struct wl_resource *resource,
                                             const char *theme)
{
    if (!theme || theme[0] == '\0') {
        wl_resource_post_error(resource,
                               TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_ICON_THEME,
                               "invalid icon theme");
        return;
    }
    string_replace(&manager->icon_theme, theme);

    struct personalization_context *ctx;
    wl_list_for_each(ctx, &manager->appearance_contexts, link) {
        treeland_personalization_appearance_context_v1_send_icon_theme(ctx->resource,
                                                                       manager->icon_theme);
    }
}

static void appearance_handle_get_icon_theme(struct wl_client *client, struct wl_resource *resource)
{
    treeland_personalization_appearance_context_v1_send_icon_theme(
        resource, manager->icon_theme ? manager->icon_theme : "");
}

static void appearance_handle_set_active_color(struct wl_client *client,
                                               struct wl_resource *resource, const char *color)
{
    if (!color || color[0] == '\0') {
        wl_resource_post_error(resource,
                               TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_ACTIVE_COLOR,
                               "invalid active color");
        return;
    }
    string_replace(&manager->active_color, color);

    struct personalization_context *ctx;
    wl_list_for_each(ctx, &manager->appearance_contexts, link) {
        treeland_personalization_appearance_context_v1_send_active_color(ctx->resource,
                                                                         manager->active_color);
    }
}

static void appearance_handle_get_active_color(struct wl_client *client,
                                               struct wl_resource *resource)
{
    treeland_personalization_appearance_context_v1_send_active_color(
        resource, manager->active_color ? manager->active_color : "");
}

static void appearance_handle_set_window_opacity(struct wl_client *client,
                                                 struct wl_resource *resource, uint32_t opacity)
{
    if (opacity > 255) {
        wl_resource_post_error(resource,
                               TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_WINDOW_OPACITY,
                               "invalid window opacity %u", opacity);
        return;
    }
    manager->window_opacity = opacity;

    struct personalization_context *ctx;
    wl_list_for_each(ctx, &manager->appearance_contexts, link) {
        treeland_personalization_appearance_context_v1_send_window_opacity(ctx->resource,
                                                                           manager->window_opacity);
    }
}

static void appearance_handle_get_window_opacity(struct wl_client *client,
                                                 struct wl_resource *resource)
{
    treeland_personalization_appearance_context_v1_send_window_opacity(resource,
                                                                       manager->window_opacity);
}

static void appearance_handle_set_window_theme_type(struct wl_client *client,
                                                    struct wl_resource *resource, uint32_t type)
{
    switch (type) {
    case TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_AUTO:
    case TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_LIGHT:
    case TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_DARK:
        break;
    default:
        wl_resource_post_error(
            resource, TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_WINDOW_THEME_TYPE,
            "invalid window theme type %u", type);
        return;
    }
    manager->window_theme_type = type;

    struct personalization_context *ctx;
    wl_list_for_each(ctx, &manager->appearance_contexts, link) {
        treeland_personalization_appearance_context_v1_send_window_theme_type(
            ctx->resource, manager->window_theme_type);
    }
}

static void appearance_handle_get_window_theme_type(struct wl_client *client,
                                                    struct wl_resource *resource)
{
    treeland_personalization_appearance_context_v1_send_window_theme_type(
        resource, manager->window_theme_type);
}

static void appearance_handle_set_window_titlebar_height(struct wl_client *client,
                                                         struct wl_resource *resource,
                                                         uint32_t height)
{
    manager->window_titlebar_height = height;

    struct personalization_context *ctx;
    wl_list_for_each(ctx, &manager->appearance_contexts, link) {
        treeland_personalization_appearance_context_v1_send_window_titlebar_height(
            ctx->resource, manager->window_titlebar_height);
    }
}

static void appearance_handle_get_window_titlebar_height(struct wl_client *client,
                                                         struct wl_resource *resource)
{
    treeland_personalization_appearance_context_v1_send_window_titlebar_height(
        resource, manager->window_titlebar_height);
}

static const struct treeland_personalization_appearance_context_v1_interface appearance_impl = {
    .set_round_corner_radius = appearance_handle_set_round_corner_radius,
    .get_round_corner_radius = appearance_handle_get_round_corner_radius,
    .set_icon_theme = appearance_handle_set_icon_theme,
    .get_icon_theme = appearance_handle_get_icon_theme,
    .set_active_color = appearance_handle_set_active_color,
    .get_active_color = appearance_handle_get_active_color,
    .set_window_opacity = appearance_handle_set_window_opacity,
    .get_window_opacity = appearance_handle_get_window_opacity,
    .set_window_theme_type = appearance_handle_set_window_theme_type,
    .get_window_theme_type = appearance_handle_get_window_theme_type,
    .set_window_titlebar_height = appearance_handle_set_window_titlebar_height,
    .get_window_titlebar_height = appearance_handle_get_window_titlebar_height,
    .destroy = context_handle_destroy,
};

/* Shared creation and teardown for the global contexts */

static void context_handle_resource_destroy(struct wl_resource *resource)
{
    struct personalization_context *ctx = wl_resource_get_user_data(resource);
    if (!ctx) {
        return;
    }
    wl_list_remove(&ctx->link);
    free(ctx->pending_theme);
    wallpaper_close_fd(ctx);
    free(ctx->wallpaper.metadata);
    free(ctx->wallpaper.identifier);
    free(ctx->wallpaper.output);
    free(ctx);
}

static struct personalization_context *context_create(struct wl_client *client,
                                                      struct wl_resource *manager_resource,
                                                      uint32_t id,
                                                      const struct wl_interface *interface,
                                                      const void *impl, struct wl_list *list)
{
    struct personalization_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        wl_resource_post_no_memory(manager_resource);
        return NULL;
    }
    ctx->wallpaper.fd = -1;

    uint32_t version = wl_resource_get_version(manager_resource);
    ctx->resource = wl_resource_create(client, interface, version, id);
    if (!ctx->resource) {
        free(ctx);
        wl_resource_post_no_memory(manager_resource);
        return NULL;
    }

    wl_resource_set_implementation(ctx->resource, impl, ctx, context_handle_resource_destroy);
    wl_list_insert(list, &ctx->link);
    return ctx;
}

/* Manager */

static void manager_get_window_context(struct wl_client *client, struct wl_resource *resource,
                                       uint32_t id, struct wl_resource *surface_resource)
{
    struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

    struct personalization_window_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        wl_resource_post_no_memory(resource);
        return;
    }

    uint32_t version = wl_resource_get_version(resource);
    ctx->resource = wl_resource_create(client, &treeland_personalization_window_context_v1_interface,
                                       version, id);
    if (!ctx->resource) {
        free(ctx);
        wl_resource_post_no_memory(resource);
        return;
    }

    ctx->surface = surface;
    wl_resource_set_implementation(ctx->resource, &window_impl, ctx,
                                   window_handle_resource_destroy);

    ctx->surface_destroy.notify = window_handle_surface_destroy;
    wl_signal_add(&surface->events.destroy, &ctx->surface_destroy);
    ctx->surface_map.notify = window_handle_surface_map;
    wl_signal_add(&surface->events.map, &ctx->surface_map);

    wl_list_insert(&manager->window_contexts, &ctx->link);
}

static void manager_get_wallpaper_context(struct wl_client *client, struct wl_resource *resource,
                                          uint32_t id)
{
    context_create(client, resource, id, &treeland_personalization_wallpaper_context_v1_interface,
                   &wallpaper_impl, &manager->wallpaper_contexts);
}

static void manager_get_cursor_context(struct wl_client *client, struct wl_resource *resource,
                                       uint32_t id)
{
    context_create(client, resource, id, &treeland_personalization_cursor_context_v1_interface,
                   &cursor_impl, &manager->cursor_contexts);
}

static void manager_get_font_context(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t id)
{
    context_create(client, resource, id, &treeland_personalization_font_context_v1_interface,
                   &font_impl, &manager->font_contexts);
}

static void manager_get_appearance_context(struct wl_client *client, struct wl_resource *resource,
                                           uint32_t id)
{
    context_create(client, resource, id, &treeland_personalization_appearance_context_v1_interface,
                   &appearance_impl, &manager->appearance_contexts);
}

static void manager_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct treeland_personalization_manager_v1_interface manager_impl = {
    .get_window_context = manager_get_window_context,
    .get_wallpaper_context = manager_get_wallpaper_context,
    .get_cursor_context = manager_get_cursor_context,
    .get_font_context = manager_get_font_context,
    .get_appearance_context = manager_get_appearance_context,
    .destroy = manager_handle_destroy,
};

static void personalization_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                         uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &treeland_personalization_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &manager_impl, manager, NULL);
}

static void manager_finish(void)
{
    if (!manager) {
        return;
    }
    free(manager->wallpaper_metadata);
    free(manager->cursor_theme);
    free(manager->font);
    free(manager->monospace_font);
    free(manager->icon_theme);
    free(manager->active_color);
    free(manager);
    manager = NULL;
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
    manager->global = NULL;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    manager_finish();
}

/**
 * @brief Create the @c treeland_personalization_manager_v1 global and manage
 *        its lifetime
 *
 * @param server (server*) the server instance
 * @return true on success, false on failure
 */
bool treeland_personalization_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct treeland_personalization_manager));
    if (!manager) {
        return false;
    }

    manager->global = wl_global_create(server->display,
                                       &treeland_personalization_manager_v1_interface,
                                       TREELAND_PERSONALIZATION_MANAGER_VERSION, manager,
                                       personalization_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_ERROR, "(Treeland Shim) Init: Failed to create treeland personalization "
                             "manager!!");
        free(manager);
        manager = NULL;
        return false;
    }

    wl_list_init(&manager->window_contexts);
    wl_list_init(&manager->wallpaper_contexts);
    wl_list_init(&manager->cursor_contexts);
    wl_list_init(&manager->font_contexts);
    wl_list_init(&manager->appearance_contexts);

    manager->cursor_size = DEFAULT_CURSOR_SIZE;
    manager->font_size = DEFAULT_FONT_SIZE;
    manager->window_opacity = DEFAULT_WINDOW_OPACITY;
    manager->window_theme_type = TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_AUTO;
    manager->window_titlebar_height = DEFAULT_TITLEBAR_HEIGHT;

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}
