// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "input/seat.h"
#include "output.h"
#include "view/workspace.h"
#include "view_p.h"

struct xdg_activation_token {
    struct wlr_xdg_activation_token_v1 *token;
    struct wl_listener token_destroy;

    struct seat *seat;
    struct wl_listener seat_destroy;

    struct kywc_output *output;
    struct wl_listener output_destroy;

    struct workspace *workspace;
    struct wl_listener workspace_destroy;

    struct view *view;
    struct wl_listener view_premap;
    struct wl_listener view_destroy;
};

struct xdg_activation_manager {
    struct wlr_xdg_activation_v1 *xdg_activation;

    struct wl_listener new_token;
    struct wl_listener request_activate;
    struct wl_listener destroy;
};

static void xdg_activation_token_destroy(struct xdg_activation_token *token)
{
    if (token->view) {
        return;
    }

    wl_list_remove(&token->token_destroy.link);
    wl_list_remove(&token->seat_destroy.link);
    wl_list_remove(&token->output_destroy.link);
    wl_list_remove(&token->workspace_destroy.link);
    wl_list_remove(&token->view_premap.link);
    wl_list_remove(&token->view_destroy.link);
    free(token);
}

static void token_handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_activation_token *token = wl_container_of(listener, token, seat_destroy);
    token->seat = NULL;
    wl_list_remove(&token->seat_destroy.link);
    wl_list_init(&token->seat_destroy.link);
}

static void token_handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_activation_token *token = wl_container_of(listener, token, output_destroy);
    token->output = NULL;
    wl_list_remove(&token->output_destroy.link);
    wl_list_init(&token->output_destroy.link);
}

static void token_handle_workspace_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_activation_token *token = wl_container_of(listener, token, workspace_destroy);
    token->workspace = NULL;
    wl_list_remove(&token->workspace_destroy.link);
    wl_list_init(&token->workspace_destroy.link);
}

static void token_handle_token_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_activation_token *token = wl_container_of(listener, token, token_destroy);
    token->token = NULL;
    xdg_activation_token_destroy(token);
}

static void token_handle_view_premap(struct wl_listener *listener, void *data)
{
    struct xdg_activation_token *token = wl_container_of(listener, token, view_premap);
    struct view *view = token->view;

    /* set view output */
    if (token->output && view->output != token->output) {
        view->output = token->output;
        wl_list_remove(&view->output_destroy.link);
        wl_signal_add(&view->output->events.destroy, &view->output_destroy);
    }

    if (token->workspace) {
        view_set_workspace(view, token->workspace);
    }

    view->base.focused_seat = token->seat ? token->seat : input_manager_get_default_seat();

    token->view = NULL;
    xdg_activation_token_destroy(token);
}

static void token_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_activation_token *token = wl_container_of(listener, token, view_destroy);
    token->view = NULL;
    xdg_activation_token_destroy(token);
}

static void handle_request_activate(struct wl_listener *listener, void *data)
{
    const struct wlr_xdg_activation_v1_request_activate_event *event = data;

    struct wlr_xdg_surface *wlr_xdg_surface = wlr_xdg_surface_try_from_wlr_surface(event->surface);
    struct xdg_activation_token *token = event->token->data;
    if (!wlr_xdg_surface || !token) {
        return;
    }

    struct view *view = view_try_from_wlr_surface(event->surface);
    if (!view) {
        return;
    }

    if (event->surface->mapped) {
        /* activation request */
        kywc_view_activate(&view->base);
        view_set_focus(view, token->seat ? token->seat : input_manager_get_default_seat());
        return;
    }

    /* startup app notification. now token destroy by view_premap or view_destroy*/
    wl_list_remove(&token->token_destroy.link);
    wl_list_init(&token->token_destroy.link);

    token->view = view;
    token->view_premap.notify = token_handle_view_premap;
    wl_signal_add(&token->view->base.events.premap, &token->view_premap);
    token->view_destroy.notify = token_handle_view_destroy;
    wl_signal_add(&token->view->base.events.destroy, &token->view_destroy);
}

static void handle_new_token(struct wl_listener *listener, void *data)
{
    struct xdg_activation_token *token = calloc(1, sizeof(struct xdg_activation_token));
    if (!token) {
        return;
    }

    struct wlr_xdg_activation_token_v1 *token_v1 = data;
    token_v1->data = token;
    token->token = token_v1;
    token->token_destroy.notify = token_handle_token_destroy;
    wl_signal_add(&token_v1->events.destroy, &token->token_destroy);

    struct seat *seat =
        token_v1->seat ? seat_from_wlr_seat(token_v1->seat) : input_manager_get_default_seat();
    token->seat = seat;
    token->seat_destroy.notify = token_handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &token->seat_destroy);

    token->output = input_current_output(seat) ? &input_current_output(seat)->base : NULL;
    token->output_destroy.notify = token_handle_output_destroy;
    wl_signal_add(&token->output->events.destroy, &token->output_destroy);

    token->workspace = workspace_manager_get_current();
    if (token_v1->surface) {
        /* workspace from surface */
        struct view *view = view_try_from_wlr_surface(token_v1->surface);
        if (view) {
            token->workspace = view->current_proxy->workspace;
        }
    }
    token->workspace_destroy.notify = token_handle_workspace_destroy;
    wl_signal_add(&token->workspace->events.destroy, &token->workspace_destroy);

    wl_list_init(&token->view_premap.link);
    wl_list_init(&token->view_destroy.link);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_activation_manager *manager = wl_container_of(listener, manager, destroy);
    wl_list_remove(&manager->destroy.link);
    wl_list_remove(&manager->request_activate.link);
    wl_list_remove(&manager->new_token.link);
    free(manager);
}

bool xdg_activation_create(struct server *server)
{
    struct xdg_activation_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->xdg_activation = wlr_xdg_activation_v1_create(server->display);
    if (!manager->xdg_activation) {
        free(manager);
        return false;
    }

    manager->request_activate.notify = handle_request_activate;
    wl_signal_add(&manager->xdg_activation->events.request_activate, &manager->request_activate);
    manager->new_token.notify = handle_new_token;
    wl_signal_add(&manager->xdg_activation->events.new_token, &manager->new_token);
    manager->destroy.notify = handle_destroy;
    wl_signal_add(&manager->xdg_activation->events.destroy, &manager->destroy);

    return true;
}
