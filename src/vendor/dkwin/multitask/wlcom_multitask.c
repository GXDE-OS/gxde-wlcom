/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * This is NOT a vendored file, but a shim to allow Deepin KWin's multitask to
 * run on GXDE Wlcom.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <kywc/binding.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_buffer.h>

#include "input/cursor.h"
#include "input/event.h"
#include "input/input.h"
#include "input/seat.h"
#include "output.h"
#include "painter.h"
#include "scene/thumbnail.h"
#include "server.h"
#include "util/file.h"
#include "view/workspace.h"
#include "src/vendor/dkwin/multitask/multitask_assets.h"
#include "../../../view/view_p.h"

#define BRIGHTNESS 0.4f
#define FIRST_WIN_SCALE (720.0f / 1080.0f)
#define WORKSPACE_SCALE (240.0f / 1920.0f)
#define WORK_SPACING_SCALE (40.0f / 1920.0f)
#define SPACING_H_SCALE (20.0f / 1080.0f)
#define SPACING_W_SCALE (20.0f / 1920.0f)
#define MAX_DESKTOP_COUNT 8
#define WINDOW_BORDER_WIDTH 3
#define ADD_BUTTON_SIZE 64
#define WORKSPACE_CORNER_RADIUS 8

enum hover_control {
    CONTROL_NONE,
    CONTROL_WINDOW,
    CONTROL_WINDOW_CLOSE,
    CONTROL_WINDOW_TOP,
    CONTROL_WORKSPACE,
    CONTROL_WORKSPACE_CLOSE,
    CONTROL_ADD_WORKSPACE,
};

struct multitask_item {
    struct multitask_view *overview;
    struct view *view;
    struct thumbnail *thumbnail;
    struct ky_scene_tree *tree;
    struct ky_scene_rect *highlight;
    struct ky_scene_buffer *buffer;
    struct ky_scene_buffer *close_button;
    struct ky_scene_buffer *top_button;
    struct kywc_box geometry;
    struct kywc_box close_geometry;
    struct kywc_box top_geometry;
    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;
    struct wl_listener view_destroy;
};

struct workspace_item {
    struct workspace *workspace;
    struct ky_scene_tree *tree;
    struct ky_scene_rect *rect;
    struct ky_scene_buffer *wallpaper;
    struct ky_scene_buffer *buffer;
    struct ky_scene_buffer *close_button;
    struct thumbnail *thumbnail;
    struct kywc_box geometry;
    int thumbnail_width, thumbnail_height;
    struct kywc_box close_geometry;
    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;
};

struct multitask_view {
    struct view_manager *view_manager;
    struct ky_scene_tree *tree;
    struct ky_scene_buffer *desktop_wallpaper;
    struct ky_scene_rect *backdrop;
    struct ky_scene_rect *workspace_bar;

    struct multitask_item **items;
    size_t item_count;
    struct workspace_item *workspaces;
    size_t workspace_count;
    int hovered_item;
    int hovered_workspace;
    int pressed_item;
    enum hover_control hovered_control;
    enum hover_control pressed_control;
    double press_x, press_y;
    bool dragging;
    bool enabled;

    struct ky_scene_buffer *add_button;
    struct kywc_box add_geometry;
    struct wlr_buffer *close_icon;
    struct wlr_buffer *top_icon;
    struct wlr_buffer *top_active_icon;
    struct wlr_buffer *add_icon;
    struct wlr_buffer *wallpaper_buffer;

    struct output *output;
    struct seat_pointer_grab pointer_grab;
    struct seat_keyboard_grab keyboard_grab;
    struct seat_touch_grab touch_grab;
    struct wl_listener server_destroy;
};

static struct multitask_view *overview;

static void multitask_view_set_enabled(bool enabled);

static void asset_path(char *path, size_t size, const char *name)
{
    snprintf(path, size, "%s/%s", MULTITASK_SOURCE_ASSET_DIR, name);
    if (!file_exists(path)) {
        snprintf(path, size, "%s/%s", MULTITASK_INSTALL_ASSET_DIR, name);
    }
}

static struct wlr_buffer *load_svg_icon(const char *name, int width, int height)
{
    char path[4096];
    asset_path(path, sizeof(path), name);
    struct file *file = file_open(path, NULL, NULL);
    if (!file) {
        return NULL;
    }
    size_t size = 0;
    const char *mapped = file_get_data(file, &size);
    char *svg = malloc(size + 1);
    if (!svg) {
        file_close(file);
        return NULL;
    }
    memcpy(svg, mapped, size);
    svg[size] = '\0';
    struct draw_info info = { .width = width, .height = height, .scale = 1.0f, .svg = svg };
    struct wlr_buffer *buffer = painter_draw_buffer(&info);
    free(svg);
    file_close(file);
    return buffer;
}

static struct wlr_buffer *load_png_icon(const char *name)
{
    char path[4096];
    asset_path(path, sizeof(path), name);
    struct draw_info info = { .image = path, .scale = 1.0f };
    return painter_draw_buffer(&info);
}

