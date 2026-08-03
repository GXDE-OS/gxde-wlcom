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
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

/**
 * @brief Which wire layout of treeland_personalization_manager_v1 to serve
 *
 * treeland-protocols 0.5.9 dropped @c get_wallpaper_context and the
 * @c treeland_personalization_wallpaper_context_v1 interface *without* bumping
 * the interface version, so the two releases share a name and a version but not
 * a request table. Wayland opcodes are positional, which makes the layouts
 * mutually exclusive and indistinguishable at bind time: the compositor has to
 * commit to whichever one its clients were built against, and getting it wrong
 * kills every DTK client at startup.
 *
 * Both layouts are served from the same vendored XML -- see
 * @c layout_derive_059() -- so switching is a matter of configuration rather
 * than of rebuilding. This requires the vendored XML to stay the 0.5.8
 * superset; once GXDE has moved to 0.5.9 for good, the whole switch and the
 * wallpaper context can go. The README has the full story.
 */
enum personalization_layout {
    PERSONALIZATION_LAYOUT_058, /**< <= 0.5.8, has get_wallpaper_context */
    PERSONALIZATION_LAYOUT_059, /**< >= 0.5.9, no wallpaper context */
};

struct treeland_personalization_manager {
    struct wl_global *global;

    /* Layout served to clients, plus the derived 0.5.9 request table. The
     * 0.5.8 one is the scanner-generated table and needs nothing here. */
    enum personalization_layout layout;
    struct wl_interface interface_059;
    struct wl_message *requests_059;

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

/**
 * @brief Whether a boolean environment variable is switched on
 *
 * Anything other than the accepted spellings -- including an empty value --
 * counts as off, so an exported-but-blank variable does not silently turn a
 * feature off.
 */
static bool env_is_on(const char *name)
{
    const char *value = getenv(name);
    if (!value || !*value) {
        return false;
    }
    return !strcasecmp(value, "TRUE") || !strcasecmp(value, "ON") || !strcasecmp(value, "YES") ||
           !strcmp(value, "1");
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

/* Layout selection */

/**
 * @brief The 0.5.9 manager implementation
 *
 * Deliberately a bare struct rather than the generated
 * @c treeland_personalization_manager_v1_interface: it mirrors what
 * wayland-scanner would emit for the 0.5.9 XML, i.e. the members line up
 * one-to-one with the request table derived in @c layout_derive_059(), which is
 * how libwayland dispatches. Same handlers, minus the wallpaper one.
 */
struct manager_implementation_059 {
    void (*get_window_context)(struct wl_client *, struct wl_resource *, uint32_t,
                               struct wl_resource *);
    void (*get_cursor_context)(struct wl_client *, struct wl_resource *, uint32_t);
    void (*get_font_context)(struct wl_client *, struct wl_resource *, uint32_t);
    void (*get_appearance_context)(struct wl_client *, struct wl_resource *, uint32_t);
    void (*destroy)(struct wl_client *, struct wl_resource *);
};

static const struct manager_implementation_059 manager_impl_059 = {
    .get_window_context = manager_get_window_context,
    .get_cursor_context = manager_get_cursor_context,
    .get_font_context = manager_get_font_context,
    .get_appearance_context = manager_get_appearance_context,
    .destroy = manager_handle_destroy,
};

/**
 * @brief Build the 0.5.9 request table out of the generated 0.5.8 one
 *
 * Copying the @c wl_message entries beats vendoring a second XML: everything
 * except the dropped request comes straight from the scanner-generated table,
 * so the two layouts cannot drift apart.
 *
 * @return true if the table was derived, false if the vendored XML is not the
 *         0.5.8 superset this needs
 */
static bool layout_derive_059(void)
{
    const struct wl_interface *src = &treeland_personalization_manager_v1_interface;

    if (src->method_count < 2 || strcmp(src->methods[1].name, "get_wallpaper_context") != 0) {
        kywc_log(KYWC_ERROR,
                 "(Treeland Shim) Personalization: cannot derive the 0.5.9 layout, request 1 of the "
                 "vendored XML is '%s' rather than get_wallpaper_context",
                 src->method_count > 1 ? src->methods[1].name : "(none)");
        return false;
    }

    manager->requests_059 = calloc(src->method_count - 1, sizeof(struct wl_message));
    if (!manager->requests_059) {
        return false;
    }

    /* Everything but the wallpaper request, closing the gap it leaves */
    manager->requests_059[0] = src->methods[0];
    for (int i = 2; i < src->method_count; i++) {
        manager->requests_059[i - 1] = src->methods[i];
    }

    manager->interface_059 = *src;
    manager->interface_059.method_count = src->method_count - 1;
    manager->interface_059.methods = manager->requests_059;
    return true;
}

/**
 * @brief Search a file for a byte sequence
 *
 * Shared objects are binary, so the haystack has embedded NULs and the str*
 * family is out. Chunks overlap by @p needle length - 1 so a match straddling a
 * boundary is still found.
 */
static bool file_contains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }

