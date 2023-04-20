#ifndef _ADAPTER_WLROOTS_P_H_
#define _ADAPTER_WLROOTS_P_H_

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif

#include <kywc/log.h>

#include "wlroots.h"

struct wlroots_server {
    struct server *server;

    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_backend *backend;
    struct wlr_session *session;
    struct wlr_compositor *compositor;

#if HAVE_XWAYLAND
    struct wlr_xwayland *xwayland;
    struct wl_listener xwayland_ready;
#endif
    struct wlr_scene *scene;
    struct wlr_output_layout *layout;

    struct wl_listener new_output;
    struct wl_listener new_input;
    struct wl_listener new_virtual_pointer;
    struct wl_listener new_virtual_keyboard;
};

struct wlroots_server *wlroots_server_from_server(struct server *server);

bool wlroots_input_init(struct wlroots_server *wlroots);

bool wlroots_output_init(struct wlroots_server *wlroots);

bool wlroots_server_init_seat(struct server *server, struct seat *seat);

#endif /* _ADAPTER_WLROOTS_P_H_ */
