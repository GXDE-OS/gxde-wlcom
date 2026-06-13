/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -----------------------------------------------------------------------------
 * treeland_dde_shell_v1协议的部分实现，让程序可以轻松从Wlcom拿到自身全局坐标
 * 实际实现与Treeland略有差异，做出的修改部分是为GXDE的Wayland适配需求定制的
 */

#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>

#include "input/cursor.h"
#include "input/input.h"
#include "input/seat.h"
#include "treeland-dde-shell-v1-protocol.h"
#include "view/view.h"
#include "view_p.h"

/**
 * @file treeland_dde_shell.c
 * @brief treeland_dde_shell_v1 协议的精简实现
 *
 * 很遗憾，出于「安全」顾虑Wayland不向客户端暴露窗口的全局坐标,
 * 客户端也无法设置自身的绝对位置，那么GXDE搭载的一些纯X11菜单daemon有福了，
 * 目标是把子菜单摆到「触发它的哪个窗口/父菜单旁边」，这些位置只有WM知道...
 * 使用此私有协议，客户端提交相对偏移，合成器转换到全局坐标
 *
 * 本协议实现与Treeland原版有所差异： @c set_surface_position 的 @c (x,y)
 * 在此被解释为「相对某基准左上角的偏移」, 而非绝对全局坐标(见 @c surface_set_surface_position)
 * 真正的落点计算在 @c wlr_layer_shell.c 布局layer surface时通过
 * @c treeland_dde_shell_get_placement() 查询本模块缓存的位置
 */

#define TREELAND_DDE_SHELL_MANAGER_VERSION 1

struct treeland_dde_shell_manager {
    struct wl_global *global;
    struct wl_list surfaces;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct treeland_dde_shell_surface_state {
    struct wl_list link;      /**< manager->surfaces */
    struct wl_list resources; /**< 所有wl_resource */

    struct wl_client *client;        /**< 对应的客户端，用于识别「同一客户端父菜单」 */
    struct wlr_surface *wlr_surface; /**< 关联的surface */
    struct wl_listener surface_destroy;

    bool auto_place;      /**< true即「按全局光标定位」 */
    int32_t y_offset;     /**< auto_place时光标y方向的附加偏移 */
    bool has_pos;         /**< true即pos_x/pos_y为有效的固定全局坐标 */
    int32_t pos_x, pos_y; /**< 已解析的全局落点 (锚定左上角) */
};

static struct treeland_dde_shell_manager *manager = NULL;

/**
 * @brief 按 @c wlr_surface 在 @c manager->surfaces 中查找定位状态
 *
 * @param surface (wlr_surface*) 目标surface
 * @return (treeland_dde_shell_surface_state*) 命中时返回对应state，否则则返回 @c NULL.
 */
static struct treeland_dde_shell_surface_state *state_from_wlr_surface(struct wlr_surface *surface)
{
    struct treeland_dde_shell_surface_state *state;
    wl_list_for_each(state, &manager->surfaces, link) {
        if (state->wlr_surface == surface) {
            return state;
        }
    }
    return NULL;
}

/**
 * @brief 查询surface的定位请求
 *
 * @param[in]  surface    (wlr_surface*) 目标surface
 * @param[out] auto_place (bool*)        true时要求按全局光标定位
 * @param[out] y_offset   (int*)         @c auto_place 时光标y的附加偏移
 * @param[out] has_pos    (bool*)        true时为「 @c pos 为有效的固定全局坐标」
 * @param[out] px         (int*)         固定全局坐标x
 * @param[out] py         (int*)         固定全局坐标y
 * @return (bool) surface已注册则返回true，否则false
 */
bool treeland_dde_shell_get_placement(struct wlr_surface *surface, bool *auto_place, int *y_offset,
                                      bool *has_pos, int *px, int *py)
{
    if (!manager || !surface) {
        return false;
    }

    struct treeland_dde_shell_surface_state *state = state_from_wlr_surface(surface);
    if (!state) {
        return false;
    }

    *auto_place = state->auto_place;
    *y_offset = state->y_offset;
    *has_pos = state->has_pos;
    *px = state->pos_x;
    *py = state->pos_y;
    return true;
}

/**
 * @brief 获取surface校正后的实际放置坐标
 *
 * @warning layer-shell在排版控件的时候会把请求的坐标校准进屏幕可见区域，防止菜单等部件越界
 *          如果不会写子菜单会按照父菜单「请求值」定位，而父菜单的坐标其实是被校正过的，
 *          子菜单没有，表象即为菜单的错位，需要等确定最终落点坐标后调用本函数，
 *          使得父子菜单位置对齐
 * @note 参数中的坐标均为全局坐标，以左上角为锚点
 * @param surface (wlr_surface*) 目标surface
 * @param x       (int)          实际落点x
 * @param y       (int)          实际落点y
 * @return (void)
 */
void treeland_dde_shell_set_resolved_position(struct wlr_surface *surface, int x, int y)
{
    if (!manager || !surface) {
        return;
    }

    struct treeland_dde_shell_surface_state *state = state_from_wlr_surface(surface);
    if (state) {
        state->pos_x = x;
        state->pos_y = y;
        state->has_pos = true;
    }
}

/**
 * @brief 销毁定位状态
 *
 * @param state (treeland_dde_shell_surface_state*) 待销毁的状态
 * @return (void)
 */
static void dde_shell_state_destroy(struct treeland_dde_shell_surface_state *state)
{
    struct wl_resource *resource;
    struct wl_resource *tmp;

    wl_resource_for_each_safe(resource, tmp, &state->resources) {
        wl_resource_set_user_data(resource, NULL);
        wl_list_remove(wl_resource_get_link(resource));
        wl_list_init(wl_resource_get_link(resource));
    }

    wl_list_remove(&state->link);
    wl_list_remove(&state->surface_destroy.link);
    free(state);
}

/**
 * @brief surface销毁信号callback
 *
 * @param listener (wl_listener*) State内的listener
 * @param data     (void*)        这里不重要，因为没用到
 * @return (void)
 */
static void dde_shell_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct treeland_dde_shell_surface_state *state =
        wl_container_of(listener, state, surface_destroy);
    dde_shell_state_destroy(state);
}

