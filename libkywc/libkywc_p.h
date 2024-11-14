// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _LIBKYWC_HEADER_P_H_
#define _LIBKYWC_HEADER_P_H_

#include "libkywc.h"

struct ky_context_provider {
    struct wl_list link;
    enum kywc_context_capability capability;

    bool (*bind)(struct ky_context_provider *provider, struct wl_registry *registry, uint32_t name,
                 const char *interface, uint32_t version);
    void (*destroy)(struct ky_context_provider *provider);
    void *data;
};

struct _kywc_context {
    struct wl_display *display;
    struct wl_registry *registry;
    bool own_display;

    uint32_t capabilities;
    const struct kywc_context_interface *impl;
    void *user_data;

    struct wl_list providers;

    struct ky_workspace_manager *workspace;
    struct ky_output_manager *output;
    struct ky_toplevel_manager *toplevel;
    struct ky_thumbnail_manager *thumbnail;
};

bool ky_context_add_provider(kywc_context *ctx, struct ky_context_provider *provider,
                             void *manager);

/**
 * workspace
 */
struct ky_workspace_manager {
    kywc_context *ctx;
    struct wl_list workspaces;

    void (*create_workspace)(struct ky_workspace_manager *manager, const char *name,
                             uint32_t position);
    void (*destroy)(struct ky_workspace_manager *manager);
    void *data;
};

struct ky_workspace {
    kywc_workspace base;

    struct ky_workspace_manager *manager;
    struct wl_list link;

    const struct kywc_workspace_interface *impl;
    void *user_data;

    void (*set_position)(struct ky_workspace *workspace, uint32_t position);
    void (*activate)(struct ky_workspace *workspace);
    void (*remove)(struct ky_workspace *workspace);
    void (*destroy)(struct ky_workspace *workspace);
    void *data;

    uint32_t pending_mask;
    bool newly_added;
};

struct ky_workspace_manager *ky_workspace_manager_create(kywc_context *ctx);

void ky_workspace_manager_destroy(struct ky_workspace_manager *manager);

void ky_workspace_manager_update_states(struct ky_workspace_manager *manager);

struct ky_workspace *ky_workspace_create(struct ky_workspace_manager *manager, const char *uuid);

void ky_workspace_destroy(struct ky_workspace *workspace);

void ky_workspace_update_name(struct ky_workspace *workspace, const char *name);

void ky_workspace_update_position(struct ky_workspace *workspace, uint32_t position);

void ky_workspace_update_activated(struct ky_workspace *workspace, bool activated);

/**
 * output
 */
struct ky_output_manager {
    kywc_context *ctx;
    struct wl_list outputs;

    struct ky_output *primary;

    void (*apply)(struct ky_output_manager *manager);
    void (*destroy)(struct ky_output_manager *manager);
    void *data;
};

struct ky_output {
    kywc_output base;

    struct ky_output_manager *manager;
    struct wl_list link;

    const struct kywc_output_interface *impl;
    void *user_data;

    void (*destroy)(struct ky_output *output);
    void *data;

    uint32_t pending_mask;
    bool newly_added;
};

struct ky_output_mode {
    struct kywc_output_mode base;
    struct ky_output *output;

    void (*destroy)(struct ky_output_mode *mode);
    void *data;
};

struct ky_output_manager *ky_output_manager_create(kywc_context *ctx);

void ky_output_manager_destroy(struct ky_output_manager *manager);

void ky_output_manager_update_states(struct ky_output_manager *manager);

void ky_output_manager_update_primary(struct ky_output_manager *manager, struct ky_output *primary);

struct ky_output *ky_output_create(struct ky_output_manager *manager, const char *uuid);

void ky_output_destroy(struct ky_output *output);

void ky_output_set_name(struct ky_output *output, const char *name);

void ky_output_set_make(struct ky_output *output, const char *make);

void ky_output_set_model(struct ky_output *output, const char *model);

void ky_output_set_serial_number(struct ky_output *output, const char *serial_number);

void ky_output_set_description(struct ky_output *output, const char *description);

void ky_output_set_physical_size(struct ky_output *output, int width, int height);

void ky_output_set_capabilities(struct ky_output *output, uint32_t capabilities);

struct ky_output_mode *ky_output_mode_create(struct ky_output *output);

void ky_output_mode_set_size(struct ky_output_mode *mode, int width, int height);

void ky_output_mode_set_refresh(struct ky_output_mode *mode, int refresh);

void ky_output_mode_set_preferred(struct ky_output_mode *mode);

void ky_output_mode_destroy(struct ky_output_mode *mode);

void ky_output_update_enabled(struct ky_output *output, bool enabled);

void ky_output_update_current_mode(struct ky_output *output, struct ky_output_mode *mode);

void ky_output_update_position(struct ky_output *output, int x, int y);

void ky_output_update_transform(struct ky_output *output, int transform);

void ky_output_update_scale(struct ky_output *output, float scale);

void ky_output_update_power(struct ky_output *output, bool power);

