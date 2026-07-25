// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _VIEW_P_H_
#define _VIEW_P_H_

#include <kywc/log.h>

#include "server.h"
#include "view/view.h"

struct view_mode_interface {
    const char *name;

    void (*view_map)(struct view *view);
    void (*view_unmap)(struct view *view);

    void (*view_request_move)(struct view *view, int x, int y);
    void (*view_request_resize)(struct view *view, struct kywc_box *geometry);
    void (*view_request_minimized)(struct view *view, bool minimized);
    void (*view_request_maximized)(struct view *view, bool maximized,
                                   struct kywc_output *kywc_output);
    void (*view_request_fullscreen)(struct view *view, bool fullscreen,
                                    struct kywc_output *kywc_output);
    void (*view_request_tiled)(struct view *view, enum kywc_tile tile,
                               struct kywc_output *kywc_output);
    void (*view_request_activate)(struct view *view);

    void (*view_request_show_menu)(struct view *view, struct seat *seat, int x, int y);

    void (*view_request_show_split_screen_switcher)(struct view *view, struct seat *seat,
                                                    bool enabled);

    void (*view_click)(struct seat *seat, struct view *view, uint32_t button, bool pressed,
                       enum click_state state);
    void (*view_hover)(struct seat *seat, struct view *view);

    void (*view_mode_enter)(void);
    void (*view_mode_leave)(void);

    void (*mode_destroy)(void);
};

enum minimize_effect_type {
    MINIMIZE_EFFECT_TYPE_SCALE,
    MINIMIZE_EFFECT_TYPE_MAGIC_LAMP,
    MINIMIZE_EFFECT_TYPE_NUM,
};

struct view_mode {
    struct wl_list link;
    const struct view_mode_interface *impl;
};

struct view_manager {
    struct server *server;
    struct wl_list views;

    struct view *global_authentication_view;
    struct view *desktop;

    struct {
        struct wl_signal new_view;
        struct wl_signal new_mapped_view;
        struct wl_signal show_desktop;
        struct wl_signal activate_view;
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
        bool csd_round_corner;
        enum minimize_effect_type minimize_effect_type;
    } state;

    struct wl_listener theme_update;
    struct wl_listener theme_icon_update;
    struct wl_listener new_xdg_surface;
    struct wl_listener server_terminate;
    struct wl_listener server_ready;
    struct wl_listener server_destroy;

    bool show_desktop_enabled;
    bool show_activte_only_enabled;
    bool switcher_shown;

    struct wl_list view_modes; // struct view_mode.link
    struct view_mode *mode;
};

bool view_manager_config_init(struct view_manager *view_manager);

void view_manager_set_switcher_shown(bool shown);

bool view_read_config(struct view_manager *view_manager);

void view_write_config(struct view_manager *view_manager);

void view_show_window_menu(struct view *view, struct seat *seat, int x, int y);

void view_close_popups(struct view *view);

void view_update_round_corner(struct view *view);

bool xdg_shell_init(struct view_manager *view_manager);

bool decoration_manager_create(struct view_manager *view_manager);

void view_proxy_destroy(struct view_proxy *view_proxy);

void view_set_current_proxy(struct view *view, struct view_proxy *view_proxy);

struct view_proxy *view_proxy_by_workspace(struct view *view, struct workspace *workspace);

bool positioner_manager_create(struct view_manager *view_manager);

void positioner_add_new_view(struct view *view);

bool server_decoration_manager_create(struct view_manager *view_manager);

bool window_actions_create(struct view_manager *view_manager);

bool view_manager_actions_create(struct view_manager *view_manager);

bool window_menu_manager_create(struct view_manager *view_manager);

void window_menu_show(struct view *view, struct seat *seat, int x, int y);

bool maximize_switcher_create(struct view_manager *view_manager);

bool window_switcher_create(struct view_manager *view_manager);

bool multitask_view_create(struct view_manager *view_manager);

bool multitask_view_toggle(void);

bool multitask_launcher_interface_create(void);

void modal_create(struct view *view);

void global_authentication_create(struct view *view);

void view_manager_set_global_authentication(struct view *view);

struct view *view_manager_get_global_authentication(void);

void view_begin_tile_preview(struct view *view, struct seat *seat, struct output *output,
                             uint32_t tile);

bool split_screen_switcher_manager_create(struct view_manager *view_manager);

void split_screen_switcher_show(struct view *view, struct seat *seat, bool enabled);

bool view_show_split_screen_switcher(struct view *view, struct seat *seat, bool enabled);

struct wlr_xdg_popup;
void xdg_popup_create(struct wlr_xdg_popup *wlr_xdg_popup, struct ky_scene_tree *shell,
                      struct view_layer *layer, bool use_usable_area);

bool ky_workspace_manager_create(struct server *server);

bool ky_toplevel_manager_create(struct server *server);

bool xdg_dialog_create(struct server *server);

bool xdg_activation_create(struct server *server);

struct view_mode *view_manager_mode_from_name(const char *name);

struct view_mode *view_manager_mode_register(const struct view_mode_interface *impl);

void view_manager_mode_unregister(struct view_mode *mode);

void stack_mode_register(struct view_manager *view_manager);

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

struct wlr_surface;
#if HAVE_WLR_LAYER_SHELL
bool wlr_layer_shell_manager_create(struct server *server);
void wlr_layer_shell_reconfigure_surface(struct wlr_surface *surface);
#else
static __attribute__((unused)) inline bool wlr_layer_shell_manager_create(struct server *server)
{
    return false;
}
static __attribute__((unused)) inline void
wlr_layer_shell_reconfigure_surface(struct wlr_surface *surface)
{
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

#if HAVE_KDE_APPMENU
bool kde_appmenu_manager_create(struct server *server);
#else

static __attribute__((unused)) inline bool kde_appmenu_manager_create(struct server* server) {
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

struct wlr_surface;
bool treeland_dde_shell_manager_create(struct server *server);
bool treeland_dde_shell_get_placement(struct wlr_surface *surface, bool *auto_place, int *y_offset,
                                      bool *has_pos, int *px, int *py);
void treeland_dde_shell_set_resolved_position(struct wlr_surface *surface, int x, int y);

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

/* dde-shell相关，用于DTK窗体属性 */
bool dde_shell_create(struct server *server);

#endif /* _VIEW_P_H_ */