/**
 * @brief 销毁协议资源请求
 *
 * @note state由资源析构按需回收
 * @param client   (wl_client*)   目标客户端
 * @param resource (wl_resource*) 目标资源
 * @return (void)
 */
static void surface_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

/**
 * @brief 找到子菜单对应的父菜单
 *
 * @details 已知父菜单早于子菜单弹出，可安全假定「同一client、已提交定位 (has_pos为真)、
 *          已mapped且不是这个子surface本身的surface」即为父菜单
 * @note 存在多级子菜单时，链表中最后一个匹配者代表「最近弹出的一级」，排除掉子菜单自身，
 *       其上一级即为父菜单
 * @note 如果返回 @c NULL，有一种情况为传入的菜单就是父菜单
 * @param self   (treeland_dde_shell_surface_state*) 子菜单自身state
 * @param client (wl_client*)                        目标客户端
 * @return (treeland_dde_shell_surface_state*) 命中父菜单的状态，找不到即返回 @c NULL
 */
static struct treeland_dde_shell_surface_state *
find_parent_menu(struct treeland_dde_shell_surface_state *self, struct wl_client *client)
{
    struct treeland_dde_shell_surface_state *s, *parent = NULL;
    wl_list_for_each(s, &manager->surfaces, link) {
        if (s == self || s->client != client || !s->has_pos) {
            continue;
        }
        if (s->wlr_surface && s->wlr_surface->mapped) {
            parent = s;
        }
    }
    return parent;
}

/**
 * @brief 提交菜单定位请求
 *
 * @note 把 @c (x,y) 解释为「相对某基准左上角的偏移」而非Treeland原义的绝对
 *       全局坐标), 因为Wayland客户端不能获知自身/触发窗口的全局位置
 *       基准的选取:
 *           - 父菜单: 基准为父菜单, 落点 => 父菜单全局位置 + 偏移;
 *           - 否则: 落点 = 该窗口
 *
 * @param client   发起请求的客户端。
 * @param resource surface_v1 资源(其 user_data 为对应 state)。
 * @param x 相对基准左上角的横向偏移。
 * @param y 相对基准左上角的纵向偏移。
 */
