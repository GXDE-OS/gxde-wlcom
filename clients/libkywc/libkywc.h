// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

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

enum kywc_context_capability {
    KYWC_CONTEXT_CAPABILITY_OUTPUT = 1 << 0,
    KYWC_CONTEXT_CAPABILITY_TOPLEVEL = 1 << 1,
    KYWC_CONTEXT_CAPABILITY_WORKSPACE = 1 << 2,
};

struct kywc_context_interface {
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
    struct kywc_output_mode *mode;
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
    KYWC_OUTPUT_STATE_TRANSFROM = 1 << 3,
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

kywc_output *kywc_context_find_output(kywc_context *ctx, const char *uuid);

void kywc_output_set_user_data(kywc_output *output, void *data);

void *kywc_output_get_user_data(kywc_output *output);

/**
 * toplevel or window
 */
struct _kywc_toplevel {
    const char *uuid;
    const char *title, *app_id;
    const char *icon;
    uint32_t capabilities;
    kywc_toplevel *parent;
    const char *primary_output;
    /* state */
    bool activated, minimized, maximized, fullscreen;
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

void kywc_toplevel_set_user_data(kywc_toplevel *toplevel, void *data);

void *kywc_toplevel_get_user_data(kywc_toplevel *toplevel);

#ifdef __cplusplus
}
#endif

#endif /* _LIBKYWC_HEADER_H_ */
