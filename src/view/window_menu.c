#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "input/cursor.h"
#include "input/event.h"
#include "input/seat.h"
#include "nls.h"
#include "output.h"
#include "painter.h"
#include "theme.h"
#include "view/action.h"
#include "view_p.h"
#include "widget/widget.h"

#define ITEM_WIDTH (160)
#define ITEM_HEIGHT (40)
#define MENU_GAP (2)

struct menu_item {
    struct ky_scene_tree *tree;
    struct wl_listener destroy;
    struct widget *content;

    struct wl_list link; // menu::items
    struct menu *menu;
    struct menu *submenu; // may be NULL

    char *text;
    bool first, last;

    uint32_t key;
    enum window_action action;
};

struct menu {
    struct ky_scene_tree *tree;
    struct wl_listener destroy;

    struct wl_list items;
    struct menu_item *parent;
    struct menu_item *hovered;

    /* redraw menu and items */
    struct wl_listener theme_update;

    int height, width;
    bool enabled;
};

struct window_menu {
    struct wl_list link;
    struct menu *toplevel;
    struct menu *current;

    /* for multiseat */
    struct seat *seat;
    struct wl_listener seat_destroy;
    struct seat_pointer_grab pointer_grab;
    struct seat_keyboard_grab keyboard_grab;
    struct seat_touch_grab touch_grab;

    /* current view */
    struct view *view;
    struct wl_listener view_destroy;

    /* TODO: do something for output, like disable menu if output gone */
    struct output *output;
    // struct wl_listener output_off;
    // struct wl_listener output_destroy;

    bool enabled;
};

struct window_menu_manager {
    struct ky_scene_tree *tree;
    struct wl_list menus;
    struct wl_listener window_menu;
    struct wl_listener server_destroy;
};

static struct window_menu_manager *manager = NULL;

static void window_menu_show(struct seat *seat, struct view *view, int x, int y);

static bool menu_item_action(struct menu_item *item, struct window_menu *window_menu)
{
    if (!window_menu->view || item->action == WINDOW_ACTION_NONE) {
        return false;
    }

    window_action(window_menu->view, window_menu->seat, item->action);
    return true;
}

static struct window_menu *window_menu_by_seat(struct seat *seat)
{
    struct window_menu *window_menu;
    wl_list_for_each(window_menu, &manager->menus, link) {
        if (window_menu->seat == seat) {
            return window_menu;
        }
    }
    return NULL;
}

static void menu_set_enabled(struct menu *menu, bool enabled)
{
    if (menu->enabled == enabled) {
        return;
    }

    menu->hovered = NULL;
    menu->enabled = enabled;
    ky_scene_node_set_enabled(ky_scene_node_from_tree(menu->tree), enabled);

    struct menu_item *item;
    wl_list_for_each(item, &menu->items, link) {
        ky_scene_node_set_enabled(ky_scene_node_from_tree(item->tree), enabled);
        if (!enabled && item->submenu) {
            menu_set_enabled(item->submenu, false);
        }
        widget_set_hovered(item->content, false);
        widget_set_enabled(item->content, enabled);
        widget_update(item->content, true);
    }
}

static void submenu_set_position(struct window_menu *window_menu, struct menu *menu)
{
    struct menu_item *parent = menu->parent;
    struct ky_scene_node *node = ky_scene_node_from_tree(parent->tree);

    int lx, ly;
    ky_scene_node_coords(node, &lx, &ly);

    struct kywc_box *geo = &window_menu->output->geometry;
    int max_x = geo->x + geo->width;
    int max_y = geo->y + geo->height;

    /* default menu position */
    int x = parent->menu->width;
    int y = 0;

    if (lx + parent->menu->width + menu->width > max_x) {
        x = -parent->menu->width;
    }
    int off_y = ly + menu->height - max_y;
    if (off_y > 0) {
        y -= off_y;
    }

    ky_scene_node_set_position(ky_scene_node_from_tree(menu->tree), x, y);
}

static void menu_item_set_hovered(struct menu_item *item)
{
    struct menu_item *hovered = item->menu->hovered;
    if (hovered == item) {
        return;
    }

    if (hovered) {
        widget_set_hovered(hovered->content, false);
        widget_update(hovered->content, true);
        if (hovered->submenu) {
            menu_set_enabled(hovered->submenu, false);
        }
    }

    widget_set_hovered(item->content, true);
    widget_update(item->content, true);
    item->menu->hovered = item;
}