static char *get_wallpaper_path(void)
{
    char *path = NULL;
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema =
        source ? g_settings_schema_source_lookup(
            source, "com.deepin.wrap.gnome.desktop.background", true)
            : NULL;

    if (schema && g_settings_schema_has_key(schema, "picture-uri")) {
        GSettings *settings = g_settings_new_full(schema, NULL, NULL);
        char *uri = g_settings_get_string(settings, "picture-uri");
        if (g_str_has_prefix(uri, "file://")) {
            path = g_filename_from_uri(uri, NULL, NULL);
        } else if (*uri) {
            path = g_strdup(uri);
        }
        g_free(uri);
        g_object_unref(settings);
    }

    if (schema) {
        g_settings_schema_unref(schema);
    }

    if (path && file_exists(path)) {
        return path;
    }
    g_free(path);

    static const char *fallbacks[] = {
        "/usr/share/backgrounds/default_background.jpg",
        "/usr/share/wallpapers/deepin/desktop.jpg",
    };

    for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
        if (file_exists(fallbacks[i])) {
            return g_strdup(fallbacks[i]);
        }
    }
    return NULL;
}

static struct wlr_buffer *load_wallpaper(void)
{
    char *path = get_wallpaper_path();
    if (!path) {
        return NULL;
    }
    struct draw_info info = { .image = path, .scale = 1.0f };
    struct wlr_buffer *buffer = painter_draw_buffer(&info);
    g_free(path);
    return buffer;
}

static void scene_buffer_set_cover(struct ky_scene_buffer *scene_buffer,
                                   struct wlr_buffer *buffer, int width, int height)
{
    if (!scene_buffer || !buffer || width <= 0 || height <= 0) {
        return;
    }

    const double destination_ratio = (double)width / height;
    const double source_ratio = (double)buffer->width / buffer->height;
    struct wlr_fbox source = { 0, 0, buffer->width, buffer->height };
    if (source_ratio > destination_ratio) {
        source.width = buffer->height * destination_ratio;
        source.x = (buffer->width - source.width) / 2.0;
    } else {
        source.height = buffer->width / destination_ratio;
        source.y = (buffer->height - source.height) / 2.0;
    }
    ky_scene_buffer_set_source_box(scene_buffer, &source);
    ky_scene_buffer_set_dest_size(scene_buffer, width, height);
}

static void scene_node_set_rounded_clip(struct ky_scene_node *node, int width, int height,
                                        int radius)
{
    pixman_region32_t region;
    pixman_region32_init(&region);

    radius = radius < width / 2 ? radius : width / 2;
    radius = radius < height / 2 ? radius : height / 2;
    for (int y = 0; y < height; y++) {
        int inset = 0;
        if (radius > 0 && (y < radius || y >= height - radius)) {
            const double center_y =
                y < radius ? radius : height - radius;
            const double distance_y = fabs((y + 0.5) - center_y);
            const double circle_width =
                sqrt(radius * radius - distance_y * distance_y);
            inset = ceil(radius - circle_width);
        }
        pixman_region32_union_rect(&region, &region, inset, y,
                                   width - inset * 2, 1);
    }

    ky_scene_node_set_clip_region(node, &region);
    pixman_region32_fini(&region);
}

static bool load_original_assets(void)
{
    overview->close_icon = load_svg_icon("multiview_delete.svg", 70, 70);
    overview->top_icon = load_svg_icon("multiview_top.svg", 70, 70);
    overview->top_active_icon = load_svg_icon("multiview_top_active.svg", 72, 72);
    overview->add_icon = load_png_icon("add-light.png");
    return overview->close_icon && overview->top_icon && overview->top_active_icon &&
           overview->add_icon;
}

static bool point_in_box(double x, double y, const struct kywc_box *box)
{
    return x >= box->x && y >= box->y && x < box->x + box->width && y < box->y + box->height;
}

static void set_item_hover(int index)
{
    if (overview->hovered_item == index) {
        return;
    }
    if (overview->hovered_item >= 0) {
        struct multitask_item *old = overview->items[overview->hovered_item];
        ky_scene_rect_set_color(old->highlight, (float[4]){ 0.0f, 0.0f, 0.0f, 0.20f });
        ky_scene_node_set_enabled(&old->close_button->node, false);
        ky_scene_node_set_enabled(&old->top_button->node, false);
    }
    overview->hovered_item = index;
    if (index >= 0) {
        struct multitask_item *item = overview->items[index];
        ky_scene_rect_set_color(item->highlight, (float[4]){ 0.0f, 0.0f, 0.0f, 1.0f });
        ky_scene_buffer_set_buffer(item->top_button,
                                   item->view->base.kept_above ? overview->top_active_icon
                                                               : overview->top_icon);
        ky_scene_node_set_enabled(&item->close_button->node, true);
        ky_scene_node_set_enabled(&item->top_button->node, true);
    }
    if (overview->output) {
        output_schedule_frame(overview->output->wlr_output);
    }
}

