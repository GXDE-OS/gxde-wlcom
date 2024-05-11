// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>

#include "input/seat.h"
#include "output.h"
#include "view_p.h"

struct wlr_foreign_manager {
    struct wlr_foreign_toplevel_manager_v1 *manager;
    struct wl_listener new_mapped_view;
    struct wl_listener destroy;
};

struct wlr_foreign {
    struct wlr_foreign_manager *manager;
    struct wlr_foreign_toplevel_handle_v1 *toplevel_handle;
    struct kywc_view *toplevel_view;

    struct wl_listener view_unmap;

    struct wl_listener view_maximize;
    struct wl_listener view_minimize;
    struct wl_listener view_activate;
    struct wl_listener view_fullscreen;
    struct wl_listener view_title;
    struct wl_listener view_app_id;

    struct wl_listener request_maximize;
    struct wl_listener request_minimize;
    struct wl_listener request_activate;
    struct wl_listener request_fullscreen;
    struct wl_listener request_close;
    struct wl_listener set_rectangle;
    struct wl_listener destroy;
};

static void handle_request_maximize(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, request_maximize);
    struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;

    kywc_view_set_maximized(foreign->toplevel_view, event->maximized, NULL);
}

static void handle_request_minimize(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, request_minimize);
    struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;

    kywc_view_set_minimized(foreign->toplevel_view, event->minimized);
}

static void handle_request_activate(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, request_activate);
    struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
    struct kywc_view *view = foreign->toplevel_view;

    kywc_view_activate(view);
    seat_focus_surface(seat_from_wlr_seat(event->seat), view_from_kywc_view(view)->surface);
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, request_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
    struct kywc_view *view = foreign->toplevel_view;

    struct kywc_output *kywc_output = NULL;
    if (event->output) {
        struct output *output = output_from_wlr_output(event->output);
        if (output && !output->base.destroying && output->base.state.enabled) {
            kywc_output = &output->base;
        }
    }

    kywc_view_set_fullscreen(view, event->fullscreen, kywc_output);
}

static void handle_request_close(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, request_close);
    struct kywc_view *view = foreign->toplevel_view;

    kywc_view_close(view);
}

static void handle_set_rectangle(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, request_close);
    struct wlr_foreign_toplevel_handle_v1_set_rectangle_event *event = data;

    kywc_log(KYWC_DEBUG, "set surface %p rectangle (%d, %d), %d x %d", event->surface, event->x,
             event->y, event->width, event->height);
}

static void handle_foreign_destroy(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, destroy);

    wl_list_remove(&foreign->view_maximize.link);
    wl_list_remove(&foreign->view_minimize.link);
    wl_list_remove(&foreign->view_activate.link);
    wl_list_remove(&foreign->view_fullscreen.link);
    wl_list_remove(&foreign->view_title.link);
    wl_list_remove(&foreign->view_app_id.link);

    wl_list_remove(&foreign->request_maximize.link);
    wl_list_remove(&foreign->request_minimize.link);
    wl_list_remove(&foreign->request_activate.link);
    wl_list_remove(&foreign->request_fullscreen.link);
    wl_list_remove(&foreign->request_close.link);
    wl_list_remove(&foreign->set_rectangle.link);
    wl_list_remove(&foreign->destroy.link);
}

static void handle_view_maximize(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, view_maximize);
    struct wlr_foreign_toplevel_handle_v1 *toplevel = foreign->toplevel_handle;
    struct kywc_view *view = foreign->toplevel_view;

    wlr_foreign_toplevel_handle_v1_set_maximized(toplevel, view->maximized);
}

static void handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, view_minimize);
    struct wlr_foreign_toplevel_handle_v1 *toplevel = foreign->toplevel_handle;
    struct kywc_view *view = foreign->toplevel_view;

    wlr_foreign_toplevel_handle_v1_set_minimized(toplevel, view->minimized);
}

static void handle_view_activate(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, view_activate);
    struct wlr_foreign_toplevel_handle_v1 *toplevel = foreign->toplevel_handle;
    struct kywc_view *view = foreign->toplevel_view;

    wlr_foreign_toplevel_handle_v1_set_activated(toplevel, view->activated);
}

static void handle_view_fullscreen(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, view_fullscreen);
    struct wlr_foreign_toplevel_handle_v1 *toplevel = foreign->toplevel_handle;
    struct kywc_view *view = foreign->toplevel_view;

    wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel, view->fullscreen);
}

static void handle_view_title(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, view_title);
    struct wlr_foreign_toplevel_handle_v1 *toplevel = foreign->toplevel_handle;
    struct kywc_view *view = foreign->toplevel_view;

    if (view->title) {
        wlr_foreign_toplevel_handle_v1_set_title(toplevel, view->title);
    }
}

static void handle_view_app_id(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, view_app_id);
    struct wlr_foreign_toplevel_handle_v1 *toplevel = foreign->toplevel_handle;
    struct kywc_view *view = foreign->toplevel_view;

    if (view->app_id) {
        wlr_foreign_toplevel_handle_v1_set_app_id(toplevel, view->app_id);
    }
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = wl_container_of(listener, foreign, view_unmap);

    if (foreign->toplevel_handle) {
        wlr_foreign_toplevel_handle_v1_destroy(foreign->toplevel_handle);
        foreign->toplevel_handle = NULL;
    }

    wl_list_remove(&foreign->view_unmap.link);
    free(foreign);
}