static void menu_show_prev_or_next(struct menu *menu, bool next)
{
    struct menu_item *item;

    if (!menu->hovered) {
        item = wl_container_of(next ? menu->items.prev : menu->items.next, item, link);
    } else {
        struct wl_list *node = next ? menu->hovered->link.prev : menu->hovered->link.next;
        /* skip list head */
        if (node == &menu->items) {
            node = next ? menu->items.prev : menu->items.next;
        }
        item = wl_container_of(node, item, link);
    }

    menu_item_set_hovered(item);
}

static void submenu_show(struct window_menu *window_menu, struct menu *menu, bool hovered)
{
    menu_set_enabled(menu, true);
    submenu_set_position(window_menu, menu);
    window_menu->current = menu;
    if (hovered) {
        menu_show_prev_or_next(menu, true);
    }
}

static void window_menu_set_enabled(struct window_menu *window_menu, bool enabled)
{
    if (window_menu->enabled == enabled) {
        return;
    }

    window_menu->current = NULL;
    window_menu->enabled = enabled;
    menu_set_enabled(window_menu->toplevel, enabled);

    if (!enabled) {
        wl_list_remove(&window_menu->view_destroy.link);
        window_menu->view = NULL;
        seat_end_pointer_grab(window_menu->seat, &window_menu->pointer_grab);
        seat_end_keyboard_grab(window_menu->seat, &window_menu->keyboard_grab);
        seat_end_touch_grab(window_menu->seat, &window_menu->touch_grab);
        return;
    }

    seat_start_pointer_grab(window_menu->seat, &window_menu->pointer_grab);
    seat_start_keyboard_grab(window_menu->seat, &window_menu->keyboard_grab);
    seat_start_touch_grab(window_menu->seat, &window_menu->touch_grab);
    wl_signal_add(&window_menu->view->base.events.destroy, &window_menu->view_destroy);
}

static bool menu_item_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                            uint32_t time, bool first, bool hold, void *data)
{
    struct menu_item *item = data;

    if (first) {
        cursor_set_image(seat->cursor, CURSOR_DEFAULT);
    } else if (item->menu->hovered == item) {
        return false;
    }

    struct window_menu *window_menu = window_menu_by_seat(seat);
    menu_item_set_hovered(item);
    window_menu->current = item->menu;

    if (item->submenu) {
        submenu_show(window_menu, item->submenu, false);
    }

    /* make sure parent item is hovered */
    if (item->menu->parent) {
        menu_item_set_hovered(item->menu->parent);
    }

    return false;
}

static void menu_item_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data)
{
    struct menu_item *item = data;
    /* don't if submenu is enabled */
    if (item->submenu && item->submenu->enabled) {
        return;
    }

    if (item->menu->hovered == item) {
        widget_set_hovered(item->content, false);
        widget_update(item->content, true);
        item->menu->hovered = NULL;
    }
}

static void menu_item_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                            bool pressed, uint32_t time, bool dual, void *data)
{
    /* do actions when released */
    if (pressed) {
        return;
    }

    struct window_menu *window_menu = window_menu_by_seat(seat);
    struct menu_item *item = data;

    if (menu_item_action(item, window_menu)) {
        window_menu_set_enabled(window_menu, false);
    }
}

static const struct input_event_node_impl menu_item_impl = {
    .hover = menu_item_hover,
    .leave = menu_item_leave,
    .click = menu_item_click,
};

static struct ky_scene_node *menu_item_get_root(void *data)
{
    struct menu_item *item = data;
    struct menu *menu = item->menu;
    while (menu->parent) {
        menu = menu->parent->menu;
    }
    return ky_scene_node_from_tree(menu->tree);
}

static bool menu_shortcut(struct window_menu *window_menu, uint32_t key)
{
    struct menu_item *item;
    wl_list_for_each(item, &window_menu->current->items, link) {
        if (item->key != key) {
            continue;
        }
        if (menu_item_action(item, window_menu)) {
            return true;
        } else if (item->submenu) {
            submenu_show(window_menu, item->submenu, true);
            menu_item_set_hovered(item);
        }
        break;
    }
    return false;
}

