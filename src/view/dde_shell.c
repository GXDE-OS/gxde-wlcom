/*
 * SPDX-FileCopyrightText: 2026 GXDE OS Contributors.
 *
 * SPDX-License-Identifier: GPL-1.0-or-later
 *
 * Implements the dde_shell server-side Wayland protocol,
 * enabling DTK/Deepin applications on DWayland to communicate
 * window properties (corner radius, titlebar preference, etc.) to the compositor.
 *
 * Reference protocol: deepin-wayland-protocols/src/protocols/dde-shell.xml
 * Reference implementation: dwayland/src/server/ddeshell_interface.cpp
 */

#define _POSIX_C_SOURCE 200809L
#include <kywc/log.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/util/log.h>

#include "dde-shell-protocol.h"
#include "scene/scene.h"
#include "scene/surface.h"
#include "theme.h"
#include "view_p.h"

struct dde_shell {
    struct wl_global *global;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct dde_shell_surface {
    struct wl_resource *resource;

    struct wlr_surface *wlr_surface;
    struct wl_listener surface_map;
    struct wl_listener surface_destroy;

    struct view *view;
    struct wl_listener view_map;
    struct wl_listener view_destroy;
    struct wl_listener view_decoration;

    float window_radius_x;
    float window_radius_y;

    bool no_titlebar;

    /* effectscene bit mask: Represents which window effects are disabled */
    uint32_t effect_scene;

