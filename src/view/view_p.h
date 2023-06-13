#ifndef _VIEW_P_H_
#define _VIEW_P_H_

#include <kywc/log.h>

#include "server.h"
#include "view/view.h"

struct view_manager {
    struct server *server;
    struct wl_list views;

    struct {
        struct wl_signal new_view;
    } events;

    struct view_layer layers[LAYER_NUMBER];

    // TODO: views keyboard focused when multi-seat
    struct {
        /* only one activated view in all workspaces at once */
        struct view *view;
        struct wl_listener minimize;
        struct wl_listener destroy;
    } activated;

    struct wl_listener new_xdg_surface;
    struct wl_listener server_destroy;
};

/* for interactive move and resize */
struct seat;
void interactive_begin_move(struct view *view, struct seat *seat);
void interactive_begin_resize(struct view *view, uint32_t edges, struct seat *seat);

bool xdg_shell_init(struct view_manager *view_manager);

bool decoration_manager_create(struct view_manager *view_manager);
bool decoration_should_use_ssd(struct wlr_surface *surface);

void view_topmost_activate(struct workspace *workspace);

bool positioner_manager_create(struct view_manager *view_manager);

struct wlr_xdg_popup;
void xdg_popup_create(struct wlr_xdg_popup *wlr_xdg_popup, struct ky_scene_tree *shell);

#if HAVE_KDE_VIRTUAL_DESKTOP
bool kde_virtual_desktop_management_create(struct server *server);
#else
static __attribute__((unused)) inline bool
kde_virtual_desktop_management_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_WLR_FOREIGN_TOPLEVEL
bool wlr_foreign_toplevel_manager_create(struct server *server);
#else
static __attribute__((unused)) inline bool
wlr_foreign_toplevel_manager_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_WLR_LAYER_SHELL
bool wlr_layer_shell_manager_create(struct server *server);
#else
static __attribute__((unused)) inline bool wlr_layer_shell_manager_create(struct server *server)
{
    return false;
}
#endif

#endif /* _VIEW_P_H_ */
