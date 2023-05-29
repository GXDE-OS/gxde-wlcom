#include <stdlib.h>

#include <kywc/log.h>

#include "server.h"
#include "view/workspace.h"
#include "view_p.h"

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

    view_manager->server = server;
    wl_signal_init(&view_manager->events.new_view);

    view_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &view_manager->server_destroy);

    /* create all layers */
    for (int layer = LAYER_FIRST; layer < LAYER_NUMBER; layer++) {
        view_manager->layers[layer].layer = layer;
        view_manager->layers[layer].tree = ky_scene_create_tree(server->scene);
    }

    workspace_manager_create(view_manager);
    decoration_init(view_manager);
    xdg_shell_init(view_manager);

    return view_manager;
}

struct view_layer *view_manager_get_layer(enum layer layer)
{
    switch (layer) {
    case LAYER_BELOW:
        return &workspace_manager_get_current()->layers[0];
    case LAYER_NORMAL:
        return &workspace_manager_get_current()->layers[1];
    case LAYER_ABOVE:
        return &workspace_manager_get_current()->layers[2];
    default:
        return &view_manager->layers[layer];
    }
}

void view_init(struct view *view, const struct view_impl *impl, void *data)
{
    struct kywc_view *kywc_view = &view->base;

    view->impl = impl;
    view->data = data;
    wl_list_init(&view->subview.children);

    kywc_view->type = impl->type;
    wl_signal_init(&kywc_view->events.premap);
    wl_signal_init(&kywc_view->events.map);
    wl_signal_init(&kywc_view->events.commit);
    wl_signal_init(&kywc_view->events.unmap);
    wl_signal_init(&kywc_view->events.destroy);
    wl_signal_init(&kywc_view->events.activate);
    wl_signal_init(&kywc_view->events.maximize);
    wl_signal_init(&kywc_view->events.minimize);
    wl_signal_init(&kywc_view->events.fullscreen);
    wl_signal_init(&kywc_view->events.title);
    wl_signal_init(&kywc_view->events.app_id);
    wl_signal_init(&kywc_view->events.decoration);
    wl_signal_init(&kywc_view->events.shadow);

    wl_signal_init(&view->events.workspace);

    /* only emit new_view signal when client shells */
    if (kywc_view->type == KYWC_VIEW_XDG_SHELL || kywc_view->type == KYWC_VIEW_XWAYLAND) {
        view_set_workspace(view, workspace_manager_get_current());
        wl_signal_emit_mutable(&view_manager->events.new_view, kywc_view);
    }
}

void view_destroy(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;
    kywc_log(KYWC_DEBUG, "kywc_view %p destroy", kywc_view);

    wl_signal_emit_mutable(&kywc_view->events.destroy, kywc_view);

    ky_scene_node_destroy(ky_scene_node_from_tree(view->tree));
    view->impl->destroy(view);
}

void view_set_title(struct view *view, const char *title)
{
    struct kywc_view *kywc_view = &view->base;

    kywc_view->title = title;
    kywc_log(KYWC_DEBUG, "kywc_view %p title: %s", kywc_view, title);

    wl_signal_emit_mutable(&kywc_view->events.title, kywc_view);
}

void view_set_app_id(struct view *view, const char *app_id)
{
    struct kywc_view *kywc_view = &view->base;

    kywc_view->app_id = app_id;
    kywc_log(KYWC_DEBUG, "kywc_view %p app_id: %s", kywc_view, app_id);

    wl_signal_emit_mutable(&kywc_view->events.app_id, kywc_view);
}

void view_set_decoration(struct view *view, bool need_ssd)
{
    struct kywc_view *kywc_view = &view->base;
    if (kywc_view->need_ssd == need_ssd) {
        return;
    }

    kywc_view->need_ssd = need_ssd;
    kywc_log(KYWC_DEBUG, "kywc_view %p need ssd %d", kywc_view, need_ssd);

    wl_signal_emit_mutable(&kywc_view->events.decoration, kywc_view);
}

void view_set_workspace(struct view *view, struct workspace *workspace)
{

    if (view->workspace == workspace) {
        return;
    }

    view->workspace = workspace;
    kywc_log(KYWC_DEBUG, "kywc_view %p worskpace: %s", &view->base, workspace->name);

    wl_signal_emit_mutable(&view->events.workspace, workspace);
}

void view_set_parent(struct view *view, struct view *parent)
{
    struct view *old_parent = view->subview.parent;
    if (old_parent == parent) {
        return;
    }

    if (old_parent) {
        wl_list_remove(&view->subview.link);
    }
    if (parent) {
        wl_list_insert(&parent->subview.children, &view->subview.link);
    }
    view->subview.parent = parent;

    kywc_log(KYWC_DEBUG, "view %p set parent to %p", view, parent);
}

void view_map(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    // TODO: set output and workspace

    wl_signal_emit_mutable(&kywc_view->events.premap, kywc_view);

    ky_scene_node_set_enabled(ky_scene_node_from_tree(view->tree), true);

    kywc_view->mapped = true;

    wl_signal_emit_mutable(&kywc_view->events.map, kywc_view);
}

void view_unmap(struct view *view)
{
    struct kywc_view *kywc_view = &view->base;

    kywc_view->title = kywc_view->app_id = NULL;
    ky_scene_node_set_enabled(ky_scene_node_from_tree(view->tree), false);

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

void kywc_view_set_output(struct kywc_view *kywc_view, struct kywc_output *output)
{
    struct view *view = view_from_kywc_view(kywc_view);
    if (view->output == output) {
        return;
    }

    view->output = output;
}

void kywc_view_move(struct kywc_view *kywc_view, int x, int y) {}

void kywc_view_resize(struct kywc_view *kywc_view, int x, int y, int w, int h) {}

void kywc_view_activate(struct kywc_view *kywc_view) {}

void kywc_view_set_tiled(struct kywc_view *kywc_view, enum kywc_edges edges) {}

void kywc_view_set_enabled(struct kywc_view *kywc_view, bool enabled) {}

void kywc_view_toggle_enabled(struct kywc_view *kywc_view) {}

void kywc_view_set_minimized(struct kywc_view *kywc_view, bool minimized) {}

void kywc_view_toggle_minimized(struct kywc_view *kywc_view) {}

void kywc_view_set_maximized(struct kywc_view *kywc_view, bool maximized) {}

void kywc_view_toggle_maximized(struct kywc_view *kywc_view) {}

void kywc_view_set_fullscreen(struct kywc_view *kywc_view, bool fullscreen)
{
    if (!kywc_view->fullscreenable) {
        return;
    }
}

void kywc_view_toggle_fullscreen(struct kywc_view *kywc_view)
{
    kywc_view_set_fullscreen(kywc_view, !kywc_view->fullscreen);
}