static bool foreign_toplevel_create(struct wlr_foreign *foreign)
{
    foreign->toplevel_handle = wlr_foreign_toplevel_handle_v1_create(foreign->manager->manager);
    if (!foreign->toplevel_handle) {
        return false;
    }

    foreign->request_maximize.notify = handle_request_maximize;
    foreign->request_minimize.notify = handle_request_minimize;
    foreign->request_activate.notify = handle_request_activate;
    foreign->request_fullscreen.notify = handle_request_fullscreen;
    foreign->request_close.notify = handle_request_close;
    foreign->set_rectangle.notify = handle_set_rectangle;
    foreign->destroy.notify = handle_foreign_destroy;
    wl_signal_add(&foreign->toplevel_handle->events.request_maximize, &foreign->request_maximize);
    wl_signal_add(&foreign->toplevel_handle->events.request_minimize, &foreign->request_minimize);
    wl_signal_add(&foreign->toplevel_handle->events.request_activate, &foreign->request_activate);
    wl_signal_add(&foreign->toplevel_handle->events.request_fullscreen,
                  &foreign->request_fullscreen);
    wl_signal_add(&foreign->toplevel_handle->events.request_close, &foreign->request_close);
    wl_signal_add(&foreign->toplevel_handle->events.set_rectangle, &foreign->set_rectangle);
    wl_signal_add(&foreign->toplevel_handle->events.destroy, &foreign->destroy);

    struct kywc_view *view = foreign->toplevel_view;
    foreign->view_maximize.notify = handle_view_maximize;
    foreign->view_minimize.notify = handle_view_minimize;
    foreign->view_activate.notify = handle_view_activate;
    foreign->view_fullscreen.notify = handle_view_fullscreen;
    foreign->view_title.notify = handle_view_title;
    foreign->view_app_id.notify = handle_view_app_id;
    wl_signal_add(&view->events.maximize, &foreign->view_maximize);
    wl_signal_add(&view->events.minimize, &foreign->view_minimize);
    wl_signal_add(&view->events.activate, &foreign->view_activate);
    wl_signal_add(&view->events.fullscreen, &foreign->view_fullscreen);
    wl_signal_add(&view->events.title, &foreign->view_title);
    wl_signal_add(&view->events.app_id, &foreign->view_app_id);

    return true;
}

static void handle_new_mapped_view(struct wl_listener *listener, void *data)
{
    struct wlr_foreign *foreign = calloc(1, sizeof(struct wlr_foreign));
    if (!foreign) {
        return;
    }

    struct wlr_foreign_manager *manager = wl_container_of(listener, manager, new_mapped_view);
    foreign->manager = manager;

    struct kywc_view *view = data;
    foreign->toplevel_view = view;
    foreign->view_unmap.notify = handle_view_unmap;
    wl_signal_add(&view->events.unmap, &foreign->view_unmap);

    if (!foreign_toplevel_create(foreign)) {
        return;
    }

    struct wlr_foreign_toplevel_handle_v1 *toplevel = foreign->toplevel_handle;
    /* set toplevel state */
    // struct wlr_output *output = view_most_output(view);
    // wlr_foreign_toplevel_handle_v1_output_enter(toplevel, output);
    // TODO: add scene buffer output event
    // wlr_foreign_toplevel_handle_v1_output_enter(toplevel, output);
    // wlr_foreign_toplevel_handle_v1_output_leave(toplevel, output);
    // TODO: foreign_toplevel_handle_from_view
    // wlr_foreign_toplevel_handle_v1_set_parent(toplevel);
    if (view->title) {
        wlr_foreign_toplevel_handle_v1_set_title(toplevel, view->title);
    }
    if (view->app_id) {
        wlr_foreign_toplevel_handle_v1_set_app_id(toplevel, view->app_id);
    }
    wlr_foreign_toplevel_handle_v1_set_maximized(toplevel, view->maximized);
    wlr_foreign_toplevel_handle_v1_set_minimized(toplevel, view->minimized);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel, view->fullscreen);
    wlr_foreign_toplevel_handle_v1_set_activated(toplevel, view->activated);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    struct wlr_foreign_manager *manager = wl_container_of(listener, manager, destroy);

    wl_list_remove(&manager->destroy.link);
    wl_list_remove(&manager->new_mapped_view.link);
    free(manager);
}

bool wlr_foreign_toplevel_manager_create(struct server *server)
{
    struct wlr_foreign_manager *manager = calloc(1, sizeof(struct wlr_foreign_manager));
    if (!manager) {
        return false;
    }

    manager->manager = wlr_foreign_toplevel_manager_v1_create(server->display);
    if (!manager->manager) {
        free(manager);
        return false;
    }

    /* monitor new mapped view */
    manager->new_mapped_view.notify = handle_new_mapped_view;
    kywc_view_add_new_mapped_listener(&manager->new_mapped_view);

    manager->destroy.notify = handle_destroy;
    wl_signal_add(&manager->manager->events.destroy, &manager->destroy);

    return true;
}