static void surface_set_surface_position(struct wl_client *client, struct wl_resource *resource,
                                         int32_t x, int32_t y)
{
    struct treeland_dde_shell_surface_state *state = wl_resource_get_user_data(resource);
    if (!state) {
        return;
    }

    int gx = x, gy = y;
    struct treeland_dde_shell_surface_state *parent = find_parent_menu(state, client);
    if (parent) {
        // 子菜单: 基准是父菜单
        gx = parent->pos_x + x;
        gy = parent->pos_y + y;
    } else {
        // 顶层菜单: 基准是触发它的窗口
        struct seat *seat = input_manager_get_default_seat();
        if (seat && seat->wlr_seat) {
            struct wlr_surface *focus = seat->wlr_seat->keyboard_state.focused_surface;
            if (focus) {
                struct view *v = view_try_from_wlr_surface(focus);
                if (v) {
                    // GTK应用的(x,y)相对其wl_surface(含CSD隐形阴影边距)左上角;
                    // surface原点 = geometry - padding(CSD边距), 用它相加避免重复计阴影。
                    gx = v->base.geometry.x - v->base.padding.left + x;
                    gy = v->base.geometry.y - v->base.padding.top + y;
                }
            }
        }
    }
    state->has_pos = true;
    state->pos_x = gx;
    state->pos_y = gy;
    state->auto_place = false;
    kywc_log(KYWC_WARN,
             "(Treeland Shim) SetPos: "
             "dde set_pos surface %p in=%d,%d parent=%p -> %d,%d",
             state->wlr_surface, x, y, (void *)parent, gx, gy);
    wlr_layer_shell_reconfigure_surface(state->wlr_surface);
}

/* surface_v1.set_role暂时不需要，空实现 */
static void surface_set_role(struct wl_client *client, struct wl_resource *resource, uint32_t role)
{
    return;
}

/**
 * @brief 按全局光标定位，适用于右键上下文菜单等
 *
 * @param client   (wl_client*)   目标客户端
 * @param resource (wl_resource*) 目标资源
 * @param y_offset (uint32_t)     相对光标的y轴偏移
 */
static void surface_set_auto_placement(struct wl_client *client, struct wl_resource *resource,
                                       uint32_t y_offset)
{
    struct treeland_dde_shell_surface_state *state = wl_resource_get_user_data(resource);
    if (!state) {
        return;
    }

    struct seat *seat = input_manager_get_default_seat();
    if (seat && seat->cursor) {
        state->pos_x = (int)seat->cursor->lx;
        state->pos_y = (int)seat->cursor->ly + (int32_t)y_offset;
        state->has_pos = true;
        state->auto_place = false;
    } else {
        // 拿不到光标时
        state->auto_place = true;
        state->y_offset = (int32_t)y_offset;
        state->has_pos = false;
    }
}

/**
 * @brief 取单个uint32参数的surface请求的共用空实现
 * @note 覆盖 @c set_skip_switcher / @c set_skip_dock_preview /
 * @c set_skip_muti_task_view / @c set_accept_keyboard_focus
 * 上述行为wlcom暂不需要。
 */
static void surface_noop_u32(struct wl_client *client, struct wl_resource *resource, uint32_t value)
{
    return;
}

static const struct treeland_dde_shell_surface_v1_interface surface_impl = {
    .destroy = surface_handle_destroy,
    .set_surface_position = surface_set_surface_position,
    .set_role = surface_set_role,
    .set_auto_placement = surface_set_auto_placement,
    .set_skip_switcher = surface_noop_u32,
    .set_skip_dock_preview = surface_noop_u32,
    .set_skip_muti_task_view = surface_noop_u32,
    .set_accept_keyboard_focus = surface_noop_u32,
};

/**
 * @brief 资源析构
 *
 * @note 资源的state如果在资源销毁后没用了会被一同销毁
 * @param resource (wl_resource*) 待销毁的资源
 * @return (void)
 */
static void surface_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
    struct treeland_dde_shell_surface_state *state = wl_resource_get_user_data(resource);
    if (state && wl_list_empty(&state->resources)) {
        dde_shell_state_destroy(state);
    }
}

/** ===========================================================================
 *                         treeland_dde_shell_manager_v1
 *  ===========================================================================
 */

/**
 * @brief @c get_shell_surface请求: 为指定surface取得 @c surface_v1资源
 *
 * @note 同一surface复用同一份state
 * @param client           (wl_client*)   发起请求的客户端
 * @param manager_resource (wl_resource*) manager_v1资源
 * @param id               (uint32_t)     新建资源的id
 * @param surface_resource (wl_resource*) 目标资源
 * @return (void)
 */