static void update_hover(double lx, double ly)
{
    int item_index = -1;
    int workspace_index = -1;
    for (size_t i = 0; i < overview->item_count; i++) {
        if (point_in_box(lx, ly, &overview->items[i]->geometry) ||
            point_in_box(lx, ly, &overview->items[i]->close_geometry) ||
            point_in_box(lx, ly, &overview->items[i]->top_geometry)) {
            item_index = i;
            break;
        }
    }
    for (size_t i = 0; i < overview->workspace_count; i++) {
        if (point_in_box(lx, ly, &overview->workspaces[i].geometry) ||
            point_in_box(lx, ly, &overview->workspaces[i].close_geometry)) {
            workspace_index = i;
            break;
        }
    }
    if (overview->hovered_workspace != workspace_index) {
        if (overview->hovered_workspace >= 0) {
            struct workspace_item *old = &overview->workspaces[overview->hovered_workspace];
            ky_scene_node_set_enabled(&old->close_button->node, false);
        }
        overview->hovered_workspace = workspace_index;
        if (workspace_index >= 0 && overview->workspace_count > 1) {
            struct workspace_item *item = &overview->workspaces[workspace_index];
            ky_scene_node_set_enabled(&item->close_button->node, true);
        }
    }
    set_item_hover(item_index);

    overview->hovered_control = CONTROL_NONE;
    if (point_in_box(lx, ly, &overview->add_geometry) &&
        overview->workspace_count < MAX_DESKTOP_COUNT) {
        overview->hovered_control = CONTROL_ADD_WORKSPACE;
    } else if (item_index >= 0) {
        struct multitask_item *item = overview->items[item_index];
        if (point_in_box(lx, ly, &item->close_geometry)) {
            overview->hovered_control = CONTROL_WINDOW_CLOSE;
        } else if (point_in_box(lx, ly, &item->top_geometry)) {
            overview->hovered_control = CONTROL_WINDOW_TOP;
        } else {
            overview->hovered_control = CONTROL_WINDOW;
        }
    } else if (workspace_index >= 0) {
        struct workspace_item *item = &overview->workspaces[workspace_index];
        overview->hovered_control = point_in_box(lx, ly, &item->close_geometry) &&
                                                overview->workspace_count > 1
                                            ? CONTROL_WORKSPACE_CLOSE
                                            : CONTROL_WORKSPACE;
    }
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct multitask_item *item = wl_container_of(listener, item, thumbnail_update);
    struct thumbnail_update_event *event = data;
    if (event->buffer_changed && item->buffer->buffer != event->buffer) {
        ky_scene_buffer_set_buffer(item->buffer, event->buffer);
    } else {
        ky_scene_node_push_damage(&item->buffer->node, KY_SCENE_DAMAGE_HARMLESS, NULL);
    }
    ky_scene_buffer_set_dest_size(item->buffer,
        item->geometry.width - WINDOW_BORDER_WIDTH * 2,
        item->geometry.height - WINDOW_BORDER_WIDTH * 2);
    if (item->view->tree->node.enabled) {
        ky_scene_node_set_enabled(&item->view->tree->node, false);
    }
}

static void handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct multitask_item *item = wl_container_of(listener, item, thumbnail_destroy);
    wl_list_remove(&item->thumbnail_update.link);
    wl_list_remove(&item->thumbnail_destroy.link);
    item->thumbnail = NULL;
}

static void handle_item_view_destroy(struct wl_listener *listener, void *data)
{
    multitask_view_set_enabled(false);
}

static void handle_workspace_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct workspace_item *item = wl_container_of(listener, item, thumbnail_update);
    struct thumbnail_update_event *event = data;
    if (event->buffer_changed && item->buffer->buffer != event->buffer) {
        ky_scene_buffer_set_buffer(item->buffer, event->buffer);
    } else {
        ky_scene_node_push_damage(&item->buffer->node, KY_SCENE_DAMAGE_HARMLESS, NULL);
    }
    ky_scene_buffer_set_dest_size(item->buffer, item->thumbnail_width, item->thumbnail_height);
}

static void handle_workspace_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct workspace_item *item = wl_container_of(listener, item, thumbnail_destroy);
    wl_list_remove(&item->thumbnail_update.link);
    wl_list_remove(&item->thumbnail_destroy.link);
    item->thumbnail = NULL;
}

static void destroy_contents(void)
{
    for (size_t i = 0; i < overview->item_count; i++) {
        struct multitask_item *item = overview->items[i];
        if (item->view->base.mapped && !item->view->base.minimized) {
            ky_scene_node_set_enabled(&item->view->tree->node, true);
        }
        wl_list_remove(&item->view_destroy.link);
        if (item->thumbnail) {
            thumbnail_destroy(item->thumbnail);
        }
        ky_scene_node_destroy(&item->tree->node);
        free(item);
    }
    free(overview->items);
    overview->items = NULL;
    overview->item_count = 0;
    for (size_t i = 0; i < overview->workspace_count; i++) {
        struct workspace_item *item = &overview->workspaces[i];
        if (item->thumbnail) {
            thumbnail_destroy(item->thumbnail);
        }
        ky_scene_node_destroy(&item->tree->node);
    }
    free(overview->workspaces);
    overview->workspaces = NULL;
    overview->workspace_count = 0;
    if (overview->add_button) {
        ky_scene_node_destroy(&overview->add_button->node);
        overview->add_button = NULL;
    }
}