    /* effecttype: Type of startup effect (Normal/Cursor/Top/Bottom) */
    uint32_t startup_effect;
};


static void dde_surface_apply_radius(struct dde_shell_surface *surf)
{
    if (!surf->wlr_surface) {
        return;
    }

    /* effectNoRadius: If client asks no window radius */
    bool no_radius = surf->effect_scene & DDE_SHELL_EFFECTSCENE_EFFECTNORADIUS;

    /* Else, do no-ops */
    if (surf->window_radius_x < 0 && !no_radius) {
        return;
    }

    /* buffer必须有效 */
    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(surf->wlr_surface);
    if (!buffer) {
        return;
    }

    int r = no_radius ? 0 : (int)(surf->window_radius_x + 0.5f);
    bool need_corner = true;
    bool need_top_corner = true;

    /* 对于常规窗口(view)，根据窗口状态决定是否应用圆角 */
    if (surf->view) {
        struct kywc_view *kywc_view = &surf->view->base;
        if (!kywc_view->mapped) {
            return;
        }
        need_corner = !kywc_view->maximized && !kywc_view->fullscreen && !kywc_view->tiled;
        need_top_corner = need_corner && !(kywc_view->ssd & KYWC_SSD_TITLE);
    }

    /* 翻译到Open Kylin Wlcom的圆角设置 */
    int radius[4] = { 0 };
    radius[KY_SCENE_ROUND_CORNER_RB] = need_corner ? r : 0;
    radius[KY_SCENE_ROUND_CORNER_RT] = need_top_corner ? r : 0;
    radius[KY_SCENE_ROUND_CORNER_LB] = need_corner ? r : 0;
    radius[KY_SCENE_ROUND_CORNER_LT] = need_top_corner ? r : 0;

    /* 设置圆角至WM */
    ky_scene_node_set_radius(&buffer->node, radius);

    /* 处理日志 */
    kywc_log(KYWC_DEBUG, "(DDE Shell) Apply Radius: Window radius %d applied to surface %p.",
             r, surf->wlr_surface);
    kywc_log(KYWC_DEBUG, "(DDE Shell) Apply Radius: Right bottom: %d", radius[0]);
    kywc_log(KYWC_DEBUG, "(DDE Shell) Apply Radius: Right top: %d", radius[1]);
    kywc_log(KYWC_DEBUG, "(DDE Shell) Apply Radius: Left bottom: %d", radius[2]);
    kywc_log(KYWC_DEBUG, "(DDE Shell) Apply Radius: Left top: %d", radius[3]);
}


/* 应用无标题栏属性，对齐deepin-chameleon的noTitleBar */
static void dde_surface_apply_no_titlebar(struct dde_shell_surface *surf)
{
    if (!surf->no_titlebar || !surf->view) {
        return;
    }

    struct kywc_view *kywc_view = &surf->view->base;
    if (!kywc_view->mapped) {
        return;
    }

    /* 仅在当前确有服务端标题栏时去除，保留 BORDER/RESIZE(及其阴影圆角) */
    if (kywc_view->ssd & KYWC_SSD_TITLE) {
        view_set_decoration(surf->view, kywc_view->ssd & ~KYWC_SSD_TITLE);
        kywc_log(KYWC_DEBUG,
                 "(DDE Shell) NoTitleBar: dropped server titlebar for surface %p, ssd now %d",
                 surf->wlr_surface, kywc_view->ssd);
    }
}

/* DDE apply window effect */
static void dde_surface_apply_window_effect(struct dde_shell_surface* surf) {
    if (!surf->view) {
        return;
    }

    struct kywc_view* kywc_view = &surf->view->base;
    if (!kywc_view->mapped) {
        return;
    }

    /* effectNoBorder: Remove CSD (and shadow) */
    if (surf->effect_scene & DDE_SHELL_EFFECTSCENE_EFFECTNOBORDER) {
        enum kywc_ssd ssd = kywc_view->ssd & ~(KYWC_SSD_BORDER | KYWC_SSD_RESIZE);
        if (ssd != kywc_view->ssd) {
            view_set_decoration(surf->view, ssd);
            kywc_log(KYWC_DEBUG,
                "(DDE Shell) WindowEffect: dropped border decoration for surface %p",
                surf->wlr_surface);
        }
    }
}


/* 回调函数，监控map，在窗口真正显示到屏幕上时，应用无标题栏与圆角设置
 * 先去标题栏再设圆角: 圆角逻辑依赖 ssd 中的 KYWC_SSD_TITLE 位决定是否圆顶角 */
static void dde_surface_handle_view_map(struct wl_listener *listener, void *data)
{
    struct dde_shell_surface *surf = wl_container_of(listener, surf, view_map);
    dde_surface_apply_no_titlebar(surf);
    dde_surface_apply_window_effect(surf);
    dde_surface_apply_radius(surf);
}

static void dde_surface_handle_view_decoration(struct wl_listener *listener, void *data)
{
    struct dde_shell_surface *surf = wl_container_of(listener, surf, view_decoration);
    dde_surface_apply_no_titlebar(surf);
    dde_surface_apply_window_effect(surf);
}

static void dde_surface_handle_view_destroy(struct wl_listener* listener,
        void* data) {
    struct dde_shell_surface* surf = wl_container_of(listener, surf,
        view_destroy);

    wl_list_remove(&surf->view_map.link);
    wl_list_init(&surf->view_map.link);
    wl_list_remove(&surf->view_destroy.link);
    wl_list_init(&surf->view_destroy.link);
    wl_list_remove(&surf->view_decoration.link);
    wl_list_init(&surf->view_decoration.link);
    surf->view = NULL;
}

static void dde_surface_handle_surface_destroy(struct wl_listener* listener,
        void* data) {
    struct dde_shell_surface* surf = wl_container_of(listener, surf,
        surface_destroy);

    wl_list_remove(&surf->surface_map.link);
    wl_list_init(&surf->surface_map.link);
    wl_list_remove(&surf->surface_destroy.link);
    wl_list_init(&surf->surface_destroy.link);
    surf->wlr_surface = NULL;
}


/* 负责在底层surface首次准备就绪时，查找对应的上层view，为其挂载圆角渲染和窗口销毁等事件的监听器 */
static void dde_surface_handle_surface_map(struct wl_listener *listener, void *data)
{
    /* 监控map */
    struct dde_shell_surface *surf = wl_container_of(listener, surf, surface_map);
    wl_list_remove(&surf->surface_map.link);
    wl_list_init(&surf->surface_map.link);

    /* 寻找对应的View */
    surf->view = view_try_from_wlr_surface(surf->wlr_surface);
    if (surf->view) {
        surf->view_map.notify = dde_surface_handle_view_map;
        wl_signal_add(&surf->view->base.events.map, &surf->view_map);
        surf->view_destroy.notify = dde_surface_handle_view_destroy;
        wl_signal_add(&surf->view->base.events.destroy, &surf->view_destroy);
        surf->view_decoration.notify = dde_surface_handle_view_decoration;
        wl_signal_add(&surf->view->base.events.decoration, &surf->view_decoration);
        dde_surface_handle_view_map(&surf->view_map, NULL);
    } else {
        /* layer-shell 等无 view 的 surface：直接应用圆角和无标题栏 */
        dde_surface_apply_no_titlebar(surf);
        dde_surface_apply_radius(surf);
    }
}


/* 协议的VTable方法们 */
/* 获取surface的几何属性 */
static void dde_shell_surface_get_geometry(struct wl_client *client,
                                           struct wl_resource *resource)
{
    struct dde_shell_surface *surf = wl_resource_get_user_data(resource);
    if (!surf->view || !surf->view->base.mapped) {
        return;
    }

