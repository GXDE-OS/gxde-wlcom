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
    void (*new_output)(kywc_context *context, kywc_output *output);
    void (*new_toplevel)(kywc_context *context, kywc_toplevel *toplevel);
    void (*new_workspace)(kywc_context *context, kywc_workspace *workspace);
};

/**
 * Create a kywc context with the wayland display name.
 */
kywc_context *kywc_context_create(const char *name, uint32_t capabilities,
                                  const struct kywc_context_interface *impl);
/**
 * Create a kywc context with the exist wayland display.
 */
kywc_context *kywc_context_create_by_display(struct wl_display *display, uint32_t capabilities,
                                             const struct kywc_context_interface *impl);
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

void kywc_context_set_user_data(kywc_context *ctx, void *data);

void *kywc_context_get_user_data(kywc_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _LIBKYWC_HEADER_H_ */
