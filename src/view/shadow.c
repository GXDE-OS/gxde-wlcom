// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include "painter.h"
#include "scene/scene.h"
#include "theme.h"
#include "view_p.h"

enum SHADOW_PART {
    SHADOW_PART_TOP_LEFT = 0,
    SHADOW_PART_TOP,
    SHADOW_PART_TOP_RIGHT,
    SHADOW_PART_RIGHT,
    SHADOW_PART_BOTTOM_RIGHT,
    SHADOW_PART_BOTTOM,
    SHADOW_PART_BOTTOM_LEFT,
    SHADOW_PART_LEFT,
    SHADOW_PART_COUNT,
};

enum SHADOW_PART_MASK {
    SHADOW_MASK_TOP_LEFT = 1 << 0,
    SHADOW_MASK_TOP = 1 << 1,
    SHADOW_MASK_TOP_RIGHT = 1 << 2,
    SHADOW_MASK_RIGHT = 1 << 3,
    SHADOW_MASK_BOTTOM_RIGHT = 1 << 4,
    SHADOW_MASK_BOTTOM = 1 << 5,
    SHADOW_MASK_BOTTOM_LEFT = 1 << 6,
    SHADOW_MASK_LEFT = 1 << 7,
    SHADOW_MASK_ALL = (1 << 8) - 1,
};

enum shadow_update_cause {
    SHADOW_UPDATE_CAUSE_SIZE = 1 << 0,
    SHADOW_UPDATE_CAUSE_MAXIMIZE = 1 << 1,
    SHADOW_UPDATE_CAUSE_TILE = 1 << 2,
    SHADOW_UPDATE_CAUSE_FULLSCREEN = 1 << 3,
    SHADOW_UPDATE_CAUSE_CREATE = 1 << 4,
    SHADOW_UPDATE_CAUSE_ALL = (1 << 5) - 1,
};

struct shadow_manager {
    /* enable or disable all shadows */
    struct wl_list shadows;

    struct wl_listener new_view;
    struct wl_listener server_destroy;
};

struct shadow_part {
    struct ky_scene_node *node;
};

struct shadow {
    struct wl_list link;

    struct kywc_view *kywc_view;
    struct wl_listener view_map;
    struct wl_listener view_unmap;
    struct wl_listener view_destroy;
    struct wl_listener view_shadow;
    struct wl_listener view_size;
    struct wl_listener view_tile;
    struct wl_listener view_maximize;
    struct wl_listener view_fullscreen;
    struct wl_listener view_decoration;

    struct wl_listener theme_update;

    struct shadow_part parts[SHADOW_PART_COUNT];

    struct ky_scene_tree *tree;
    struct ky_scene_node *node;

    bool created;
    /* view size to reduce redraw */
    int view_width, view_height;
};

static struct ky_scene_node *shadow_part_create(struct ky_scene_tree *parent, enum SHADOW_PART part)
{
    struct theme *theme = theme_manager_get_current();
    struct wlr_buffer *shadow = theme->shadow;

    struct ky_scene_buffer *scene_buffer = ky_scene_buffer_create(parent, shadow);
    if (!scene_buffer) {
        return NULL;
    }

    int width, height;
    painter_buffer_unscaled_size(shadow, &width, &height);
    double w = width, h = height;
    struct wlr_fbox src_box;

    switch (part) {
    case SHADOW_PART_TOP_LEFT:
        src_box.x = 0;
        src_box.y = 0;
        src_box.width = w / 2;
        src_box.height = h / 2;
        break;
    case SHADOW_PART_TOP:
        src_box.x = w / 2;
        src_box.y = 0;
        src_box.width = 1;
        src_box.height = h / 2;
        break;
    case SHADOW_PART_TOP_RIGHT:
        src_box.x = w / 2;
        src_box.y = 0;
        src_box.width = w / 2;
        src_box.height = h / 2;
        break;
    case SHADOW_PART_RIGHT:
        src_box.x = w / 2;
        src_box.y = h / 2;
        src_box.width = w / 2;
        src_box.height = 1;
        break;
    case SHADOW_PART_BOTTOM_RIGHT:
        src_box.x = w / 2;
        src_box.y = h / 2;
        src_box.width = w / 2;
        src_box.height = h / 2;
        break;
    case SHADOW_PART_BOTTOM:
        src_box.x = w / 2;
        src_box.y = h / 2;
        src_box.width = 1;
        src_box.height = h / 2;
        break;
    case SHADOW_PART_BOTTOM_LEFT:
        src_box.x = 0;
        src_box.y = h / 2;
        src_box.width = w / 2;
        src_box.height = h / 2;
        break;
    case SHADOW_PART_LEFT:
        src_box.x = 0;
        src_box.y = h / 2;
        src_box.width = w / 2;
        src_box.height = 1;
        break;
    case SHADOW_PART_COUNT:
        break;
    }