static void manager_get_shell_surface(struct wl_client *client,
                                      struct wl_resource *manager_resource, uint32_t id,
                                      struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        kywc_log(KYWC_ERROR, "(Treeland Shim) Get surface: Failed to get surface from resource,"
                             "aborting...");
        return;
    }

    struct treeland_dde_shell_surface_state *state = state_from_wlr_surface(wlr_surface);
    if (!state) {
        state = calloc(1, sizeof(*state));
        if (!state) {
            wl_client_post_no_memory(client);
            return;
        }
        wl_list_init(&state->resources);
        state->client = client;
        state->wlr_surface = wlr_surface;
        state->surface_destroy.notify = dde_shell_handle_surface_destroy;
        wl_signal_add(&wlr_surface->events.destroy, &state->surface_destroy);
        wl_list_insert(&manager->surfaces, &state->link);
    }

    int version = wl_resource_get_version(manager_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &treeland_dde_shell_surface_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &surface_impl, state, surface_handle_resource_destroy);
    wl_list_insert(&state->resources, wl_resource_get_link(resource));
}

/**
 * @brief 销毁Manager的资源
 *
 * @param client   (wl_client*)   client实例
 * @param resource (wl_resource*) 待销毁的资源
 * @return (void)
 */
static void manager_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

/* 以下请求当前未支持，所以是空实现，将来如果有程序需要这些函数应该单独提Merge request */
static void manager_noop_id(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    return;
}

/* 以下请求当前未支持，所以是空实现，将来如果有程序需要这些函数应该单独提Merge request */
static void manager_noop_id_obj(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                                struct wl_resource *obj)
{
    return;
}

/* 以下请求当前未支持，所以是空实现，将来如果有程序需要这些函数应该单独提Merge request */
static void manager_noop_xwindow(struct wl_client *client, struct wl_resource *resource,
                                 uint32_t callback, uint32_t wid, struct wl_resource *anchor,
                                 wl_fixed_t dx, wl_fixed_t dy)
{
    return;
}

static const struct treeland_dde_shell_manager_v1_interface manager_impl = {
    .get_window_overlap_checker = manager_noop_id,
    .get_shell_surface = manager_get_shell_surface,
    .get_treeland_dde_active = manager_noop_id_obj,
    .get_treeland_multitaskview = manager_noop_id,
    .get_treeland_window_picker = manager_noop_id,
    .get_treeland_lockscreen = manager_noop_id,
    .set_xwindow_position_relative = manager_noop_xwindow,
    .destroy = manager_handle_destroy,
};

/**
 * @brief 客户端绑定 @c manager_v1 全局时的callback，会创建对应资源
 *
 * @param client  (wl_client*)  绑定的客户端
 * @param data    (void*)       全局user_data，此处即manager
 * @param version (unit32_t)    请求的协议版本
 * @param id      (uint32_t)    资源的id
 * @return (void)
 */
static void treeland_dde_shell_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                            uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &treeland_dde_shell_manager_v1_interface, version, id);

    if (!resource) {
        kywc_log(KYWC_ERROR, "(Treeland Shim) Bind: Fail to bind, calling "
                             "WL_CLIENT_POST_NO_MEMORY...");
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &manager_impl, manager, NULL);
}

/**
 * @brief wl_display销毁callback
 */
static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

/**
 * @brief server销毁callback
 * @return (void)
 */
static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
    manager = NULL;
}

/**
 * @brief 创建 @c treeland_dde_shell_manager_v1 全局，处理生命周期
 *
 * 分配单例、注册协议全局、初始化 surface 链表、初始化listeners
 *
 * @param server (server*) server实例
 * @return 成功则返回true；失败返回 false
 */
bool treeland_dde_shell_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct treeland_dde_shell_manager));

    if (!manager) {
        return false;
    }

    manager->global = wl_global_create(server->display, &treeland_dde_shell_manager_v1_interface,
                                       TREELAND_DDE_SHELL_MANAGER_VERSION, manager,
                                       treeland_dde_shell_manager_bind);

    if (!manager->global) {
        kywc_log(KYWC_ERROR, "(Treeland Shim) Init: Failed to create treeland dde-shell "
                             "manager!!");

        free(manager);

        if (manager) {
            manager = NULL;
        }

        return false;
    }

    wl_list_init(&manager->surfaces);
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}