    const size_t nlen = strlen(needle);
    const size_t chunk = 64 * 1024;
    char *buf = malloc(chunk + nlen);
    if (!buf) {
        fclose(file);
        return false;
    }

    bool found = false;
    size_t carry = 0;
    size_t read;
    while (!found && (read = fread(buf + carry, 1, chunk, file)) > 0) {
        const size_t total = carry + read;
        for (const char *p = buf; total >= nlen && (size_t)(p - buf) + nlen <= total; p++) {
            const char *hit = memchr(p, needle[0], total - (size_t)(p - buf) - nlen + 1);
            if (!hit) {
                break;
            }
            if (memcmp(hit, needle, nlen) == 0) {
                found = true;
                break;
            }
            p = hit;
        }
        carry = total < nlen - 1 ? total : nlen - 1;
        memmove(buf, buf + total - carry, carry);
    }

    free(buf);
    fclose(file);
    return found;
}

#define WALLPAPER_INTERFACE_SYMBOL "treeland_personalization_wallpaper_context_v1"

/* DTK drives this protocol on GXDE, so its build is the one to match. Its
 * generated code carries the interface name of every request it can issue,
 * which makes the string a reliable stand-in for "which XML was this built
 * against". Both the Qt5 and the Qt6 stack are probed, multiarch included. */
static const char *const dtk_lib_patterns[] = {
    "/usr/lib/libdtk6gui.so.*", "/usr/lib/*/libdtk6gui.so.*",
    "/usr/lib/libdtkgui.so.*",  "/usr/lib/*/libdtkgui.so.*",
};

static bool layout_from_env(enum personalization_layout *layout)
{
    const char *value = getenv("GXDE_WLCOM_PERSONALIZATION");
    if (!value || !*value) {
        return false;
    }

    if (!strcmp(value, "058") || !strcmp(value, "0.5.8")) {
        *layout = PERSONALIZATION_LAYOUT_058;
        return true;
    }
    if (!strcmp(value, "059") || !strcmp(value, "0.5.9")) {
        *layout = PERSONALIZATION_LAYOUT_059;
        return true;
    }

    kywc_log(KYWC_WARN,
             "(Treeland Shim) Personalization: ignoring GXDE_WLCOM_PERSONALIZATION='%s', expected "
             "058 or 059",
             value);
    return false;
}

/**
 * @brief Work out which layout this system's clients expect
 *
 * Note that the installed treeland-protocols version is only a fallback hint:
 * the XML is a build-time input, so what matters is when DTK was last compiled,
 * not which protocol package happens to be unpacked right now.
 */
static enum personalization_layout layout_detect(void)
{
    enum personalization_layout layout;
    if (layout_from_env(&layout)) {
        kywc_log(KYWC_INFO,
                 "(Treeland Shim) Personalization: layout forced to %s by "
                 "GXDE_WLCOM_PERSONALIZATION",
                 layout == PERSONALIZATION_LAYOUT_058 ? "0.5.8" : "0.5.9");
        return layout;
    }

    int probed = 0, with_wallpaper = 0;
    for (size_t i = 0; i < sizeof(dtk_lib_patterns) / sizeof(dtk_lib_patterns[0]); i++) {
        glob_t found;
        if (glob(dtk_lib_patterns[i], 0, NULL, &found) != 0) {
            continue;
        }
        for (size_t j = 0; j < found.gl_pathc; j++) {
            bool has = file_contains(found.gl_pathv[j], WALLPAPER_INTERFACE_SYMBOL);
            kywc_log(KYWC_DEBUG, "(Treeland Shim) Personalization: probing %s -> %s",
                     found.gl_pathv[j], has ? "0.5.8" : "0.5.9");
            probed++;
            with_wallpaper += has;
        }
        globfree(&found);
    }

