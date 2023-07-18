#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>

#include "input_p.h"
#include "server.h"
#include "view/view.h"

struct idle_inhibit_manager {
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit;
    struct wl_list idle_inhibitors;

    struct wl_listener new_idle_inhibitor;
    struct wl_listener destroy;
};

struct idle_inhibitor {
    struct wl_list link;
    struct idle_inhibit_manager *manager;
    struct wlr_idle_inhibitor_v1 *wlr_idle_inhibitor;
    struct wl_listener inhibitor_destroy;

    struct wlr_surface *surface;
    struct wl_listener surface_map;
    struct wl_listener surface_unmap;

    struct view *view;
    struct wl_listener view_map;
    struct wl_listener view_minimize;

    bool visible;
};

static void idle_inhibitor_set_inhibit(struct idle_inhibitor *idle_inhibitor)
{
    if (idle_inhibitor->visible) {
        idle_manager_set_inhibited(true);
        return;
    }

    struct idle_inhibit_manager *manager = idle_inhibitor->manager;
    struct idle_inhibitor *inhibitor;
    wl_list_for_each(inhibitor, &manager->idle_inhibitors, link) {
        if (inhibitor->visible) {
            idle_manager_set_inhibited(true);
            return;
        }
    }

    idle_manager_set_inhibited(false);
}

static void handle_view_map(struct wl_listener *listener, void *data)
{
    struct idle_inhibitor *idle_inhibitor = wl_container_of(listener, idle_inhibitor, view_map);
    idle_inhibitor->visible = !idle_inhibitor->view->base.minimized;
    idle_inhibitor_set_inhibit(idle_inhibitor);
}

static void handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct idle_inhibitor *idle_inhibitor =
        wl_container_of(listener, idle_inhibitor, view_minimize);
    idle_inhibitor->visible = !idle_inhibitor->view->base.minimized;
    idle_inhibitor_set_inhibit(idle_inhibitor);
}

static void handle_surface_map(struct wl_listener *listener, void *data)
{
    struct idle_inhibitor *idle_inhibitor = wl_container_of(listener, idle_inhibitor, surface_map);
    struct view *view = view_try_from_wlr_surface(idle_inhibitor->surface);
    if (!view) {
        wl_list_init(&idle_inhibitor->view_map.link);
        wl_list_init(&idle_inhibitor->view_minimize.link);
        idle_inhibitor->visible = true;
        idle_inhibitor_set_inhibit(idle_inhibitor);
        return;
    }

    idle_inhibitor->view = view;
    if (view->base.mapped) {
        handle_view_map(&idle_inhibitor->view_map, NULL);
        wl_list_init(&idle_inhibitor->view_map.link);
    } else {
        idle_inhibitor->view_map.notify = handle_view_map;
        wl_signal_add(&view->base.events.map, &idle_inhibitor->view_map);
    }

    idle_inhibitor->view_minimize.notify = handle_view_minimize;
    wl_signal_add(&view->base.events.minimize, &idle_inhibitor->view_minimize);
}

static void handle_surface_unmap(struct wl_listener *listener, void *data)
{
    struct idle_inhibitor *idle_inhibitor =
        wl_container_of(listener, idle_inhibitor, surface_unmap);
    idle_inhibitor->visible = false;
    idle_inhibitor_set_inhibit(idle_inhibitor);
}

static void handle_inhibitor_destroy(struct wl_listener *listener, void *data)
{
    struct idle_inhibitor *idle_inhibitor =
        wl_container_of(listener, idle_inhibitor, inhibitor_destroy);

    wl_list_remove(&idle_inhibitor->link);
    wl_list_remove(&idle_inhibitor->surface_map.link);
    wl_list_remove(&idle_inhibitor->surface_unmap.link);
    wl_list_remove(&idle_inhibitor->view_map.link);
    wl_list_remove(&idle_inhibitor->view_minimize.link);

    if (idle_inhibitor->visible) {
        idle_inhibitor->visible = false;
        idle_inhibitor_set_inhibit(idle_inhibitor);
    }

    free(idle_inhibitor);
}

static void handle_new_idle_inhibitor(struct wl_listener *listener, void *data)
{
    struct idle_inhibit_manager *manager = wl_container_of(listener, manager, new_idle_inhibitor);
    struct wlr_idle_inhibitor_v1 *wlr_idle_inhibitor = data;

    struct idle_inhibitor *idle_inhibitor = calloc(1, sizeof(struct idle_inhibitor));
    if (!idle_inhibitor) {
        return;
    }

    idle_inhibitor->manager = manager;
    wl_list_insert(&manager->idle_inhibitors, &idle_inhibitor->link);

    idle_inhibitor->wlr_idle_inhibitor = wlr_idle_inhibitor;
    idle_inhibitor->inhibitor_destroy.notify = handle_inhibitor_destroy;
    wl_signal_add(&wlr_idle_inhibitor->events.destroy, &idle_inhibitor->inhibitor_destroy);

    wl_list_init(&idle_inhibitor->view_map.link);
    wl_list_init(&idle_inhibitor->view_minimize.link);

    idle_inhibitor->surface = wlr_idle_inhibitor->surface;
    if (idle_inhibitor->surface->mapped) {
        handle_surface_map(&idle_inhibitor->surface_map, NULL);
        wl_list_init(&idle_inhibitor->surface_map.link);
    } else {
        idle_inhibitor->surface_map.notify = handle_surface_map;
        wl_signal_add(&idle_inhibitor->surface->events.map, &idle_inhibitor->surface_map);
    }
    idle_inhibitor->surface_unmap.notify = handle_surface_unmap;
    wl_signal_add(&idle_inhibitor->surface->events.unmap, &idle_inhibitor->surface_unmap);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    struct idle_inhibit_manager *manager = wl_container_of(listener, manager, destroy);
    wl_list_remove(&manager->destroy.link);
    free(manager);
}

bool idle_inhibit_manager_create(struct server *server)
{
    struct idle_inhibit_manager *manager = calloc(1, sizeof(struct idle_inhibit_manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->idle_inhibitors);

    manager->idle_inhibit = wlr_idle_inhibit_v1_create(server->display);
    manager->new_idle_inhibitor.notify = handle_new_idle_inhibitor;
    wl_signal_add(&manager->idle_inhibit->events.new_inhibitor, &manager->new_idle_inhibitor);

    manager->destroy.notify = handle_destroy;
    wl_signal_add(&manager->idle_inhibit->events.destroy, &manager->destroy);

    return true;
}