    struct kywc_view *kywc_view = &surf->view->base;
    dde_shell_surface_send_geometry(resource, kywc_view->geometry.x,
                                    kywc_view->geometry.y,
                                    kywc_view->geometry.width,
                                    kywc_view->geometry.height);
}


/* 要求WM分屏 */
static void dde_shell_surface_request_split_window(struct wl_client *client,
                                                   struct wl_resource *resource,
                                                   uint32_t split_type)
{
    /* 先获取surface */
    struct dde_shell_surface *surf = wl_resource_get_user_data(resource);
    if (!surf->view) {
        return;
    }

    /* DDE_SHELL_SPLIT_TYPE_LEFT_SPLIT = 1, RIGHT_SPLIT = 2 */
    enum kywc_tile tile = KYWC_TILE_NONE;
    if (split_type & DDE_SHELL_SPLIT_TYPE_LEFT_SPLIT) {
        tile = KYWC_TILE_LEFT;
    } else if (split_type & DDE_SHELL_SPLIT_TYPE_RIGHT_SPLIT) {
        tile = KYWC_TILE_RIGHT;
    }

    if (tile != KYWC_TILE_NONE) {
        kywc_view_set_tiled(&surf->view->base, tile, surf->view->output);
    }
}


/* 请求激活窗体 */
static void dde_shell_surface_request_active(struct wl_client *client,
                                             struct wl_resource *resource)
{
    /* 先获取surface */
    struct dde_shell_surface *surf = wl_resource_get_user_data(resource);
    if (!surf->view) {
        return;
    }