    if (probed == 0) {
        /* No DTK here -- a build chroot or a minimal install. Fall back to the
         * protocol package, and to the superset if that is missing too. */
        static const char *xml =
            "/usr/share/treeland-protocols/treeland-personalization-manager-v1.xml";
        if (access(xml, R_OK) != 0) {
            kywc_log(KYWC_INFO, "(Treeland Shim) Personalization: nothing to probe, defaulting to "
                                "the 0.5.8 layout");
            return PERSONALIZATION_LAYOUT_058;
        }
        bool has = file_contains(xml, "get_wallpaper_context");
        kywc_log(KYWC_INFO,
                 "(Treeland Shim) Personalization: no DTK found, following treeland-protocols -> "
                 "%s layout",
                 has ? "0.5.8" : "0.5.9");
        return has ? PERSONALIZATION_LAYOUT_058 : PERSONALIZATION_LAYOUT_059;
    }

    if (with_wallpaper > 0 && with_wallpaper < probed) {
        kywc_log(KYWC_ERROR,
                 "(Treeland Shim) Personalization: DTK libraries disagree, %d of %d still reference "
                 "the wallpaper context. Serving 0.5.8, so the ones already rebuilt against 0.5.9 "
                 "will fail to start -- finish the rebuild, or force the other layout with "
                 "GXDE_WLCOM_PERSONALIZATION=059",
                 with_wallpaper, probed);
    } else {
        kywc_log(KYWC_INFO,
                 "(Treeland Shim) Personalization: probed %d DTK librar%s, wallpaper context %s -> "
                 "using the %s layout",
                 probed, probed == 1 ? "y" : "ies", with_wallpaper ? "referenced" : "absent",
                 with_wallpaper ? "0.5.8" : "0.5.9");
    }

    return with_wallpaper > 0 ? PERSONALIZATION_LAYOUT_058 : PERSONALIZATION_LAYOUT_059;
}

/* Manager global */

static bool layout_is_059(void)
{
    return manager->layout == PERSONALIZATION_LAYOUT_059 && manager->requests_059;
}

static void personalization_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                         uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client,
                           layout_is_059() ? &manager->interface_059
                                           : &treeland_personalization_manager_v1_interface,
                           version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(
        resource, layout_is_059() ? (const void *)&manager_impl_059 : (const void *)&manager_impl,
        manager, NULL);
}

static void manager_finish(void)
{
    if (!manager) {
        return;
    }
    free(manager->requests_059);
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
 * Setting @c GXWM_DONOT_BROADCAST_TLPM turns the protocol off entirely: no
 * global is advertised, so clients fall back to their own defaults instead of
 * failing. That is the escape hatch for the day a treeland-protocols change
 * breaks this implementation again -- a desktop without blur and custom corner
 * radii still beats one that cannot start.
 *
 * @param server (server*) the server instance
 * @return true on success, false on failure
 */
bool treeland_personalization_manager_create(struct server *server)
{
    /* Logged above the default level: with the global gone the symptom is
     * silent -- windows simply stop honouring client-side personalization --
     * so whoever is debugging that needs to find the reason without -V. */
    if (env_is_on("GXWM_DONOT_BROADCAST_TLPM")) {
        kywc_log(KYWC_WARN, "(Treeland Shim) Personalization: global not advertised, disabled by "
                            "GXWM_DONOT_BROADCAST_TLPM");
        return true;
    }

    manager = calloc(1, sizeof(struct treeland_personalization_manager));
    if (!manager) {
        return false;
    }

    /* Decide the wire layout before advertising anything: the global carries
     * the request table, so it has to be the right one from the start. */
    manager->layout = layout_detect();
    if (manager->layout == PERSONALIZATION_LAYOUT_059 && !layout_derive_059()) {
        kywc_log(KYWC_ERROR, "(Treeland Shim) Personalization: falling back to the 0.5.8 layout, "
                             "clients built against 0.5.9 will not start");
        manager->layout = PERSONALIZATION_LAYOUT_058;
    }

    manager->global = wl_global_create(server->display,
                                       layout_is_059()
                                           ? &manager->interface_059
                                           : &treeland_personalization_manager_v1_interface,
                                       TREELAND_PERSONALIZATION_MANAGER_VERSION, manager,
                                       personalization_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_ERROR, "(Treeland Shim) Init: Failed to create treeland personalization "
                             "manager!!");
        free(manager->requests_059);
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
