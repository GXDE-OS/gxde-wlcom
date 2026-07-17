// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _LIBKYWC_HEADER_H_
#define _LIBKYWC_HEADER_H_

#include <stdbool.h>
#include <wayland-client.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _kywc_context kywc_context;
typedef struct _kywc_output kywc_output;
typedef struct _kywc_toplevel kywc_toplevel;
typedef struct _kywc_workspace kywc_workspace;
typedef struct _kywc_thumbnail kywc_thumbnail;

enum kywc_context_capability {
    KYWC_CONTEXT_CAPABILITY_OUTPUT = 1 << 0,
    KYWC_CONTEXT_CAPABILITY_TOPLEVEL = 1 << 1,
    KYWC_CONTEXT_CAPABILITY_WORKSPACE = 1 << 2,
    KYWC_CONTEXT_CAPABILITY_THUMBNAIL = 1 << 3,
    /* with multi-plane support */
    KYWC_CONTEXT_CAPABILITY_THUMBNAIL_EXT = 1 << 4,
    /* detect GXDE-Wlcom compositor by gxde-identifier-v1 */
    KYWC_CONTEXT_CAPABILITY_IDENTIFIER = 1 << 5,
};

struct kywc_context_interface {
    /* called when context is created successfully but before wayland roundtrip */
    void (*create)(kywc_context *ctx, void *data);
    void (*destroy)(kywc_context *ctx, void *data);
    void (*new_output)(kywc_context *ctx, kywc_output *output, void *data);
    void (*new_toplevel)(kywc_context *ctx, kywc_toplevel *toplevel, void *data);
    void (*new_workspace)(kywc_context *ctx, kywc_workspace *workspace, void *data);
};

/**
 * Create a kywc context with the wayland display name.
 */
kywc_context *kywc_context_create(const char *name, uint32_t capabilities,
                                  const struct kywc_context_interface *impl, void *data);
/**
 * Create a kywc context with the exist wayland display.
 */
kywc_context *kywc_context_create_by_display(struct wl_display *display, uint32_t capabilities,
                                             const struct kywc_context_interface *impl, void *data);

struct wl_display *kywc_context_get_display(kywc_context *ctx);

void kywc_context_set_user_data(kywc_context *ctx, void *data);

void *kywc_context_get_user_data(kywc_context *ctx);

/**
 * Get the fd, work with kywc_context_process.
 */
int kywc_context_get_fd(kywc_context *ctx);

int kywc_context_process(kywc_context *ctx);

/**
 * Use the internal event loop in kywc context.
 */
void kywc_context_dispatch(kywc_context *ctx);

void kywc_context_destroy(kywc_context *ctx);

/**
 * GXDE WM identifier (gxde-identifier-v1)
 *
 * When the compositor advertises the gxde_identifier_v1 global, the client
 * can confirm it is running under GXDE-Wlcom (or a 100% compatible compositor)
 */

/**
 * Check whether the compositor is GXDE-Wlcom (or 100% compatible).
 */
bool kywc_context_is_gxde_wlcom(kywc_context *ctx);

/**
 * Get the WM version.
 */
const char *kywc_context_get_wm_version(kywc_context *ctx);

/**
 * workspace or virtual desktop
 */
struct _kywc_workspace {
    const char *uuid;
    const char *name;
    uint32_t position;
    bool activated;
};

enum kywc_workspace_state_mask {
    KYWC_WORKSPACE_STATE_NAME = 1 << 0,
    KYWC_WORKSPACE_STATE_POSITION = 1 << 1,
    KYWC_WORKSPACE_STATE_ACTIVATED = 1 << 2,
};

struct kywc_workspace_interface {
    void (*state)(kywc_workspace *workspace, uint32_t mask);
    void (*destroy)(kywc_workspace *workspace);
};

void kywc_workspace_set_interface(kywc_workspace *workspace,
                                  const struct kywc_workspace_interface *impl);

/* return true if need to break the loop */
typedef bool (*kywc_workspace_iterator_func_t)(kywc_workspace *workspace, void *data);

void kywc_context_for_each_workspace(kywc_context *ctx, kywc_workspace_iterator_func_t iterator,
                                     void *data);

kywc_workspace *kywc_context_find_workspace(kywc_context *ctx, const char *uuid);

kywc_context *kywc_workspace_get_context(kywc_workspace *workspace);

void kywc_workspace_create(kywc_context *ctx, const char *name, uint32_t position);

void kywc_workspace_remove(kywc_workspace *workspace);

void kywc_workspace_set_position(kywc_workspace *workspace, uint32_t position);

void kywc_workspace_activate(kywc_workspace *workspace);

void kywc_workspace_set_user_data(kywc_workspace *workspace, void *data);

void *kywc_workspace_get_user_data(kywc_workspace *workspace);

/**
 * output
 */
enum kywc_output_capability {
    KYWC_OUTPUT_CAPABILITY_POWER = 1 << 0,
    KYWC_OUTPUT_CAPABILITY_BRIGHTNESS = 1 << 1,
    KYWC_OUTPUT_CAPABILITY_COLOR_TEMP = 1 << 2,
};