static size_t count_views(struct workspace *workspace, struct kywc_output *output)
{
    size_t count = 0;
    struct view_proxy *proxy;
    wl_list_for_each(proxy, &workspace->view_proxies, workspace_link) {
        struct view *view = proxy->view;
        if (view->base.mapped && !view->base.skip_switcher && view->output == output) {
            count++;
        }
    }
    return count;
}

static int create_workspace_bar(const struct kywc_box *area)
{
    overview->workspace_count = workspace_manager_get_count();
    overview->workspaces = calloc(overview->workspace_count, sizeof(*overview->workspaces));
    int workspace_width = lroundf(area->width * WORKSPACE_SCALE);
    int workspace_height = lroundf(area->height * WORKSPACE_SCALE);
    int workspace_gap = lroundf(area->width * WORK_SPACING_SCALE);
    int spacing_y = lroundf(area->height * SPACING_H_SCALE);
    int total_width = overview->workspace_count * workspace_width +
                      (overview->workspace_count - 1) * workspace_gap;
    int x = area->x + (area->width - total_width) / 2;
    int y = area->y + spacing_y;
    for (size_t i = 0; i < overview->workspace_count; i++) {
        struct workspace_item *item = &overview->workspaces[i];
        item->workspace = workspace_by_position(i);
        item->geometry = (struct kywc_box){ x, y, workspace_width, workspace_height };
        item->thumbnail_width = workspace_width - 4;
        item->thumbnail_height = workspace_height - 4;
        const bool active = item->workspace == workspace_manager_get_current();
        item->tree = ky_scene_tree_create(overview->tree);
        ky_scene_node_set_position(&item->tree->node, x, y);
        item->rect = ky_scene_rect_create(item->tree, workspace_width, workspace_height,
                                          active ? (float[4]){ 0.0f, 0.506f, 1.0f, 1.0f }
                                                 : (float[4]){ 1.0f, 1.0f, 1.0f, 0.20f });
        ky_scene_node_set_radius(
            &item->rect->node,
            (int[4]){ WORKSPACE_CORNER_RADIUS, WORKSPACE_CORNER_RADIUS,
                      WORKSPACE_CORNER_RADIUS, WORKSPACE_CORNER_RADIUS });
        scene_node_set_rounded_clip(&item->rect->node, workspace_width, workspace_height,
                                    WORKSPACE_CORNER_RADIUS);
        item->wallpaper = ky_scene_buffer_create(item->tree, overview->wallpaper_buffer);
        ky_scene_node_set_position(&item->wallpaper->node, 2, 2);
        ky_scene_node_set_radius(
            &item->wallpaper->node,
            (int[4]){ WORKSPACE_CORNER_RADIUS - 2, WORKSPACE_CORNER_RADIUS - 2,
                      WORKSPACE_CORNER_RADIUS - 2, WORKSPACE_CORNER_RADIUS - 2 });
        scene_node_set_rounded_clip(&item->wallpaper->node, item->thumbnail_width,
                                    item->thumbnail_height, WORKSPACE_CORNER_RADIUS - 2);
        scene_buffer_set_cover(item->wallpaper, overview->wallpaper_buffer,
                               item->thumbnail_width, item->thumbnail_height);
        item->buffer = ky_scene_buffer_create(item->tree, NULL);
        ky_scene_node_set_position(&item->buffer->node, 2, 2);
        ky_scene_node_set_radius(
            &item->buffer->node,
            (int[4]){ WORKSPACE_CORNER_RADIUS - 2, WORKSPACE_CORNER_RADIUS - 2,
                      WORKSPACE_CORNER_RADIUS - 2, WORKSPACE_CORNER_RADIUS - 2 });
        scene_node_set_rounded_clip(&item->buffer->node, item->thumbnail_width,
                                    item->thumbnail_height, WORKSPACE_CORNER_RADIUS - 2);
        ky_scene_buffer_set_dest_size(item->buffer, item->thumbnail_width,
            item->thumbnail_height);
        item->close_geometry = (struct kywc_box){ x + workspace_width - 30, y - 13, 48, 48 };
        item->close_button = ky_scene_buffer_create(item->tree, overview->close_icon);
        ky_scene_node_set_position(&item->close_button->node, workspace_width - 30, -13);
        ky_scene_buffer_set_dest_size(item->close_button, 48, 48);
        ky_scene_node_set_enabled(&item->close_button->node, false);
        item->thumbnail =
            thumbnail_create_from_workspace(item->workspace, &overview->output->base, 1.0f, false);
        if (item->thumbnail) {
            item->thumbnail_update.notify = handle_workspace_thumbnail_update;
            thumbnail_add_update_listener(item->thumbnail, &item->thumbnail_update);
            item->thumbnail_destroy.notify = handle_workspace_thumbnail_destroy;
            thumbnail_add_destroy_listener(item->thumbnail, &item->thumbnail_destroy);
            thumbnail_mark_wants_update(item->thumbnail, true);
            thumbnail_update(item->thumbnail);
        }
        x += workspace_width + workspace_gap;
    }
    overview->add_geometry =
        (struct kywc_box){
            area->x + area->width - 104,
            y + (workspace_height - ADD_BUTTON_SIZE) / 2,
            ADD_BUTTON_SIZE, ADD_BUTTON_SIZE
        };
    overview->add_button = ky_scene_buffer_create(overview->tree, overview->add_icon);
    ky_scene_node_set_position(&overview->add_button->node, overview->add_geometry.x,
                               overview->add_geometry.y);
    ky_scene_buffer_set_dest_size(overview->add_button, ADD_BUTTON_SIZE, ADD_BUTTON_SIZE);
    ky_scene_node_set_enabled(&overview->add_button->node,
                              overview->workspace_count < MAX_DESKTOP_COUNT);
    return spacing_y * 2 + workspace_height;
}