    ky_scene_buffer_set_source_box(scene_buffer, &src_box);
    return ky_scene_node_from_buffer(scene_buffer);
}

static void shadow_create_parts(struct shadow *shadow)
{
    for (int i = 0; i < SHADOW_PART_COUNT; i++) {
        shadow->parts[i].node = shadow_part_create(shadow->tree, i);
    }
}

#define UPDATE_PART_POSITION(i, x, y) ky_scene_node_set_position(shadow->parts[i].node, x, y);
#define UPDATE_PART_SIZE(i, w, h)                                                                  \
    ky_scene_buffer_set_dest_size(ky_scene_buffer_from_node(shadow->parts[i].node), w, h);

static void set_shadow_part(struct shadow *shadow, uint32_t mask)
{
    for (int i = SHADOW_PART_TOP_LEFT; i < SHADOW_PART_COUNT; i++) {
        ky_scene_node_set_enabled(shadow->parts[i].node, (mask >> i) & 0x1);
    }
}

static void shadow_update_parts_tiled(struct shadow *shadow, enum kywc_tile tiled)
{
    struct theme *theme = theme_manager_get_current();
    struct kywc_view *view = shadow->kywc_view;
    /* use view margin to calc all positions */
    int size = theme->shadow_border + theme->corner_radius;
    int w = view->geometry.width + view->margin.off_width - 2 * theme->corner_radius;
    int h = view->geometry.height + view->margin.off_height - 2 * theme->corner_radius;
    int x1 = -theme->shadow_border;
    int x2 = theme->corner_radius;
    int x3 = theme->corner_radius + w;
    int y1 = -theme->shadow_border;
    int y2 = theme->corner_radius;
    int y3 = theme->corner_radius + h;

    switch (tiled) {
    case KYWC_TILE_TOP:
        UPDATE_PART_POSITION(SHADOW_PART_BOTTOM, 0, y3);
        UPDATE_PART_SIZE(SHADOW_PART_BOTTOM, w + 2 * theme->corner_radius, size);
        set_shadow_part(shadow, SHADOW_MASK_BOTTOM);
        break;
    case KYWC_TILE_BOTTOM:
        UPDATE_PART_POSITION(SHADOW_PART_TOP, 0, y1);
        UPDATE_PART_SIZE(SHADOW_PART_TOP, w + 2 * theme->corner_radius, size);
        set_shadow_part(shadow, SHADOW_MASK_TOP);
        break;
    case KYWC_TILE_LEFT:
        UPDATE_PART_POSITION(SHADOW_PART_RIGHT, x3, 0);
        UPDATE_PART_SIZE(SHADOW_PART_RIGHT, size, h + 2 * theme->corner_radius);
        set_shadow_part(shadow, SHADOW_MASK_RIGHT);
        break;
    case KYWC_TILE_RIGHT:
        UPDATE_PART_POSITION(SHADOW_PART_LEFT, x1, 0);
        UPDATE_PART_SIZE(SHADOW_PART_LEFT, size, h + 2 * theme->corner_radius);
        set_shadow_part(shadow, SHADOW_MASK_LEFT);
        break;
    case KYWC_TILE_TOP_LEFT:
        UPDATE_PART_POSITION(SHADOW_PART_RIGHT, x3, 0);
        UPDATE_PART_POSITION(SHADOW_PART_BOTTOM, 0, y3);
        UPDATE_PART_POSITION(SHADOW_PART_BOTTOM_RIGHT, x3, y3);
        UPDATE_PART_SIZE(SHADOW_PART_RIGHT, size, h + theme->corner_radius);
        UPDATE_PART_SIZE(SHADOW_PART_BOTTOM, w + theme->corner_radius, size);
        set_shadow_part(shadow, SHADOW_MASK_RIGHT | SHADOW_MASK_BOTTOM_RIGHT | SHADOW_MASK_BOTTOM);
        break;
    case KYWC_TILE_BOTTOM_LEFT:
        UPDATE_PART_POSITION(SHADOW_PART_TOP, 0, y1);
        UPDATE_PART_POSITION(SHADOW_PART_RIGHT, x3, y2);
        UPDATE_PART_POSITION(SHADOW_PART_TOP_RIGHT, x3, y1);
        UPDATE_PART_SIZE(SHADOW_PART_RIGHT, size, h + theme->corner_radius);
        UPDATE_PART_SIZE(SHADOW_PART_TOP, w + theme->corner_radius, size);
        set_shadow_part(shadow, SHADOW_MASK_RIGHT | SHADOW_MASK_TOP_RIGHT | SHADOW_MASK_TOP);
        break;
    case KYWC_TILE_TOP_RIGHT:
        UPDATE_PART_POSITION(SHADOW_PART_LEFT, x1, 0);
        UPDATE_PART_POSITION(SHADOW_PART_BOTTOM, x2, y3);
        UPDATE_PART_POSITION(SHADOW_PART_BOTTOM_LEFT, x1, y3);
        UPDATE_PART_SIZE(SHADOW_PART_LEFT, size, h + theme->corner_radius);
        UPDATE_PART_SIZE(SHADOW_PART_BOTTOM, w + theme->corner_radius, size);
        set_shadow_part(shadow, SHADOW_MASK_LEFT | SHADOW_MASK_BOTTOM_LEFT | SHADOW_MASK_BOTTOM);
        break;
    case KYWC_TILE_BOTTOM_RIGHT:
        UPDATE_PART_POSITION(SHADOW_PART_LEFT, x1, y2);
        UPDATE_PART_POSITION(SHADOW_PART_TOP, x2, y1);
        UPDATE_PART_SIZE(SHADOW_PART_LEFT, size, h + theme->corner_radius);
        UPDATE_PART_SIZE(SHADOW_PART_TOP, w + theme->corner_radius, size);
        set_shadow_part(shadow, SHADOW_MASK_LEFT | SHADOW_MASK_TOP_LEFT | SHADOW_MASK_TOP);
        break;
    default:
        break;
    }
}