    /* 激活View */
    struct kywc_view *kywc_view = &surf->view->base;
    kywc_view_activate(kywc_view);
    view_set_focus(surf->view, kywc_view->focused_seat);
}


/* 设置窗体状态 */
static void dde_shell_surface_set_state(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t flags, uint32_t state)
{
    /* 先获取surface */
    struct dde_shell_surface *surf = wl_resource_get_user_data(resource);
    if (!surf->view) {
        return;
    }

    struct kywc_view *kywc_view = &surf->view->base;

    /* 要求将窗口提到最前并获得焦点 */
    if (flags & DDE_SHELL_STATE_ACTIVE) {
        if (state & DDE_SHELL_STATE_ACTIVE) {
            kywc_view_activate(kywc_view);
            view_set_focus(surf->view, kywc_view->focused_seat);
        }
    }
    /* 最小化: 隐藏窗口到任务栏 */
    if (flags & DDE_SHELL_STATE_MINIMIZED) {
        kywc_view_set_minimized(kywc_view, state & DDE_SHELL_STATE_MINIMIZED);
    }
    /* 最大化: 将窗口扩展到整个可用工作区 */
    if (flags & DDE_SHELL_STATE_MAXIMIZED) {
        kywc_view_set_maximized(kywc_view, state & DDE_SHELL_STATE_MAXIMIZED,
                                surf->view->output);
    }
    /* 全屏: 占用整个屏幕，隐藏面板和dock */
    if (flags & DDE_SHELL_STATE_FULLSCREEN) {
        kywc_view_set_fullscreen(kywc_view, state & DDE_SHELL_STATE_FULLSCREEN,
                                 surf->view->output);
    }
}


/* 处理DDE Shell surface属性设置请求，如标题栏、圆角等可视属性 */
static void dde_shell_surface_set_property(struct wl_client *client,
                                           struct wl_resource *resource,
                                           uint32_t property, struct wl_array *data)
{
    /* 先获取surface */
    struct dde_shell_surface *surf = wl_resource_get_user_data(resource);
    if (!surf->wlr_surface) {
        return;
    }

    /* 无标题栏: 1=应用程序自己绘制标题栏(CSD) */
    if (property & DDE_SHELL_PROPERTY_NOTITLEBAR) {
        int *value = (int *)data->data;
        surf->no_titlebar = (*value != 0);
        dde_surface_apply_no_titlebar(surf);
    }

    /* 窗口圆角: 数据段为两个float，X和Y方向圆角半径 */
    if (property & DDE_SHELL_PROPERTY_WINDOWRADIUS) {
        float *value = (float *)data->data;
        surf->window_radius_x = value[0];
        surf->window_radius_y = value[1];
        dde_surface_apply_radius(surf);
    }
}


/**
 * Request custom window effect.
 * Note that effectscene is a bit mask of "which effect to DISABLE"
 * ... and 0 represents all enabled.
 */
static void dde_shell_surface_request_window_effect(struct wl_client* client,
        struct wl_resource* resource, uint32_t effectscene) {
    struct dde_shell_surface* surf = wl_resource_get_user_data(resource);
    surf->effect_scene = effectscene;

    dde_surface_apply_radius(surf);
    dde_surface_apply_window_effect(surf);

    kywc_log(KYWC_DEBUG, "(DDE Shell) WindowEffect: effectscene 0x%x set on surface %p",
        effectscene, (void *)surf->wlr_surface);
}

/**
 * Request startup effect for the window.
 * Note that effecttype is a single value representing the type of startup effect.
 * We have effectNormal/Cursor/Top/Bottom.
 */
static void dde_shell_surface_request_window_startup_effect(
        struct wl_client* client, struct wl_resource* resource,
        uint32_t effecttype) {
    struct dde_shell_surface* surf = wl_resource_get_user_data(resource);
    surf->startup_effect = effecttype;

    kywc_log(KYWC_DEBUG,
        "(DDE Shell) StartupEffect: effecttype 0x%x recorded for surface %p",
        effecttype, (void *)surf->wlr_surface);
}


/* DDE Shell surface接口实现，挂到wl_resource上供wayland协议分发 */
static const struct dde_shell_surface_interface dde_shell_surface_impl = {
    .get_geometry = dde_shell_surface_get_geometry,
    .request_active = dde_shell_surface_request_active,
    .set_state = dde_shell_surface_set_state,
    .set_property = dde_shell_surface_set_property,
    .request_split_window = dde_shell_surface_request_split_window,
    .request_window_effect = dde_shell_surface_request_window_effect,
    .request_window_startup_effect = dde_shell_surface_request_window_startup_effect,
};


/* Wayland资源销毁回调: 客户端断开连接/主动销毁时触发，清理所有监听器和内存 */
static void dde_shell_surface_resource_destroy(struct wl_resource *resource)
{
    /* 获取对应的DDE Shell Surface */
    struct dde_shell_surface *surf = wl_resource_get_user_data(resource);

    /* 摘掉surface和view上的所有事件监听 */
    wl_list_remove(&surf->surface_map.link);
    wl_list_remove(&surf->surface_destroy.link);
    if (surf->view) {
        wl_list_remove(&surf->view_map.link);
        wl_list_remove(&surf->view_destroy.link);
        wl_list_remove(&surf->view_decoration.link);
    }
    free(surf);
}


/* 客户端调dde_shell.get_shell_surface时，创建一个绑定到wl_surface的壳 */
static void dde_shell_get_shell_surface(struct wl_client *client, struct wl_resource *resource,
                                        uint32_t id, struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "(DDE Shell) Creation: Invalid surface");
        return;
    }

    /* 分配壳对象 */
    struct dde_shell_surface *surf = calloc(1, sizeof(*surf));
    if (!surf) {
        wl_client_post_no_memory(client);
        return;
    }

    /* 初始化默认值 */
    surf->window_radius_x = -1.0f;
    surf->window_radius_y = -1.0f;
    surf->wlr_surface = wlr_surface;

    /* 创建对应的Wayland资源，挂载VTable实现 */
    surf->resource = wl_resource_create(client, &dde_shell_surface_interface,
                                        wl_resource_get_version(resource), id);
    if (!surf->resource) {
        free(surf);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(surf->resource, &dde_shell_surface_impl, surf,
                                   dde_shell_surface_resource_destroy);

    /* 初始化事件监听链表 */
    wl_list_init(&surf->surface_map.link);
    wl_list_init(&surf->surface_destroy.link);
    wl_list_init(&surf->view_map.link);
    wl_list_init(&surf->view_destroy.link);
    wl_list_init(&surf->view_decoration.link);

    /* 挂载surface map监听 */
    surf->surface_map.notify = dde_surface_handle_surface_map;
    wl_signal_add(&wlr_surface->events.map, &surf->surface_map);

    surf->surface_destroy.notify = dde_surface_handle_surface_destroy;
    wl_signal_add(&wlr_surface->events.destroy, &surf->surface_destroy);

    /* 如果此时surface已经mapped，直接触发map逻辑 */
    if (wlr_surface->mapped) {
        dde_surface_handle_surface_map(&surf->surface_map, NULL);
    }

    kywc_log(KYWC_DEBUG, "(DDE Shell) Creation: dde_shell_surface created for surface %p",
             (void *)wlr_surface);
}