static bool create_window_items(const struct kywc_box *area, int workspace_bar_height)
{
    struct workspace *workspace = workspace_manager_get_current();
    overview->item_count = count_views(workspace, &overview->output->base);
    if (!overview->item_count) {
        return true;
    }
    overview->items = calloc(overview->item_count, sizeof(*overview->items));
    if (!overview->items) {
        return false;
    }

    struct view **views = calloc(overview->item_count, sizeof(*views));
    if (!views) {
        return false;
    }
    size_t view_count = 0;
    struct view_proxy *proxy;
    wl_list_for_each(proxy, &workspace->view_proxies, workspace_link) {
        struct view *view = proxy->view;
        if (!view->base.mapped || view->base.skip_switcher ||
            view->output != &overview->output->base) {
            continue;
        }
        views[view_count++] = view;
    }

    const int spacing_x = lroundf(area->width * SPACING_W_SCALE);
    const int spacing_y = lroundf(area->height * SPACING_H_SCALE);
    const int content_y = area->y + workspace_bar_height;
    const int content_height = area->height - workspace_bar_height;
    float scale_height = area->height * FIRST_WIN_SCALE;
    int rows = 1;
    int used_rows = 1;
    bool overlap;
    do {
        overlap = false;
        float row_width = spacing_x;
        used_rows = 1;
        for (size_t reverse = view_count; reverse > 0; reverse--) {
            struct view *view = views[reverse - 1];
            float width = view->base.geometry.width;
            if (view->base.geometry.height > scale_height) {
                width *= scale_height / view->base.geometry.height;
            }
            row_width += width + spacing_x;
            if (row_width > area->width) {
                used_rows++;
                if (used_rows > rows) {
                    overlap = true;
                    break;
                }
                row_width = spacing_x + width + spacing_x;
            }
        }
        if (overlap) {
            scale_height -= 15.0f;
            float critical = (content_height - (rows + 2) * spacing_y) / (float)(rows + 1);
            if (scale_height <= critical) {
                rows++;
            }
        }
    } while (overlap && scale_height > 32.0f);

    float *row_widths = calloc(used_rows, sizeof(*row_widths));
    if (!row_widths) {
        free(views);
        return false;
    }
    int row = 0;
    row_widths[0] = spacing_x;
    for (size_t reverse = view_count; reverse > 0; reverse--) {
        struct view *view = views[reverse - 1];
        float width = view->base.geometry.width;
        if (view->base.geometry.height > scale_height) {
            width *= scale_height / view->base.geometry.height;
        }
        if (row_widths[row] + width + spacing_x > area->width && row + 1 < used_rows) {
            row++;
            row_widths[row] = spacing_x;
        }
        row_widths[row] += width + spacing_x;
    }

    float win_y = content_y +
                  (content_height - (used_rows - 1) * spacing_y - used_rows * scale_height) / 2.0f;
    row = 0;
    float x = area->x + (area->width - row_widths[0]) / 2.0f + spacing_x;
    float used_width = spacing_x;
    size_t index = 0;
    for (size_t reverse = view_count; reverse > 0; reverse--) {
        struct view *view = views[reverse - 1];
        float width = view->base.geometry.width;
        float height = view->base.geometry.height;
        if (height > scale_height) {
            float scale = scale_height / height;
            width *= scale;
            height = scale_height;
        }
        if (used_width + width + spacing_x > area->width && row + 1 < used_rows) {
            row++;
            used_width = spacing_x;
            x = area->x + (area->width - row_widths[row]) / 2.0f + spacing_x;
            win_y += scale_height + spacing_y;
        }

        struct multitask_item *item = calloc(1, sizeof(*item));
        if (!item) {
            continue;
        }
        int target_width = lroundf(width);
        int target_height = lroundf(height);
        int target_x = lroundf(x);
        int target_y = lroundf(win_y + (scale_height - height) / 2.0f);

        item->overview = overview;
        item->view = view;
        item->geometry =
            (struct kywc_box){
                target_x - WINDOW_BORDER_WIDTH,
                target_y - WINDOW_BORDER_WIDTH,
                target_width + WINDOW_BORDER_WIDTH * 2,
                target_height + WINDOW_BORDER_WIDTH * 2
            };
        item->tree = ky_scene_tree_create(overview->tree);
        ky_scene_node_set_position(&item->tree->node, target_x - WINDOW_BORDER_WIDTH,
            target_y - WINDOW_BORDER_WIDTH);
        item->highlight =
            ky_scene_rect_create(item->tree, target_width + WINDOW_BORDER_WIDTH * 2,
                target_height + WINDOW_BORDER_WIDTH * 2,
                (float[4]){ 0.0f, 0.0f, 0.0f, 0.20f });
        ky_scene_node_set_radius(&item->highlight->node, (int[4]){ 18, 18, 18, 18 });
        item->buffer = ky_scene_buffer_create(item->tree, NULL);
        ky_scene_node_set_position(&item->buffer->node, WINDOW_BORDER_WIDTH,
            WINDOW_BORDER_WIDTH);
        ky_scene_buffer_set_dest_size(item->buffer, target_width, target_height);
        item->close_geometry =
            (struct kywc_box){ target_x + target_width - 25, target_y - 17, 48, 48 };
        item->top_geometry =
            (struct kywc_box){ target_x - 22, target_y - 17, 48, 48 };
        item->close_button = ky_scene_buffer_create(item->tree, overview->close_icon);
        ky_scene_node_set_position(&item->close_button->node,
            target_width - 25 + WINDOW_BORDER_WIDTH,
            -17 + WINDOW_BORDER_WIDTH);
        ky_scene_buffer_set_dest_size(item->close_button, 48, 48);
        ky_scene_node_set_enabled(&item->close_button->node, false);
        item->top_button = ky_scene_buffer_create(
            item->tree, view->base.kept_above ? overview->top_active_icon : overview->top_icon);
        ky_scene_node_set_position(&item->top_button->node,
            -22 + WINDOW_BORDER_WIDTH,
            -17 + WINDOW_BORDER_WIDTH);
        ky_scene_buffer_set_dest_size(item->top_button, 48, 48);
        ky_scene_node_set_enabled(&item->top_button->node, false);

        item->thumbnail = thumbnail_create_from_view(view, THUMBNAIL_DISABLE_SHADOW, 1.0f);
        if (!item->thumbnail) {
            ky_scene_node_destroy(&item->tree->node);
            free(item);
            continue;
        }
        item->thumbnail_update.notify = handle_thumbnail_update;
        thumbnail_add_update_listener(item->thumbnail, &item->thumbnail_update);
        item->thumbnail_destroy.notify = handle_thumbnail_destroy;
        thumbnail_add_destroy_listener(item->thumbnail, &item->thumbnail_destroy);
        thumbnail_mark_wants_update(item->thumbnail, true);
        thumbnail_update(item->thumbnail);
        item->view_destroy.notify = handle_item_view_destroy;
        wl_signal_add(&view->base.events.destroy, &item->view_destroy);
        overview->items[index++] = item;
        x += width + spacing_x;
        used_width += width + spacing_x;
    }
    overview->item_count = index;
    free(row_widths);
    free(views);
    return true;
}