static void shadow_update_parts(struct shadow *shadow, uint32_t cause)
{
    struct theme *theme = theme_manager_get_current();
    struct kywc_view *view = shadow->kywc_view;

    /* use view margin to calc all positions */
    int size = theme->shadow_border + theme->corner_radius;
    int w = view->geometry.width + view->margin.off_width - 2 * theme->corner_radius;
    int h = view->geometry.height + view->margin.off_height - 2 * theme->corner_radius;
    int x1 = -theme->shadow_border;
    int x2 = theme->corner_radius;
    int x3 = theme->corner_radius + w;
    int y1 = -theme->shadow_border;
    int y2 = theme->corner_radius;
    int y3 = theme->corner_radius + h;

    if (cause & (SHADOW_UPDATE_CAUSE_MAXIMIZE | SHADOW_UPDATE_CAUSE_FULLSCREEN)) {
        bool enabled = !view->maximized && !view->fullscreen;
        ky_scene_node_set_enabled(shadow->node, enabled);
    }

    if (cause & SHADOW_UPDATE_CAUSE_CREATE) {
        ky_scene_node_set_position(shadow->node, -view->margin.off_x, -view->margin.off_y);
        UPDATE_PART_POSITION(SHADOW_PART_TOP_LEFT, x1, y1);
        UPDATE_PART_POSITION(SHADOW_PART_TOP, x2, y1);
        UPDATE_PART_POSITION(SHADOW_PART_LEFT, x1, y2);
        UPDATE_PART_SIZE(SHADOW_PART_TOP_LEFT, size, size);
        UPDATE_PART_SIZE(SHADOW_PART_TOP_RIGHT, size, size);
        UPDATE_PART_SIZE(SHADOW_PART_BOTTOM_RIGHT, size, size);
        UPDATE_PART_SIZE(SHADOW_PART_BOTTOM_LEFT, size, size);
    }

    if (cause & SHADOW_UPDATE_CAUSE_TILE) {
        if (view->tiled) {
            set_shadow_part(shadow, 0);
            shadow_update_parts_tiled(shadow, view->tiled);
        } else {
            UPDATE_PART_POSITION(SHADOW_PART_LEFT, x1, y2);
            UPDATE_PART_POSITION(SHADOW_PART_TOP, x2, y1);
            UPDATE_PART_POSITION(SHADOW_PART_RIGHT, x3, y2);
            UPDATE_PART_POSITION(SHADOW_PART_BOTTOM, x2, y3);

            UPDATE_PART_SIZE(SHADOW_PART_LEFT, size, h);
            UPDATE_PART_SIZE(SHADOW_PART_TOP, w, size);
            UPDATE_PART_SIZE(SHADOW_PART_RIGHT, size, h);
            UPDATE_PART_SIZE(SHADOW_PART_BOTTOM, w, size);

            set_shadow_part(shadow, SHADOW_MASK_ALL);
        }
    }

    if (cause & SHADOW_UPDATE_CAUSE_SIZE) {

        if (view->tiled) {
            /* When the zoom ratio changes, recalculate the shading when tiled */
            shadow_update_parts_tiled(shadow, view->tiled);
            return;
        }

        if (shadow->view_width != view->geometry.width) {
            UPDATE_PART_POSITION(SHADOW_PART_TOP_RIGHT, x3, y1);
            UPDATE_PART_POSITION(SHADOW_PART_RIGHT, x3, y2);
            UPDATE_PART_SIZE(SHADOW_PART_TOP, w, size);
            UPDATE_PART_SIZE(SHADOW_PART_BOTTOM, w, size);
        }
        if (shadow->view_height != view->geometry.height) {
            UPDATE_PART_POSITION(SHADOW_PART_BOTTOM, x2, y3);
            UPDATE_PART_POSITION(SHADOW_PART_BOTTOM_LEFT, x1, y3);
            UPDATE_PART_SIZE(SHADOW_PART_RIGHT, size, h);
            UPDATE_PART_SIZE(SHADOW_PART_LEFT, size, h);
        }
        UPDATE_PART_POSITION(SHADOW_PART_BOTTOM_RIGHT, x3, y3);
    }
}