struct kywc_output_mode {
    int32_t width, height;
    int32_t refresh; // mHz
    bool preferred;
    struct wl_list link;
};

struct _kywc_output {
    const char *uuid;
    /* props */
    const char *name;
    const char *make, *model, *serial, *description;
    int32_t physical_width, physical_height;
    uint32_t capabilities;

    struct wl_list modes;

    /* states */
    struct kywc_output_mode *mode; // may be NULL
    int32_t x, y, width, height;
    int32_t transform;
    float scale;

    bool enabled, power, primary;
    uint32_t brightness;
    uint32_t color_temp;
};

enum kywc_output_state_mask {
    KYWC_OUTPUT_STATE_ENABLED = 1 << 0,
    KYWC_OUTPUT_STATE_MODE = 1 << 1,
    KYWC_OUTPUT_STATE_POSITION = 1 << 2,
    KYWC_OUTPUT_STATE_TRANSFORM = 1 << 3,
    KYWC_OUTPUT_STATE_SCALE = 1 << 4,
    KYWC_OUTPUT_STATE_POWER = 1 << 5,
    KYWC_OUTPUT_STATE_PRIMARY = 1 << 6,
    KYWC_OUTPUT_STATE_BRIGHTNESS = 1 << 7,
    KYWC_OUTPUT_STATE_COLOR_TEMP = 1 << 8,
};

struct kywc_output_interface {
    void (*state)(kywc_output *output, uint32_t mask);
    void (*destroy)(kywc_output *output);
};

void kywc_output_set_interface(kywc_output *output, const struct kywc_output_interface *impl);

typedef bool (*kywc_output_iterator_func_t)(kywc_output *output, void *data);

void kywc_context_for_each_output(kywc_context *ctx, kywc_output_iterator_func_t iterator,
                                  void *data);

kywc_context *kywc_output_get_context(kywc_output *output);

kywc_output *kywc_context_find_output(kywc_context *ctx, const char *uuid);

void kywc_output_set_user_data(kywc_output *output, void *data);

void *kywc_output_get_user_data(kywc_output *output);

/**
 * toplevel or window
 */
enum kywc_toplevel_capability {
    KYWC_TOPLEVEL_CAPABILITY_SKIP_TASKBAR = 1 << 0,
    KYWC_TOPLEVEL_CAPABILITY_SKIP_SWITCHER = 1 << 1,
};

#define MAX_WORKSPACES 15

struct _kywc_toplevel {
    const char *uuid;

    const char *title, *app_id;
    const char *icon;

    /* parent toplevel, NULL if has no parent */
    kywc_toplevel *parent;
    /* output the toplevel most on */
    const char *primary_output;

    /* workspaces the toplevel on, simply use array here */
    const char *workspaces[MAX_WORKSPACES];

    int32_t x, y;
    uint32_t width, height;

    uint32_t capabilities;
    /* state */
    bool activated, minimized, maximized, fullscreen;

    uint32_t pid;
};

enum kywc_toplevel_state_mask {
    KYWC_TOPLEVEL_STATE_APP_ID = 1 << 0,
    KYWC_TOPLEVEL_STATE_TITLE = 1 << 1,
    KYWC_TOPLEVEL_STATE_ACTIVATED = 1 << 2,
    KYWC_TOPLEVEL_STATE_MINIMIZED = 1 << 3,
    KYWC_TOPLEVEL_STATE_MAXIMIZED = 1 << 4,
    KYWC_TOPLEVEL_STATE_FULLSCREEN = 1 << 5,
    KYWC_TOPLEVEL_STATE_PRIMARY_OUTPUT = 1 << 6,
    KYWC_TOPLEVEL_STATE_WORKSPACE = 1 << 7,
    KYWC_TOPLEVEL_STATE_PARENT = 1 << 8,
    KYWC_TOPLEVEL_STATE_ICON = 1 << 9,
    KYWC_TOPLEVEL_STATE_POSITION = 1 << 10,
    KYWC_TOPLEVEL_STATE_SIZE = 1 << 11,
};

struct kywc_toplevel_interface {
    void (*state)(kywc_toplevel *toplevel, uint32_t mask);
    void (*destroy)(kywc_toplevel *toplevel);
};

void kywc_toplevel_set_interface(kywc_toplevel *toplevel,
                                 const struct kywc_toplevel_interface *impl);

typedef bool (*kywc_toplevel_iterator_func_t)(kywc_toplevel *toplevel, void *data);

void kywc_context_for_each_toplevel(kywc_context *ctx, kywc_toplevel_iterator_func_t iterator,
                                    void *data);

kywc_context *kywc_toplevel_get_context(kywc_toplevel *toplevel);

kywc_toplevel *kywc_context_find_toplevel(kywc_context *ctx, const char *uuid);

bool kywc_toplevel_has_children(kywc_toplevel *toplevel);