void ky_output_update_primary(struct ky_output *output, bool primary);

void ky_output_update_brightness(struct ky_output *output, uint32_t brightness);

void ky_output_update_color_temp(struct ky_output *output, uint32_t color_temp);

/**
 * toplevel
 */
struct ky_toplevel_manager {
    kywc_context *ctx;
    struct wl_list toplevels;

    void (*destroy)(struct ky_toplevel_manager *manager);
    void *data;
};

struct ky_toplevel {
    kywc_toplevel base;

    struct ky_toplevel_manager *manager;
    struct wl_list link;

    const struct kywc_toplevel_interface *impl;
    void *user_data;

    void (*set_maximized)(struct ky_toplevel *toplevel, const char *output);
    void (*unset_maximized)(struct ky_toplevel *toplevel);
    void (*set_minimized)(struct ky_toplevel *toplevel);
    void (*unset_minimized)(struct ky_toplevel *toplevel);
    void (*set_fullscreen)(struct ky_toplevel *toplevel, const char *output);
    void (*unset_fullscreen)(struct ky_toplevel *toplevel);
    void (*activate)(struct ky_toplevel *toplevel);
    void (*close)(struct ky_toplevel *toplevel);
    void (*enter_workspace)(struct ky_toplevel *toplevel, const char *workspace);
    void (*leave_workspace)(struct ky_toplevel *toplevel, const char *workspace);
    void (*move_to_workspace)(struct ky_toplevel *toplevel, const char *workspace);
    void (*move_to_output)(struct ky_toplevel *toplevel, const char *output);
    void (*destroy)(struct ky_toplevel *toplevel);
    void *data;

    uint32_t pending_mask;
    bool newly_added;
};

struct ky_toplevel_manager *ky_toplevel_manager_create(kywc_context *ctx);

void ky_toplevel_manager_destroy(struct ky_toplevel_manager *manager);

struct ky_toplevel *ky_toplevel_create(struct ky_toplevel_manager *manager, const char *uuid);

void ky_toplevel_destroy(struct ky_toplevel *toplevel);

void ky_toplevel_update_states(struct ky_toplevel *toplevel);

void ky_toplevel_set_capabilities(struct ky_toplevel *toplevel, uint32_t capabilities);

void ky_toplevel_update_title(struct ky_toplevel *toplevel, const char *title);

void ky_toplevel_update_app_id(struct ky_toplevel *toplevel, const char *app_id);

void ky_toplevel_update_primary_output(struct ky_toplevel *toplevel, const char *output_id);

void ky_toplevel_update_maximized(struct ky_toplevel *toplevel, bool maximized);

void ky_toplevel_update_minimized(struct ky_toplevel *toplevel, bool minimized);

void ky_toplevel_update_activated(struct ky_toplevel *toplevel, bool activated);

void ky_toplevel_update_fullscreen(struct ky_toplevel *toplevel, bool fullscreen);

void ky_toplevel_update_parent(struct ky_toplevel *toplevel, struct ky_toplevel *parent);

void ky_toplevel_update_icon(struct ky_toplevel *toplevel, const char *icon);

void ky_toplevel_update_geometry(struct ky_toplevel *toplevel, int32_t x, int32_t y, uint32_t width,
                                 uint32_t height);

void ky_toplevel_enter_workspace(struct ky_toplevel *toplevel, const char *workspace);

void ky_toplevel_leave_workspace(struct ky_toplevel *toplevel, const char *workspace);

/**
 * thumbnail
 */
struct ky_thumbnail;

struct ky_thumbnail_manager {
    kywc_context *ctx;
    struct wl_list thumbnails;

    void (*capture_output)(struct ky_thumbnail_manager *manager, struct ky_thumbnail *thumbnail,
                           const char *uuid);
    void (*capture_workspace)(struct ky_thumbnail_manager *manager, struct ky_thumbnail *thumbnail,
                              const char *uuid, const char *output);
    void (*capture_toplevel)(struct ky_thumbnail_manager *manager, struct ky_thumbnail *thumbnail,
                             const char *uuid, bool without_decoration);
    void (*destroy)(struct ky_thumbnail_manager *manager);
    void *data;
};

struct ky_thumbnail {
    kywc_thumbnail base;

    struct ky_thumbnail_manager *manager;
    struct wl_list link;

    const struct kywc_thumbnail_interface *impl;
    void *user_data;

    void (*destroy)(struct ky_thumbnail *thumbnail);
    void *data;

    struct kywc_thumbnail_buffer buffer;
};

struct ky_thumbnail_manager *ky_thumbnail_manager_create(kywc_context *ctx);

void ky_thumbnail_manager_destroy(struct ky_thumbnail_manager *manager);

void ky_thumbnail_destroy(struct ky_thumbnail *thumbnail);

void ky_thumbnail_update_buffer(struct ky_thumbnail *thumbnail,
                                const struct kywc_thumbnail_buffer *buffer, bool *want_buffer);

#endif /* _LIBKYWC_HEADER_P_H_ */