static bool show_overview(void)
{
    overview->output = input_current_output(input_manager_get_default_seat());
    if (!overview->output) {
        return false;
    }
    const struct kywc_box *area = &overview->output->geometry;
    if (overview->desktop_wallpaper) {
        ky_scene_node_set_position(&overview->desktop_wallpaper->node, 0, 0);
        scene_buffer_set_cover(overview->desktop_wallpaper, overview->wallpaper_buffer,
            area->width, area->height);
    }
    ky_scene_rect_set_size(overview->backdrop, area->width, area->height);
    ky_scene_node_set_position(&overview->tree->node, area->x, area->y);
    ky_scene_node_set_position(&overview->backdrop->node, 0, 0);
    int workspace_bar_height = lroundf(area->height *
                                       (WORKSPACE_SCALE + 2.0f * SPACING_H_SCALE));
    ky_scene_rect_set_size(overview->workspace_bar, area->width, workspace_bar_height);
    ky_scene_node_set_position(&overview->workspace_bar->node, 0, 0);

    struct kywc_box local = { 0, 0, area->width, area->height };
    workspace_bar_height = create_workspace_bar(&local);
    if (!create_window_items(&local, workspace_bar_height)) {
        destroy_contents();
        return false;
    }
    overview->hovered_item = -1;
    overview->hovered_workspace = -1;
    overview->pressed_item = -1;
    overview->hovered_control = overview->pressed_control = CONTROL_NONE;
    ky_scene_node_set_enabled(&overview->tree->node, true);
    output_schedule_frame(overview->output->wlr_output);
    return true;
}