void kywc_toplevel_set_maximized(kywc_toplevel *toplevel, const char *output);

void kywc_toplevel_unset_maximized(kywc_toplevel *toplevel);

void kywc_toplevel_set_minimized(kywc_toplevel *toplevel);

void kywc_toplevel_unset_minimized(kywc_toplevel *toplevel);

void kywc_toplevel_set_fullscreen(kywc_toplevel *toplevel, const char *output);

void kywc_toplevel_unset_fullscreen(kywc_toplevel *toplevel);

void kywc_toplevel_activate(kywc_toplevel *toplevel);

void kywc_toplevel_close(kywc_toplevel *toplevel);

void kywc_toplevel_enter_workspace(kywc_toplevel *toplevel, const char *workspace);

void kywc_toplevel_leave_workspace(kywc_toplevel *toplevel, const char *workspace);

void kywc_toplevel_move_to_workspace(kywc_toplevel *toplevel, const char *workspace);

void kywc_toplevel_move_to_output(kywc_toplevel *toplevel, const char *output);

void kywc_toplevel_set_position(kywc_toplevel *toplevel, int32_t x, int32_t y);

void kywc_toplevel_set_size(kywc_toplevel *toplevel, uint32_t width, uint32_t height);

void kywc_toplevel_set_user_data(kywc_toplevel *toplevel, void *data);

void *kywc_toplevel_get_user_data(kywc_toplevel *toplevel);

/**
 * thumbnail for output, toplevel and workspace
 */
enum kywc_thumbnail_type {
    KYWC_THUMBNAIL_TYPE_OUTPUT,
    KYWC_THUMBNAIL_TYPE_TOPLEVEL,
    KYWC_THUMBNAIL_TYPE_WORKSPACE,
};

struct _kywc_thumbnail {
    enum kywc_thumbnail_type type;
    const char *source_uuid;
    const char *output_uuid; // only used when workspace
};

enum kywc_thumbnail_buffer_flag {
    /**
     * memfd: use mmap and munmap
     * dmabuf: use egl import dmabuf is better, map can't work when has modifier
     */
    KYWC_THUMBNAIL_BUFFER_IS_DMABUF = 1 << 0,
    /**
     * buffer is reused, so we can skip the import sometimes
     */
    KYWC_THUMBNAIL_BUFFER_IS_REUSED = 1 << 1,
};

struct kywc_thumbnail_buffer {
    int32_t fd;              // fd is closed in libkywc after buffer callback
    uint32_t format;         // drm fourcc
    uint32_t width, height;  // in pixels
    uint32_t offset, stride; // in bytes
    uint64_t modifier;       // only used when dmabuf
    uint32_t flags;          // enum kywc_thumbnail_buffer_flag

    uint32_t n_planes;
    // planes attributes if multi-planes
    struct {
        int32_t fd;
        uint32_t offset, stride;
    } planes[4];
};

struct kywc_thumbnail_interface {
    /**
     * return true if want buffer callback again when content is changed later,
     * otherwise destroy callback is called to destroy this thumbnail.
     */
    bool (*buffer)(kywc_thumbnail *thumbnail, const struct kywc_thumbnail_buffer *buffer,
                   void *data);
    /**
     * no need to call kywc_thumbnail_destroy
     */
    void (*destroy)(kywc_thumbnail *thumbnail, void *data);
};

/**
 * output_uuid is needed when create a workspace thumbnail.
 * buffer callback will be called by libkywc after kywc_thumbnail_create.
 */
kywc_thumbnail *kywc_thumbnail_create(kywc_context *ctx, enum kywc_thumbnail_type type,
                                      const char *source_uuid, const char *output_uuid,
                                      const struct kywc_thumbnail_interface *impl, void *data);

kywc_thumbnail *kywc_thumbnail_create_from_output(kywc_context *ctx, const char *source_uuid,
                                                  const struct kywc_thumbnail_interface *impl,
                                                  void *data);

kywc_thumbnail *kywc_thumbnail_create_from_toplevel(kywc_context *ctx, const char *source_uuid,
                                                    bool without_decoration,
                                                    const struct kywc_thumbnail_interface *impl,
                                                    void *data);

kywc_thumbnail *kywc_thumbnail_create_from_workspace(kywc_context *ctx, const char *source_uuid,
                                                     const char *output_uuid,
                                                     const struct kywc_thumbnail_interface *impl,
                                                     void *data);

kywc_context *kywc_thumbnail_get_context(kywc_thumbnail *thumbnail);

void kywc_thumbnail_set_user_data(kywc_thumbnail *thumbnail, void *data);

void *kywc_thumbnail_get_user_data(kywc_thumbnail *thumbnail);

/**
 * destroy callback will be called in kywc_thumbnail_destroy
 */
void kywc_thumbnail_destroy(kywc_thumbnail *thumbnail);

#ifdef __cplusplus
}
#endif

#endif /* _LIBKYWC_HEADER_H_ */