static bool keyboard_grab_key(struct seat_keyboard_grab *keyboard_grab, uint32_t time, uint32_t key,
                              bool pressed, uint32_t modifiers)
{
    if (!pressed) {
        return true;
    }

    struct window_menu *window_menu = keyboard_grab->data;
    if (!window_menu->current) {
        window_menu->current = window_menu->toplevel;
    }
    struct menu *menu = window_menu->current;

    switch (key) {
    case KEY_UP:
        menu_show_prev_or_next(menu, false);
        break;
    case KEY_DOWN:
        menu_show_prev_or_next(menu, true);
        break;
    case KEY_ESC:
        if (!menu->parent) {
            window_menu_set_enabled(window_menu, false);
            break;
        }
        // fallthrought to left key
    case KEY_LEFT:
        if (menu->parent) {
            menu_set_enabled(menu, false);
            window_menu->current = menu->parent->menu;
        }
        break;
    case KEY_ENTER:
        if (menu->hovered) {
            if (menu_item_action(menu->hovered, window_menu)) {
                window_menu_set_enabled(window_menu, false);
                break;
            }
        }
        // fallthrought to right key
    case KEY_RIGHT:
        if (menu->hovered && menu->hovered->submenu) {
            submenu_show(window_menu, menu->hovered->submenu, true);
        }
        break;
    default:
        if (menu_shortcut(window_menu, key)) {
            window_menu_set_enabled(window_menu, false);
        }
        break;
    }

    return true;
}

static void keyboard_grab_cancel(struct seat_keyboard_grab *keyboard_grab)
{
    struct window_menu *window_menu = keyboard_grab->data;
    window_menu_set_enabled(window_menu, false);
}

static const struct seat_keyboard_grab_interface keyboard_grab_impl = {
    .key = keyboard_grab_key,
    .cancel = keyboard_grab_cancel,
};

static void pointer_grab_cancel(struct seat_pointer_grab *pointer_grab)
{
    struct window_menu *window_menu = pointer_grab->data;
    window_menu_set_enabled(window_menu, false);
}

static bool pointer_grab_button(struct seat_pointer_grab *pointer_grab, uint32_t time,
                                uint32_t button, bool pressed)
{
    struct window_menu *window_menu = pointer_grab->data;
    struct seat *seat = pointer_grab->seat;

    /* check current hover node in the window menu tree */
    struct ky_scene_node *root_node = ky_scene_node_from_tree(window_menu->toplevel->tree);
    struct input_event_node *inode = input_event_node_from_node(seat->cursor->hover.node);
    struct ky_scene_node *node = input_event_node_root(inode);
    if (node == root_node) {
        inode->impl->click(seat, seat->cursor->hover.node, button, pressed, time, false,
                           inode->data);
        return false;
    }
    if (pressed) {
        window_menu_set_enabled(window_menu, false);
    }
    return true;
}

static bool pointer_grab_motion(struct seat_pointer_grab *pointer_grab, uint32_t time, double lx,
                                double ly)
{
    return false;
}

static bool pointer_grab_axis(struct seat_pointer_grab *pointer_grab, uint32_t time, bool vertical,
                              double value)
{
    return true;
}

static const struct seat_pointer_grab_interface pointer_grab_impl = {
    .motion = pointer_grab_motion,
    .button = pointer_grab_button,
    .axis = pointer_grab_axis,
    .cancel = pointer_grab_cancel,
};

static bool touch_grab_touch(struct seat_touch_grab *touch_grab, uint32_t time, bool down)
{
    // FIXME: interactive grab end
    struct window_menu *window_menu = touch_grab->data;
    return pointer_grab_button(&window_menu->pointer_grab, time, BTN_LEFT, down);
}

static bool touch_grab_motion(struct seat_touch_grab *touch_grab, uint32_t time, double lx,
                              double ly)
{
    return false;
}

static void touch_grab_cancel(struct seat_touch_grab *touch_grab)
{
    struct window_menu *window_menu = touch_grab->data;
    window_menu_set_enabled(window_menu, false);
}

static const struct seat_touch_grab_interface touch_grab_impl = {
    .touch = touch_grab_touch,
    .motion = touch_grab_motion,
    .cancel = touch_grab_cancel,
};

static void menu_draw_item(struct menu_item *item)
{
    struct theme *theme = theme_manager_get_current();
    uint32_t border_mask = BORDER_MASK_LEFT | BORDER_MASK_RIGHT | BORDER_MASK_BOTTOM;
    uint32_t corner_mask = CORNER_MASK_NONE;

    if (item->first) {
        border_mask |= BORDER_MASK_TOP;
        corner_mask |= CORNER_MASK_TOP_LEFT | CORNER_MASK_TOP_RIGHT;
    }
    if (item->last) {
        border_mask |= BORDER_MASK_BOTTOM;
        corner_mask |= CORNER_MASK_BOTTOM_LEFT | CORNER_MASK_BOTTOM_RIGHT;
    }

    widget_set_text(item->content, item->text, TEXT_ALIGN_LEFT, !!item->submenu);
    widget_set_font(item->content, theme->font_name, theme->font_size);
    widget_set_size(item->content, ITEM_WIDTH, ITEM_HEIGHT);

    widget_set_backgrond_color(item->content, theme->active_bg_color);
    widget_set_front_color(item->content, theme->active_text_color);
    widget_set_hovered_color(item->content, theme->accent_color);

    widget_set_border(item->content, theme->inactive_border_color, border_mask, 1);
    widget_set_round_coner(item->content, corner_mask, 8);

    widget_update(item->content, true);
}