static bool pointer_motion(struct seat_pointer_grab *grab, uint32_t time, double lx, double ly)
{
    double local_x = lx - overview->output->geometry.x;
    double local_y = ly - overview->output->geometry.y;
    update_hover(local_x, local_y);
    if (overview->pressed_item >= 0 && overview->pressed_control == CONTROL_WINDOW) {
        double dx = local_x - overview->press_x;
        double dy = local_y - overview->press_y;
        if (!overview->dragging && dx * dx + dy * dy > 100.0) {
            overview->dragging = true;
        }
        if (overview->dragging) {
            struct multitask_item *item = overview->items[overview->pressed_item];
            ky_scene_node_set_position(&item->tree->node, local_x - item->geometry.width / 2,
                                       local_y - item->geometry.height / 2);
            ky_scene_node_raise_to_top(&item->tree->node);
            output_schedule_frame(overview->output->wlr_output);
        }
    }
    return true;
}

static bool pointer_button(struct seat_pointer_grab *grab, uint32_t time, uint32_t button,
                           bool pressed)
{
    if (button != BTN_LEFT) {
        return true;
    }
    if (pressed) {
        overview->pressed_item = overview->hovered_item;
        overview->pressed_control = overview->hovered_control;
        overview->dragging = false;
        overview->press_x = grab->seat->cursor->lx - overview->output->geometry.x;
        overview->press_y = grab->seat->cursor->ly - overview->output->geometry.y;
        return true;
    }
    struct seat *seat = grab->seat;
    if (overview->pressed_control == CONTROL_WINDOW_CLOSE && overview->pressed_item >= 0 &&
        overview->hovered_control == CONTROL_WINDOW_CLOSE) {
        struct view *view = overview->items[overview->pressed_item]->view;
        multitask_view_set_enabled(false);
        kywc_view_close(&view->base);
    } else if (overview->pressed_control == CONTROL_WINDOW_TOP && overview->pressed_item >= 0 &&
               overview->hovered_control == CONTROL_WINDOW_TOP) {
        struct multitask_item *item = overview->items[overview->pressed_item];
        kywc_view_toggle_kept_above(&item->view->base);
        ky_scene_buffer_set_buffer(item->top_button,
                                   item->view->base.kept_above ? overview->top_active_icon
                                                               : overview->top_icon);
        output_schedule_frame(overview->output->wlr_output);
    } else if (overview->pressed_control == CONTROL_ADD_WORKSPACE &&
               overview->hovered_control == CONTROL_ADD_WORKSPACE &&
               workspace_manager_get_count() < MAX_DESKTOP_COUNT) {
        multitask_view_set_enabled(false);
        workspace_create(NULL, workspace_manager_get_count());
        multitask_view_set_enabled(true);
    } else if (overview->pressed_control == CONTROL_WORKSPACE_CLOSE &&
               overview->hovered_control == CONTROL_WORKSPACE_CLOSE &&
               overview->hovered_workspace >= 0 && workspace_manager_get_count() > 1) {
        struct workspace *workspace = overview->workspaces[overview->hovered_workspace].workspace;
        multitask_view_set_enabled(false);
        workspace_destroy(workspace);
        multitask_view_set_enabled(true);
    } else if (overview->dragging && overview->pressed_item >= 0 &&
               overview->hovered_workspace >= 0) {
        struct view *view = overview->items[overview->pressed_item]->view;
        struct workspace *workspace = overview->workspaces[overview->hovered_workspace].workspace;
        destroy_contents();
        view_set_workspace(view, workspace);
        show_overview();
    } else if (!overview->dragging && overview->pressed_item >= 0) {
        struct view *view = overview->items[overview->pressed_item]->view;
        kywc_view_activate(&view->base);
        view_set_focus(view, seat);
        multitask_view_set_enabled(false);
    } else if (overview->hovered_workspace >= 0) {
        struct workspace *workspace = overview->workspaces[overview->hovered_workspace].workspace;
        multitask_view_set_enabled(false);
        workspace_activate(workspace);
    } else {
        multitask_view_set_enabled(false);
    }
    overview->pressed_item = -1;
    overview->pressed_control = CONTROL_NONE;
    overview->dragging = false;
    return true;
}

static bool pointer_axis(struct seat_pointer_grab *grab, uint32_t time, bool vertical, double value)
{
    return true;
}

static void pointer_cancel(struct seat_pointer_grab *grab)
{
    multitask_view_set_enabled(false);
}

static const struct seat_pointer_grab_interface pointer_impl = {
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .cancel = pointer_cancel,
};

static bool keyboard_key(struct seat_keyboard_grab *grab, struct keyboard *keyboard, uint32_t time,
                         uint32_t key, bool pressed, uint32_t modifiers)
{
    if (pressed && (key == KEY_ESC || key == KEY_S)) {
        multitask_view_set_enabled(false);
    }
    return true;
}

static void keyboard_cancel(struct seat_keyboard_grab *grab)
{
    multitask_view_set_enabled(false);
}