#undef UPDATE_PART_POSITON
#undef UPDATE_PART_SIZE

static void handle_theme_update(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, theme_update);
    shadow_update_parts(shadow, SHADOW_UPDATE_CAUSE_ALL);
}

static void handle_view_size(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, view_size);
    shadow_update_parts(shadow, SHADOW_UPDATE_CAUSE_SIZE);

    shadow->view_width = shadow->kywc_view->geometry.width;
    shadow->view_height = shadow->kywc_view->geometry.height;
}

static void handle_view_tile(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, view_tile);
    shadow_update_parts(shadow, SHADOW_UPDATE_CAUSE_TILE);
}

static void handle_view_maximize(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, view_maximize);
    shadow_update_parts(shadow, SHADOW_UPDATE_CAUSE_MAXIMIZE);
}

static void handle_view_fullscreen(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, view_fullscreen);
    shadow_update_parts(shadow, SHADOW_UPDATE_CAUSE_FULLSCREEN);
}

static void handle_view_decoration(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, view_decoration);
    shadow_update_parts(shadow, SHADOW_UPDATE_CAUSE_ALL);
}

static void shadow_parts_create(struct shadow *shadow)
{
    if (shadow->created) {
        return;
    }
    shadow->created = true;
    shadow->view_width = shadow->view_height = 0;

    struct kywc_view *kywc_view = shadow->kywc_view;
    shadow->view_size.notify = handle_view_size;
    wl_signal_add(&kywc_view->events.size, &shadow->view_size);
    shadow->view_tile.notify = handle_view_tile;
    wl_signal_add(&kywc_view->events.tile, &shadow->view_tile);
    shadow->view_maximize.notify = handle_view_maximize;
    wl_signal_add(&kywc_view->events.maximize, &shadow->view_maximize);
    shadow->view_fullscreen.notify = handle_view_fullscreen;
    wl_signal_add(&kywc_view->events.fullscreen, &shadow->view_fullscreen);
    shadow->view_decoration.notify = handle_view_decoration;
    wl_signal_add(&kywc_view->events.decoration, &shadow->view_decoration);
    shadow->theme_update.notify = handle_theme_update;
    theme_manager_add_update_listener(&shadow->theme_update);

    struct view *view = view_from_kywc_view(kywc_view);
    shadow->tree = ky_scene_tree_create(view->tree);
    shadow->node = ky_scene_node_from_tree(shadow->tree);
    ky_scene_node_lower_to_bottom(shadow->node);

    shadow_create_parts(shadow);
    shadow_update_parts(shadow, SHADOW_UPDATE_CAUSE_ALL);

    shadow->view_width = kywc_view->geometry.width;
    shadow->view_height = kywc_view->geometry.height;
}