/* dde_shell全局接口实现 */
static const struct dde_shell_interface dde_shell_impl = {
    .get_shell_surface = dde_shell_get_shell_surface,
};


/* 客户端绑定dde_shell全局对象时，分配resource并挂载实现 */
static void dde_shell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct dde_shell *shell = data;
    struct wl_resource *resource = wl_resource_create(client, &dde_shell_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &dde_shell_impl, shell, NULL);
}


/* 当Wayland display销毁时，先于display销毁wl_global */
static void dde_shell_handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct dde_shell *shell = wl_container_of(listener, shell, display_destroy);
    wl_list_remove(&shell->display_destroy.link);
    wl_global_destroy(shell->global);
}


/* 服务器关闭时清理dde_shell对象本身 */
static void dde_shell_handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct dde_shell *shell = wl_container_of(listener, shell, server_destroy);
    wl_list_remove(&shell->server_destroy.link);
    free(shell);
}


/* 注册dde_shell全局协议对象，一启动就无条件注册，这样DTK Wayland程序就能发送窗体属性了 */
bool dde_shell_create(struct server *server)
{
    struct dde_shell *shell = calloc(1, sizeof(*shell));
    if (!shell) {
        return false;
    }

    /* 注册到Wayland display，客户端可见 */
    shell->global = wl_global_create(server->display, &dde_shell_interface, 2, shell,
        dde_shell_bind);

    if (!shell->global) {
        free(shell);
        kywc_log(KYWC_ERROR, "(DDE Shell) Creation: Failed to create dde_shell global!");
        return false;
    }

    /* display销毁前先销毁global */
    shell->display_destroy.notify = dde_shell_handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &shell->display_destroy);

    /* 服务器关闭时释放自身 */
    shell->server_destroy.notify = dde_shell_handle_server_destroy;
    server_add_destroy_listener(server, &shell->server_destroy);

    kywc_log(KYWC_INFO, "(DDE Shell) Creation: Protocol global registered (version 2)");
    return true;
}