static const struct seat_keyboard_grab_interface keyboard_impl = {
    .key = keyboard_key,
    .cancel = keyboard_cancel,
};

static bool touch_event(struct seat_touch_grab *grab, uint32_t time, bool down)
{
    return pointer_button(&overview->pointer_grab, time, BTN_LEFT, down);
}

static bool touch_motion(struct seat_touch_grab *grab, uint32_t time, double lx, double ly)
{
    return pointer_motion(&overview->pointer_grab, time, lx, ly);
}

static void touch_cancel(struct seat_touch_grab *grab)
{
    multitask_view_set_enabled(false);
}

static const struct seat_touch_grab_interface touch_impl = {
    .touch = touch_event,
    .motion = touch_motion,
    .cancel = touch_cancel,
};

static void multitask_view_set_enabled(bool enabled)
{
    if (!overview || overview->enabled == enabled) {
        return;
    }
    struct seat *seat = input_manager_get_default_seat();
    overview->enabled = enabled;
    if (enabled) {
        if (!show_overview()) {
            overview->enabled = false;
            return;
        }
        seat_start_pointer_grab(seat, &overview->pointer_grab);
        seat_start_keyboard_grab(seat, &overview->keyboard_grab);
        seat_start_touch_grab(seat, &overview->touch_grab);
    } else {
        seat_end_pointer_grab(seat, &overview->pointer_grab);
        seat_end_keyboard_grab(seat, &overview->keyboard_grab);
        seat_end_touch_grab(seat, &overview->touch_grab);
        ky_scene_node_set_enabled(&overview->tree->node, false);
        destroy_contents();
        if (overview->output) {
            output_schedule_frame(overview->output->wlr_output);
        }
        overview->output = NULL;
    }
}

static void shortcut_action(struct key_binding *binding, void *data)
{
    multitask_view_set_enabled(!overview->enabled);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    if (overview->enabled) {
        multitask_view_set_enabled(false);
    }
    wl_list_remove(&overview->server_destroy.link);
    ky_scene_node_destroy(&overview->tree->node);
    if (overview->close_icon) wlr_buffer_drop(overview->close_icon);
    if (overview->top_icon) wlr_buffer_drop(overview->top_icon);
    if (overview->top_active_icon) wlr_buffer_drop(overview->top_active_icon);
    if (overview->add_icon) wlr_buffer_drop(overview->add_icon);
    if (overview->wallpaper_buffer) wlr_buffer_drop(overview->wallpaper_buffer);
    free(overview);
    overview = NULL;
}

bool multitask_view_create(struct view_manager *view_manager)
{
    overview = calloc(1, sizeof(*overview));
    if (!overview) {
        return false;
    }
    overview->view_manager = view_manager;
    overview->hovered_item = overview->hovered_workspace = overview->pressed_item = -1;
    struct view_layer *layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
    overview->tree = ky_scene_tree_create(layer->tree);
    ky_scene_node_set_enabled(&overview->tree->node, false);
    overview->wallpaper_buffer = load_wallpaper();
    if (overview->wallpaper_buffer) {
        overview->desktop_wallpaper =
            ky_scene_buffer_create(overview->tree, overview->wallpaper_buffer);
    }
    overview->backdrop =
        ky_scene_rect_create(overview->tree, 0, 0,
                             (float[4]){ 0.0f, 0.0f, 0.0f, 1.0f - BRIGHTNESS });
    overview->workspace_bar =
        ky_scene_rect_create(overview->tree, 0, 0, (float[4]){ 0.0f, 0.0f, 0.0f, 0.0f });
    if (!load_original_assets()) {
        ky_scene_node_destroy(&overview->tree->node);
        if (overview->close_icon) wlr_buffer_drop(overview->close_icon);
        if (overview->top_icon) wlr_buffer_drop(overview->top_icon);
        if (overview->top_active_icon) wlr_buffer_drop(overview->top_active_icon);
        if (overview->add_icon) wlr_buffer_drop(overview->add_icon);
        if (overview->wallpaper_buffer) wlr_buffer_drop(overview->wallpaper_buffer);
        free(overview);
        overview = NULL;
        return false;
    }

    overview->pointer_grab =
        (struct seat_pointer_grab){ .interface = &pointer_impl, .data = overview };
    overview->keyboard_grab =
        (struct seat_keyboard_grab){ .interface = &keyboard_impl, .data = overview };
    overview->touch_grab = (struct seat_touch_grab){ .interface = &touch_impl, .data = overview };
    overview->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &overview->server_destroy);

    struct key_binding *binding = kywc_key_binding_create("Win+s:no", "toggle multitasking view");
    if (!binding || !kywc_key_binding_register(binding, KEY_BINDING_TYPE_TOGGLE_SHOW_WINDOWS,
                                               shortcut_action, overview)) {
        if (binding) {
            kywc_key_binding_destroy(binding);
        }
        handle_server_destroy(&overview->server_destroy, NULL);
        return false;
    }
    return true;
}