static void menu_render_items(struct menu *menu)
{
    int y = 0;
    struct menu_item *item;
    wl_list_for_each_reverse(item, &menu->items, link) {
        item->first = menu->items.prev == &item->link;
        item->last = menu->items.next == &item->link;
        menu_draw_item(item);
        ky_scene_node_set_position(ky_scene_node_from_tree(item->tree), 0, y);
        y += ITEM_HEIGHT;

        input_event_node_create(ky_scene_node_from_widget(item->content), &menu_item_impl,
                                menu_item_get_root, NULL, item);
    }

    menu->height = y;
    menu->width = ITEM_WIDTH;
}

static void item_handle_destroy(struct wl_listener *listener, void *data)
{
    struct menu_item *item = wl_container_of(listener, item, destroy);
    wl_list_remove(&item->destroy.link);
    wl_list_remove(&item->link);
    free(item->text);
    free(item);
}

static struct menu_item *menu_add_item(struct menu *menu, char *text, uint32_t key,
                                       enum window_action action)
{
    struct menu_item *item = calloc(1, sizeof(struct menu_item));
    if (!item) {
        return NULL;
    }

    item->menu = menu;
    wl_list_insert(&menu->items, &item->link);

    item->text = strdup(text);
    item->key = key;
    item->action = action;

    item->tree = ky_scene_tree_create(menu->tree);
    item->destroy.notify = item_handle_destroy;
    /* tree destroy event is before node destroy */
    ky_scene_node_add_destroy_listener(ky_scene_node_from_tree(item->tree), &item->destroy);
    /* use widget to create a scene buffer */
    item->content = widget_create(item->tree);

    return item;
}

static void menu_handle_theme_update(struct wl_listener *listener, void *data)
{
    struct menu *menu = wl_container_of(listener, menu, theme_update);

    /* force update all items */
    struct menu_item *item;
    wl_list_for_each(item, &menu->items, link) {
        /* redraw item in current scale */
        menu_draw_item(item);
    }
}

static void menu_handle_destroy(struct wl_listener *listener, void *data)
{
    struct menu *menu = wl_container_of(listener, menu, destroy);
    wl_list_remove(&menu->destroy.link);
    wl_list_remove(&menu->theme_update.link);
    free(menu);
}

static struct menu *menu_create(struct ky_scene_tree *parent, struct menu_item *parent_item)
{
    struct menu *menu = calloc(1, sizeof(struct menu));
    if (!menu) {
        return NULL;
    }

    menu->tree = ky_scene_tree_create(parent);
    struct ky_scene_node *node = ky_scene_node_from_tree(menu->tree);
    ky_scene_node_set_enabled(node, false);
    menu->destroy.notify = menu_handle_destroy;
    ky_scene_node_add_destroy_listener(node, &menu->destroy);

    wl_list_init(&menu->items);
    menu->parent = parent_item;
    ky_scene_node_set_position(ky_scene_node_from_tree(menu->tree),
                               menu->parent ? ITEM_WIDTH - MENU_GAP : 0, 0);

    menu->theme_update.notify = menu_handle_theme_update;
    theme_manager_add_update_listener(&menu->theme_update);

    return menu;
}

static void window_menu_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct window_menu *window_menu = wl_container_of(listener, window_menu, view_destroy);
    window_menu_set_enabled(window_menu, false);
}

static void window_menu_handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct window_menu *window_menu = wl_container_of(listener, window_menu, seat_destroy);
    wl_list_remove(&window_menu->seat_destroy.link);
    wl_list_remove(&window_menu->link);

    window_menu_set_enabled(window_menu, false);
    ky_scene_node_destroy(ky_scene_node_from_tree(window_menu->toplevel->tree));
    free(window_menu);
}

