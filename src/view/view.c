#include <stdlib.h>

#include <kywc/log.h>

#include "server.h"
#include "view.h"

static struct view_manager *view_manager = NULL;

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&view_manager->server_destroy.link);

    free(view_manager);
    view_manager = NULL;
}

struct view_manager *view_manager_create(struct server *server)
{
    view_manager = calloc(1, sizeof(struct view_manager));
    if (!view_manager) {
        return NULL;
    }

    wl_list_init(&view_manager->workspaces);
    wl_signal_init(&view_manager->events.new_view);

    view_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &view_manager->server_destroy);

    return view_manager;
}

void view_init(struct view *view, const struct view_impl *impl, void *data)
{
    struct kywc_view *kywc_view = &view->base;

    view->impl = impl;
    view->data = data;
    wl_list_init(&view->children);

    wl_signal_init(&kywc_view->events.premap);
    wl_signal_init(&kywc_view->events.map);
    wl_signal_init(&kywc_view->events.commit);
    wl_signal_init(&kywc_view->events.unmap);
    wl_signal_init(&kywc_view->events.destroy);

    wl_signal_init(&kywc_view->events.title);
    wl_signal_init(&kywc_view->events.app_id);

    // TODO: add kywc_view_type and check it
    wl_signal_emit_mutable(&view_manager->events.new_view, kywc_view);
}

void view_destroy(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    kywc_log(KYWC_DEBUG, "kywc_view %p destroy", kywc_view);

    wl_signal_emit_mutable(&kywc_view->events.destroy, kywc_view);

    view->impl->destroy(view);
}

void view_set_title(struct view *view, const char *title)
{
    struct kywc_view *kywc_view = &view->base;

    kywc_view->title = title;
    kywc_log(KYWC_DEBUG, "kywc_view %p title: %s", kywc_view, title);

    wl_signal_emit(&kywc_view->events.title, kywc_view);
}

void view_set_app_id(struct view *view, const char *app_id)
{
    struct kywc_view *kywc_view = &view->base;

    kywc_view->app_id = app_id;
    kywc_log(KYWC_DEBUG, "kywc_view %p app_id: %s", kywc_view, app_id);

    wl_signal_emit(&kywc_view->events.app_id, kywc_view);
}

void view_set_parent(struct view *view, struct view *parent)
{
    struct view *old_parent = view->parent;
    if (old_parent == parent) {
        return;
    }

    if (old_parent) {
        wl_list_remove(&view->link);
    }
    if (parent) {
        wl_list_insert(&parent->children, &view->link);
    }
    view->parent = parent;

    kywc_log(KYWC_DEBUG, "view %p set parent to %p", view, parent);
}

void view_map(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    wl_signal_emit_mutable(&kywc_view->events.premap, kywc_view);

    // TODO: add

    kywc_view->mapped = true;

    wl_signal_emit_mutable(&kywc_view->events.map, kywc_view);
}

void view_unmap(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    wl_signal_emit_mutable(&kywc_view->events.unmap, kywc_view);
}

void view_commit(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    wl_signal_emit_mutable(&kywc_view->events.commit, kywc_view);
}

void kywc_view_add_new_listener(struct wl_listener *listener)
{
    wl_signal_add(&view_manager->events.new_view, listener);
}

static struct view *view_from_kywc_view(struct kywc_view *kywc_view)
{
    struct view *view = wl_container_of(kywc_view, view, base);
    return view;
}

void kywc_view_close(struct kywc_view *kywc_view)
{
    struct view *view = view_from_kywc_view(kywc_view);

    view->impl->close(view);
}