static void shadow_parts_destroy(struct shadow *shadow)
{
    if (!shadow->created) {
        return;
    }
    shadow->created = false;

    wl_list_remove(&shadow->view_size.link);
    wl_list_remove(&shadow->view_tile.link);
    wl_list_remove(&shadow->view_maximize.link);
    wl_list_remove(&shadow->view_fullscreen.link);
    wl_list_remove(&shadow->view_decoration.link);
    wl_list_remove(&shadow->theme_update.link);

    ky_scene_node_destroy(shadow->node);
}

static void handle_view_shadow(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, view_shadow);
    if (!shadow->kywc_view->need_shadow) {
        shadow_parts_destroy(shadow);
    } else {
        shadow_parts_create(shadow);
    }
}

static void handle_view_map(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, view_map);
    struct kywc_view *kywc_view = shadow->kywc_view;

    shadow->view_shadow.notify = handle_view_shadow;
    wl_signal_add(&kywc_view->events.shadow, &shadow->view_shadow);

    /* skip if not need shadow */
    if (!kywc_view->need_shadow) {
        return;
    }

    shadow_parts_create(shadow);
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, view_unmap);
    wl_list_remove(&shadow->view_shadow.link);
    shadow_parts_destroy(shadow);
}

static void handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct shadow *shadow = wl_container_of(listener, shadow, view_destroy);
    wl_list_remove(&shadow->view_destroy.link);
    wl_list_remove(&shadow->view_map.link);
    wl_list_remove(&shadow->view_unmap.link);
    wl_list_remove(&shadow->link);

    free(shadow);
}

static void handle_new_view(struct wl_listener *listener, void *data)
{
    struct shadow_manager *manager = wl_container_of(listener, manager, new_view);
    struct kywc_view *kywc_view = data;

    struct shadow *shadow = calloc(1, sizeof(struct shadow));
    if (!shadow) {
        return;
    }

    shadow->kywc_view = kywc_view;
    wl_list_insert(&manager->shadows, &shadow->link);

    shadow->view_map.notify = handle_view_map;
    wl_signal_add(&kywc_view->events.map, &shadow->view_map);
    shadow->view_unmap.notify = handle_view_unmap;
    wl_signal_add(&kywc_view->events.unmap, &shadow->view_unmap);
    shadow->view_destroy.notify = handle_view_destroy;
    wl_signal_add(&kywc_view->events.destroy, &shadow->view_destroy);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct shadow_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    wl_list_remove(&manager->new_view.link);
    free(manager);
}

bool shadow_manager_create(struct view_manager *view_manager)
{
    struct shadow_manager *manager = calloc(1, sizeof(struct shadow_manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->shadows);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &manager->server_destroy);
    manager->new_view.notify = handle_new_view;
    kywc_view_add_new_listener(&manager->new_view);

    return true;
}