static void window_menu_set_position(struct window_menu *window_menu, int x, int y)
{
    struct kywc_output *kywc_output = kywc_output_at_point(x, y);
    struct output *output = output_from_kywc_output(kywc_output);
    struct kywc_box *geo = &output->geometry;

    /* keep toplevel menu visible in the output */
    int max_x = geo->x + geo->width;
    int max_y = geo->y + geo->height;
    if (x + window_menu->toplevel->width > max_x) {
        x = max_x - window_menu->toplevel->width - MENU_GAP;
    }
    if (y + window_menu->toplevel->height > max_y) {
        y -= window_menu->toplevel->height;
    }

    window_menu->output = output;
    ky_scene_node_set_position(ky_scene_node_from_tree(window_menu->toplevel->tree), x, y);
}

static void menu_add_more_action_submenu(struct menu *menu)
{
    struct menu_item *item = menu_add_item(menu, tr("  More(M)"), KEY_M, WINDOW_ACTION_NONE);
    struct menu *submenu = menu_create(item->tree, item);
    item->submenu = submenu;

    menu_add_item(submenu, tr("  Move(M)"), KEY_M, WINDOW_ACTION_MOVE);
    menu_add_item(submenu, tr("  Resize(R)"), KEY_R, WINDOW_ACTION_RESIZE);
    menu_add_item(submenu, tr("  Keep-Above(A)"), KEY_A, WINDOW_ACTION_KEEP_ABOVE);
    menu_add_item(submenu, tr("  Keep-Below(B)"), KEY_B, WINDOW_ACTION_KEEP_BELOW);
    menu_render_items(submenu);
}

static struct window_menu *window_menu_create(struct seat *seat)
{
    struct window_menu *window_menu = calloc(1, sizeof(struct window_menu));
    if (!window_menu) {
        return NULL;
    }

    wl_list_insert(&manager->menus, &window_menu->link);

    window_menu->seat = seat;
    window_menu->view_destroy.notify = window_menu_handle_view_destroy;
    window_menu->seat_destroy.notify = window_menu_handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &window_menu->seat_destroy);

    window_menu->pointer_grab.data = window_menu;
    window_menu->pointer_grab.interface = &pointer_grab_impl;
    window_menu->keyboard_grab.data = window_menu;
    window_menu->keyboard_grab.interface = &keyboard_grab_impl;
    window_menu->touch_grab.data = window_menu;
    window_menu->touch_grab.interface = &touch_grab_impl;

    /* create the toplevel menu: items and submenus */
    window_menu->toplevel = menu_create(manager->tree, NULL);
    menu_add_item(window_menu->toplevel, tr("  Minimize(N)"), KEY_N, WINDOW_ACTION_MINIMIZE);
    menu_add_item(window_menu->toplevel, tr("  Maximize(X)"), KEY_X, WINDOW_ACTION_MAXIMIZE);
    menu_add_item(window_menu->toplevel, tr("  Fullscreen(F)"), KEY_F, WINDOW_ACTION_FULLSCREEN);
    menu_add_item(window_menu->toplevel, tr("  Close(C)"), KEY_C, WINDOW_ACTION_CLOSE);
    menu_add_more_action_submenu(window_menu->toplevel);
    menu_render_items(window_menu->toplevel);

    return window_menu;
}

static void window_menu_show(struct seat *seat, struct view *view, int x, int y)
{
    struct window_menu *window_menu = window_menu_by_seat(seat);
    if (!window_menu) {
        window_menu = window_menu_create(seat);
    }
    if (!window_menu) {
        return;
    }

    if (window_menu->enabled) {
        window_menu_set_enabled(window_menu, false);
    }
    window_menu->view = view;
    window_menu_set_enabled(window_menu, true);
    window_menu_set_position(window_menu, x, y);
}

static void handle_window_menu(struct wl_listener *listener, void *data)
{
    struct view_show_window_menu_event *event = data;
    window_menu_show(event->seat, event->view, event->x, event->y);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    wl_list_remove(&manager->window_menu.link);
    /* all menus were destroyed by seat_destroy */
    ky_scene_node_destroy(ky_scene_node_from_tree(manager->tree));
    free(manager);
    manager = NULL;
}

bool window_menu_manager_create(struct view_manager *view_manager)
{
    manager = calloc(1, sizeof(struct window_menu_manager));
    if (!manager) {
        return false;
    }

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &manager->server_destroy);
    manager->window_menu.notify = handle_window_menu;
    wl_signal_add(&view_manager->events.window_menu, &manager->window_menu);

    wl_list_init(&manager->menus);
    manager->tree = ky_scene_tree_create(view_manager->layers[LAYER_POPUP].tree);

    /* create the default seat one */
    window_menu_create(input_manager_get_default_seat());

    return true;
}
