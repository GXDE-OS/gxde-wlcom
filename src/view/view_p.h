// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

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
        struct wl_signal new_mapped_view;
        struct wl_signal window_menu;
        struct wl_signal show_desktop;
    } events;

    struct view_layer layers[LAYER_NUMBER];

    // TODO: views keyboard focused when multi-seat
    struct {
        /* only one activated view in all workspaces at once */
        struct view *view;
        struct wl_listener minimize;
        struct wl_listener unmap;
    } activated;

    struct config *config;

    struct {
        uint32_t num_workspaces;
        uint32_t view_adsorption;
    } state;

    struct wl_listener theme_update;
    struct wl_listener theme_icon_update;
    struct wl_listener new_xdg_surface;
    struct wl_listener server_terminate;
    struct wl_listener server_destroy;

    bool show_desktop_enabled;
    bool show_activte_only_enabled;
};

struct view_show_window_menu_event {
    struct view *view;
    struct seat *seat;
    int x, y;
};

bool view_manager_config_init(struct view_manager *view_manager);

bool view_read_config(struct view_manager *view_manager);

void view_write_config(struct view_manager *view_manager);

void view_show_window_menu(struct view *view, struct seat *seat, int x, int y);

void view_close_popups(struct view *view);

bool xdg_shell_init(struct view_manager *view_manager);

bool decoration_manager_create(struct view_manager *view_manager);

void view_topmost_activate(struct workspace *workspace);

void view_proxy_destroy(struct view_proxy *view_proxy);

void view_set_current_proxy(struct view *view, struct view_proxy *view_proxy);

struct view_proxy *view_proxy_by_workspace(struct view *view, struct workspace *workspace);

bool positioner_manager_create(struct view_manager *view_manager);

void positioner_add_new_view(struct view *view);

bool server_decoration_manager_create(struct view_manager *view_manager);

bool window_actions_create(struct view_manager *view_manager);

bool view_manager_actions_create(struct view_manager *view_manager);

bool window_menu_manager_create(struct view_manager *view_manager);

bool maximize_switcher_create(struct view_manager *view_manager);

void modal_create(struct view *view);

struct wlr_xdg_popup;
void xdg_popup_create(struct wlr_xdg_popup *wlr_xdg_popup, struct ky_scene_tree *shell,
                      struct view_layer *layer, bool use_usable_area);

bool ky_workspace_manager_create(struct server *server);

bool ky_toplevel_manager_create(struct server *server);

bool xdg_dialog_create(struct server *server);

bool xdg_activation_create(struct server *server);

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

#if HAVE_KDE_PLASMA_SHELL
bool kde_plasma_shell_create(struct server *server);
#else
static __attribute__((unused)) inline bool kde_plasma_shell_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_KDE_PLASMA_WINDOW_MANAGEMENT
bool kde_plasma_window_management_create(struct server *server);
#else
static __attribute__((unused)) inline bool
kde_plasma_window_management_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_KDE_BLUR
bool kde_blur_manager_create(struct server *server);
#else
static __attribute__((unused)) inline bool kde_blur_manager_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_KDE_SLIDE
bool kde_slide_manager_create(struct server *server);
#else
static __attribute__((unused)) inline bool kde_slide_manager_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_UKUI_SHELL
bool ukui_shell_create(struct server *server);
#else
static __attribute__((unused)) inline bool ukui_shell_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_UKUI_WINDOW_MANAGEMENT
bool ukui_window_management_create(struct server *server);
#else
static __attribute__((unused)) inline bool ukui_window_management_create(struct server *server)
{
    return false;
}
#endif

#if HAVE_UKUI_BLUR
bool ukui_blur_manager_create(struct server *server);
#else
static __attribute__((unused)) inline bool ukui_blur_manager_create(struct server *server)
{
    return false;
}
#endif

#endif /* _VIEW_P_H_ */
