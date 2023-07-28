#include <assert.h>
#include <stdlib.h>

#include <linux/input-event-codes.h>
#include <wlr/types/wlr_buffer.h>

#include "input/cursor.h"
#include "output.h"
#include "painter.h"
#include "scene/scene.h"
#include "theme.h"
#include "view_p.h"
#include "widget/scaled_buffer.h"

enum {
    /* buttons */
    SSD_BUTTON_MINIMIZE = 0,
    SSD_BUTTON_MAXIMIZE,
    SSD_BUTTON_CLOSE,

    /* titlebar */
    SSD_CORNER_TOP_LEFT,
    SSD_CORNER_TOP_RIGHT,
    /* one text buffer is enough.
     * when resize or set_title, view always be active.
     */
    SSD_TITLE_TEXT,
    SSD_TITLE_RECT,

    /* border */
    SSD_BORDER_TOP,
    SSD_BORDER_RIGHT,
    SSD_BORDER_BOTTOM,
    SSD_BORDER_LEFT,

    /* extend */
    SSD_EXTEND_TOP_LEFT,
    SSD_EXTEND_TOP,
    SSD_EXTEND_TOP_RIGHT,
    SSD_EXTEND_RIGHT,
    SSD_EXTEND_BOTTOM_RIGHT,
    SSD_EXTEND_BOTTOM,
    SSD_EXTEND_BOTTOM_LEFT,
    SSD_EXTEND_LEFT,
    SSD_PART_COUNT,
};

enum ssd_update_mask {
    SSD_UPDATE_NONE = 0,
    SSD_UPDATE_POS = 1 << 0,
    SSD_UPDATE_SIZE = 1 << 1,
    SSD_UPDATE_COLOR = 1 << 2,
};

enum ssd_update_cause {
    SSD_UPDATE_CAUSE_NONE = 0,
    SSD_UPDATE_CAUSE_SIZE = 1 << 0,
    SSD_UPDATE_CAUSE_MAXIMIZE = 1 << 1,
    SSD_UPDATE_CAUSE_TILE = 1 << 2,
    SSD_UPDATE_CAUSE_TITLE = 1 << 3,
    SSD_UPDATE_CAUSE_ACTIVATE = 1 << 4,
    SSD_UPDATE_CAUSE_FULLSCREEN = 1 << 5,
    SSD_UPDATE_CAUSE_CREATE = 1 << 6,
    SSD_UPDATE_CAUSE_ALL = (1 << 7) - 1,
};

struct ssd_manager {
    /* enable or disable all ssds */
    struct wl_list ssds;

    struct wl_listener new_view;
    struct wl_listener server_destroy;
};

struct ssd_part {
    int type;
    struct ssd *ssd;
    struct ky_scene_node *node;

    enum ssd_update_mask update;
    struct wlr_box geo;
    float scale;
    float *color;
};

struct ssd {
    struct wl_list link;

    struct kywc_view *kywc_view;
    struct wl_listener view_map;
    struct wl_listener view_unmap;
    struct wl_listener view_destroy;
    struct wl_listener view_decoration;
    struct wl_listener view_activate;
    struct wl_listener view_size;
    struct wl_listener view_tile;
    struct wl_listener view_title;
    struct wl_listener view_maximize;
    struct wl_listener view_fullscreen;

    struct wl_listener theme_update;

    struct ky_scene_tree *tree;
    struct ky_scene_tree *button_tree;
    struct ky_scene_tree *titlebar_tree;
    struct ky_scene_tree *border_tree;
    struct ky_scene_tree *extend_tree;

    struct ssd_part parts[SSD_PART_COUNT];

    bool created;
    /* trick to reduce text redraw */
    bool title_is_cut;
    /* view size to reduce redraw */
    int view_width, view_height;
};

static const char ssd_part_name[][SSD_PART_COUNT] = {
    "button_minimize",  "button_maximize",    "button_close", "corner_top_left",
    "corner_top_right", "title_text",         "title_rect",   "border_top",
    "border_right",     "border_bottom",      "border_left",  "extend_top_left",
    "extend_top",       "extend_top_right",   "extend_right", "extend_bottom_right",
    "extend_bottom",    "extend_bottom_left", "extend_left",
};

static enum cursor_name get_resize_name(int type)
{
    switch (type) {
    case SSD_EXTEND_TOP_LEFT:
        return CURSOR_RESIZE_TOP_LEFT;
    case SSD_EXTEND_TOP:
        return CURSOR_RESIZE_TOP;
    case SSD_EXTEND_TOP_RIGHT:
        return CURSOR_RESIZE_TOP_RIGHT;
    case SSD_EXTEND_RIGHT:
        return CURSOR_RESIZE_RIGHT;
    case SSD_EXTEND_BOTTOM_RIGHT:
        return CURSOR_RESIZE_BOTTOM_RIGHT;
    case SSD_EXTEND_BOTTOM:
        return CURSOR_RESIZE_BOTTOM;
    case SSD_EXTEND_BOTTOM_LEFT:
        return CURSOR_RESIZE_BOTTOM_LEFT;
    case SSD_EXTEND_LEFT:
        return CURSOR_RESIZE_LEFT;
    default:
        return CURSOR_DEFAULT;
    }
}

static void ssd_part_update_theme_buffer(struct ssd_part *part, bool change);

static bool ssd_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                      uint32_t time, bool first, bool hold, void *data)
{
    struct ssd_part *part = data;

    /* we actually only need to process when first enter */
    if (!first || hold) {
        return false;
    }

    // kywc_log(KYWC_DEBUG, "ssd hover %s", ssd_part_name[part->type]);
    switch (part->type) {
    case SSD_BUTTON_MINIMIZE ... SSD_BUTTON_CLOSE:
        ssd_part_update_theme_buffer(part, true);
        cursor_set_image(seat->cursor, CURSOR_DEFAULT);
        break;
    case SSD_EXTEND_TOP_LEFT ... SSD_EXTEND_LEFT:
        if (view_is_resizable(view_from_kywc_view(part->ssd->kywc_view))) {
            cursor_set_image(seat->cursor, get_resize_name(part->type));
        }
        break;
    default:
        cursor_set_image(seat->cursor, CURSOR_DEFAULT);
        break;
    }
    return false;
}

static void ssd_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data)
{
    struct ssd_part *part = data;

    // kywc_log(KYWC_ERROR, "ssd leave %s", ssd_part_name[part->type]);
    switch (part->type) {
    case SSD_BUTTON_MINIMIZE ... SSD_BUTTON_CLOSE:
        ssd_part_update_theme_buffer(part, false);
        break;
    case SSD_EXTEND_TOP_LEFT ... SSD_EXTEND_LEFT:
        /* we have changed cursor image when hover */
        cursor_set_image(seat->cursor, CURSOR_DEFAULT);
        break;
    default:
        break;
    }
}

static void ssd_click(struct seat *seat, struct ky_scene_node *node, uint32_t button, bool pressed,
                      uint32_t time, bool dual, void *data)
{
    struct ssd_part *part = data;
    struct kywc_view *kywc_view = part->ssd->kywc_view;
    struct view *view = view_from_kywc_view(kywc_view);
    enum kywc_edges edges = KYWC_EDGE_NONE;

    if (dual) {
        if (button != BTN_LEFT) {
            return;
        }
        switch (part->type) {
        case SSD_CORNER_TOP_LEFT ... SSD_TITLE_RECT:
            if (view->base.maximizable) {
                kywc_view_toggle_maximized(kywc_view);
            }
            break;
        default:
            break;
        }
        return;
    }

    switch (part->type) {
    case SSD_BUTTON_CLOSE:
        if (LEFT_BUTTON_RELEASED(button, pressed)) {
            kywc_view_close(kywc_view);
        }
        break;
    case SSD_BUTTON_MAXIMIZE:
        if (LEFT_BUTTON_RELEASED(button, pressed)) {
            kywc_view_toggle_maximized(kywc_view);
        }
        break;
    case SSD_BUTTON_MINIMIZE:
        if (LEFT_BUTTON_RELEASED(button, pressed)) {
            kywc_view_set_minimized(kywc_view, true);
        }
        break;
    case SSD_CORNER_TOP_LEFT ... SSD_TITLE_RECT:
        if (LEFT_BUTTON_PRESSED(button, pressed)) {
            interactive_begin_move(view, seat);
        } else if (RIGHT_BUTTON_RELEASED(button, pressed)) {
            /* show window menu, menu will grab seat to hide itself */
            // TODO: menu
        }
        break;
    case SSD_EXTEND_TOP_LEFT:
        edges = KYWC_EDGE_TOP | KYWC_EDGE_LEFT;
        break;
    case SSD_EXTEND_TOP:
        edges = KYWC_EDGE_TOP;
        break;
    case SSD_EXTEND_TOP_RIGHT:
        edges = KYWC_EDGE_TOP | KYWC_EDGE_RIGHT;
        break;
    case SSD_EXTEND_RIGHT:
        edges = KYWC_EDGE_RIGHT;
        break;
    case SSD_EXTEND_BOTTOM_RIGHT:
        edges = KYWC_EDGE_BOTTOM | KYWC_EDGE_RIGHT;
        break;
    case SSD_EXTEND_BOTTOM:
        edges = KYWC_EDGE_BOTTOM;
        break;
    case SSD_EXTEND_BOTTOM_LEFT:
        edges = KYWC_EDGE_BOTTOM | KYWC_EDGE_LEFT;
        break;
    case SSD_EXTEND_LEFT:
        edges = KYWC_EDGE_LEFT;
        break;
    default:
        break;
    }

    if (edges != KYWC_EDGE_NONE && pressed && button == BTN_LEFT && view_is_resizable(view)) {
        interactive_begin_resize(view, edges, seat);
    }

    /* active current view */
    kywc_view_activate(kywc_view);
    seat_focus_surface(seat, view->surface);

    // TODO: rebase cursor
}

static struct ky_scene_node *ssd_get_root(void *data)
{
    struct ssd_part *part = data;
    return ky_scene_node_from_tree(part->ssd->tree);
}

static const struct input_event_node_impl ssd_impl = {
    .hover = ssd_hover,
    .leave = ssd_leave,
    .click = ssd_click,
};

static void ssd_part_set_theme_buffer(struct ssd_part *part, enum theme_buffer_type type)
{
    struct theme *theme = theme_manager_get_current();

    struct wlr_fbox src;
    struct wlr_buffer *buf = theme_buffer_load(theme, part->scale, type, &src);
    struct ky_scene_buffer *buffer = ky_scene_buffer_from_node(part->node);

    if (ky_scene_buffer_get_buffer(buffer) != buf) {
        ky_scene_buffer_set_buffer(buffer, buf);
    }

    int width, height;
    if (type > BUTTON_CLOSE) {
        painter_buffer_unscaled_size(buf, &width, &height);
    } else {
        width = height = theme->button_width;
    }
    ky_scene_buffer_set_dest_size(buffer, width, height);
    ky_scene_buffer_set_source_box(buffer, &src);
}

static void ssd_part_update_theme_buffer(struct ssd_part *part, bool change)
{
    switch (part->type) {
    case SSD_BUTTON_MINIMIZE:
        ssd_part_set_theme_buffer(part, change ? BUTTON_MINIMIZE_HOVER : BUTTON_MINIMIZE);
        break;
    case SSD_BUTTON_MAXIMIZE:;
        struct kywc_view *view = part->ssd->kywc_view;
        int index = change ? (view->maximized ? BUTTON_RESTORE_HOVER : BUTTON_MAXIMIZE_HOVER)
                           : (view->maximized ? BUTTON_RESTORE : BUTTON_MAXIMIZE);
        ssd_part_set_theme_buffer(part, index);
        break;
    case SSD_BUTTON_CLOSE:
        ssd_part_set_theme_buffer(part, change ? BUTTON_CLOSE_HOVER : BUTTON_CLOSE);
        break;
    case SSD_CORNER_TOP_LEFT:
        ssd_part_set_theme_buffer(part, change ? CORNER_TOP_LEFT_ACTIVE : CORNER_TOP_LEFT_INACTIVE);
        break;
    case SSD_CORNER_TOP_RIGHT:
        ssd_part_set_theme_buffer(part,
                                  change ? CORNER_TOP_RIGHT_ACTIVE : CORNER_TOP_RIGHT_INACTIVE);
        break;
    }
}

#define UPDATE_PART_POSITION(index, px, py)                                                        \
    ssd->parts[index].geo.x = px;                                                                  \
    ssd->parts[index].geo.y = py;                                                                  \
    ssd->parts[index].update |= SSD_UPDATE_POS

#define UPDATE_PART_SIZE(index, w, h)                                                              \
    ssd->parts[index].geo.width = w;                                                               \
    ssd->parts[index].geo.height = h;                                                              \
    ssd->parts[index].update |= SSD_UPDATE_SIZE

#define UPDATE_PART_COLOR(index, color)                                                            \
    ssd->parts[index].color = color;                                                               \
    ssd->parts[index].update |= SSD_UPDATE_COLOR

#define WRAP(w) (w > 0 ? w : 0)

static void redraw_title_text(struct ssd *ssd)
{
    struct theme *theme = theme_manager_get_current();
    struct kywc_view *view = ssd->kywc_view;
    struct ssd_part *part = &ssd->parts[SSD_TITLE_TEXT];

    /* no space left for title text */
    int text_width = view->geometry.width - 4 * theme->button_width;
    if (text_width <= 0) {
        return;
    }

    struct draw_info info = {
        .width = text_width,
        .height = theme->title_height,
        .scale = part->scale,
        .text = view->title ? view->title : "",
        .font = theme->font_name,
        .font_size = theme->font_size,
        .font_rgba = view->activated ? theme->active_text_color : theme->inactive_text_color,
        .auto_resize = true,
    };

    struct ky_scene_buffer *scene_buf;
    struct wlr_buffer *buffer, *old_buffer;
    /* static value is better, so we always apply new value */
    static int width, height;

    /* set_buffer -> output_enter -> redraw_title_text -> set_buffer */
    scene_buf = ky_scene_buffer_from_node(part->node);
    old_buffer = ky_scene_buffer_get_buffer(scene_buf);
    buffer = painter_draw_buffer(&info);
    /* get buffer size before set_buffer, as set_buffer recursion here */
    painter_buffer_unscaled_size(buffer, &width, &height);
    ky_scene_buffer_set_buffer(scene_buf, buffer);
    /* set_dest_size must after set_buffer, otherwise no damage added */
    ky_scene_buffer_set_dest_size(scene_buf, width, height);
    wlr_buffer_drop(old_buffer);
}

static void ssd_update_title(struct ssd *ssd, bool force)
{
    struct theme *theme = theme_manager_get_current();
    struct kywc_view *view = ssd->kywc_view;

    enum justification justify = theme->text_justify;
    int button_w = theme->button_width;
    int border_w = theme->border_width;
    int view_w = view->geometry.width;
    int title_h = theme->title_height;
    int max_width = view_w - 4 * button_w;
    int x, y, text_width, text_height;

    struct ky_scene_buffer *scene_buf = ky_scene_buffer_from_node(ssd->parts[SSD_TITLE_TEXT].node);

    /* if title not draw to buffer */
    bool redraw = force || !ky_scene_buffer_get_buffer(scene_buf);

    /* already have same title buffer */
    if (!redraw) {
        painter_buffer_unscaled_size(ky_scene_buffer_get_buffer(scene_buf), &text_width,
                                     &text_height);
        /* but is too bigger or cut off */
        redraw = ssd->title_is_cut || text_width > max_width;
    }

    /* redraw title buffer */
    if (redraw) {
        redraw_title_text(ssd);
    }

    /* redraw was failed */
    if (!ky_scene_buffer_get_buffer(scene_buf)) {
        return;
    }

    painter_buffer_unscaled_size(ky_scene_buffer_get_buffer(scene_buf), &text_width, &text_height);
    ssd->title_is_cut = text_width == max_width;

    y = border_w + (title_h - text_height) / 2;
    if (justify == JUSTIFY_LEFT) {
        x = button_w + border_w;
    } else if (justify == JUSTIFY_CENTER) {
        x = (border_w * 2 + view_w - text_width) / 2;
        /* add a left shift if close to button */
        if (text_width + 4 * button_w > max_width) {
            x -= button_w;
        }
    } else {
        x = border_w + button_w + max_width - text_width;
    }
    /* setting position directly is better */
    ky_scene_node_set_position(ssd->parts[SSD_TITLE_TEXT].node, x, y);
}

static void ssd_update_titlebar(struct ssd *ssd, uint32_t cause)
{
    struct theme *theme = theme_manager_get_current();
    struct kywc_view *view = ssd->kywc_view;

    int border_w = theme->border_width;
    int button_w = theme->button_width;
    int title_h = theme->title_height;
    int view_w = view->geometry.width;

    /* set titlebar subtree position if theme changed */
    if (cause & SSD_UPDATE_CAUSE_CREATE) {
        ky_scene_node_set_position(ky_scene_node_from_tree(ssd->titlebar_tree), -border_w,
                                   -(title_h + border_w));
    }

    /* only set button tree position when view w changed */
    if (cause & SSD_UPDATE_CAUSE_SIZE && ssd->view_width != view_w) {
        int pad = (title_h - button_w) / 2;
        int x = view_w + border_w - 3 * button_w - pad;
        int y = pad + border_w;
        ky_scene_node_set_position(ky_scene_node_from_tree(ssd->button_tree), x, y);
    }

    if (cause & SSD_UPDATE_CAUSE_CREATE) {
        UPDATE_PART_POSITION(SSD_BUTTON_MAXIMIZE, button_w, 0);
        UPDATE_PART_POSITION(SSD_BUTTON_CLOSE, 2 * button_w, 0);
        ssd_part_update_theme_buffer(&ssd->parts[SSD_BUTTON_MINIMIZE], false);
        ssd_part_update_theme_buffer(&ssd->parts[SSD_BUTTON_CLOSE], false);
    }

    if (cause & (SSD_UPDATE_CAUSE_TILE | SSD_UPDATE_CAUSE_MAXIMIZE | SSD_UPDATE_CAUSE_SIZE)) {
        bool tiled = view->tiled || view->maximized;
        ky_scene_node_set_enabled(ssd->parts[SSD_CORNER_TOP_LEFT].node, !tiled);
        ky_scene_node_set_enabled(ssd->parts[SSD_CORNER_TOP_RIGHT].node, !tiled);

        if (tiled) {
            UPDATE_PART_POSITION(SSD_TITLE_RECT, 0, border_w);
            UPDATE_PART_SIZE(SSD_TITLE_RECT, view_w + 2 * border_w, title_h);
        } else {
            UPDATE_PART_POSITION(SSD_TITLE_RECT, border_w + button_w, border_w);
            UPDATE_PART_SIZE(SSD_TITLE_RECT, WRAP(view_w - 2 * button_w), title_h);
            /* update corner buffer */
            UPDATE_PART_POSITION(SSD_CORNER_TOP_RIGHT, view_w + border_w - button_w, 0);
        }
    }

    if (cause & (SSD_UPDATE_CAUSE_TITLE | SSD_UPDATE_CAUSE_ACTIVATE | SSD_UPDATE_CAUSE_SIZE)) {
        /* don't force redraw when when resize only */
        if (cause == SSD_UPDATE_CAUSE_SIZE) {
            if (ssd->view_width != view_w) {
                ssd_update_title(ssd, false);
            }
        } else {
            ssd_update_title(ssd, true);
        }
    }

    if (cause & SSD_UPDATE_CAUSE_ACTIVATE) {
        float *color = view->activated ? theme->active_bg_color : theme->inactive_bg_color;
        UPDATE_PART_COLOR(SSD_TITLE_RECT, color);
        ssd_part_update_theme_buffer(&ssd->parts[SSD_CORNER_TOP_LEFT], view->activated);
        ssd_part_update_theme_buffer(&ssd->parts[SSD_CORNER_TOP_RIGHT], view->activated);
    }

    if (cause & SSD_UPDATE_CAUSE_MAXIMIZE) {
        ky_scene_node_set_enabled(ssd->parts[SSD_BUTTON_MAXIMIZE].node, view->maximizable);
        if (view->maximizable) {
            UPDATE_PART_POSITION(SSD_BUTTON_MINIMIZE, 0, 0);
            /* set maximize and restore  */
            ssd_part_update_theme_buffer(&ssd->parts[SSD_BUTTON_MAXIMIZE], false);
        } else {
            UPDATE_PART_POSITION(SSD_BUTTON_MINIMIZE, button_w, 0);
        }
    }
}

static void ssd_update_border(struct ssd *ssd, uint32_t cause)
{
    struct theme *theme = theme_manager_get_current();
    struct kywc_view *view = ssd->kywc_view;

    int border_w = theme->border_width;
    int button_w = theme->button_width;
    int title_h = theme->title_height;
    int view_w = view->geometry.width;
    int view_h = view->geometry.height;

    if (cause & SSD_UPDATE_CAUSE_CREATE) {
        UPDATE_PART_POSITION(SSD_BORDER_LEFT, -border_w, 0);
    }

    if (cause & (SSD_UPDATE_CAUSE_TILE | SSD_UPDATE_CAUSE_MAXIMIZE | SSD_UPDATE_CAUSE_SIZE)) {
        if (view->tiled || view->maximized) {
            UPDATE_PART_POSITION(SSD_BORDER_TOP, -border_w, -(title_h + border_w));
            UPDATE_PART_SIZE(SSD_BORDER_TOP, view_w + 2 * border_w, border_w);
        } else {
            UPDATE_PART_POSITION(SSD_BORDER_TOP, button_w, -(title_h + border_w));
            UPDATE_PART_SIZE(SSD_BORDER_TOP, WRAP(view_w - 2 * button_w), border_w);
        }
    }

    if (cause & SSD_UPDATE_CAUSE_SIZE) {
        if (ssd->view_width != view_w) {
            UPDATE_PART_POSITION(SSD_BORDER_RIGHT, view_w, 0);
            UPDATE_PART_SIZE(SSD_BORDER_BOTTOM, view_w + 2 * border_w, border_w);
        }
        if (ssd->view_height != view_h) {
            UPDATE_PART_POSITION(SSD_BORDER_BOTTOM, -border_w, view_h);
            UPDATE_PART_SIZE(SSD_BORDER_LEFT, border_w, view_h);
            UPDATE_PART_SIZE(SSD_BORDER_RIGHT, border_w, view_h);
        }
    }

    if (cause & SSD_UPDATE_CAUSE_ACTIVATE) {
        float *color = view->activated ? theme->active_border_color : theme->inactive_border_color;
        UPDATE_PART_COLOR(SSD_BORDER_TOP, color);
        UPDATE_PART_COLOR(SSD_BORDER_LEFT, color);
        UPDATE_PART_COLOR(SSD_BORDER_BOTTOM, color);
        UPDATE_PART_COLOR(SSD_BORDER_RIGHT, color);
    }
}

static void ssd_update_extend(struct ssd *ssd, uint32_t cause)
{
    struct theme *theme = theme_manager_get_current();
    struct kywc_view *view = ssd->kywc_view;

    int size = theme->resize_border + theme->corner_radius + theme->border_width;
    int w = view->geometry.width - 2 * theme->corner_radius;
    int h = view->geometry.height + theme->title_height - 2 * theme->corner_radius;
    int x1 = -(theme->resize_border + theme->border_width);
    int x2 = theme->corner_radius;
    int x3 = theme->corner_radius + w;
    int y1 = -(theme->title_height + theme->border_width + theme->resize_border);
    int y2 = -(theme->title_height - theme->corner_radius);
    int y3 = view->geometry.height - theme->corner_radius;

    if (cause & SSD_UPDATE_CAUSE_CREATE) {
        UPDATE_PART_POSITION(SSD_EXTEND_TOP_LEFT, x1, y1);
        UPDATE_PART_POSITION(SSD_EXTEND_TOP, x2, y1);
        UPDATE_PART_POSITION(SSD_EXTEND_LEFT, x1, y2);
        UPDATE_PART_SIZE(SSD_EXTEND_TOP_LEFT, size, size);
        UPDATE_PART_SIZE(SSD_EXTEND_TOP_RIGHT, size, size);
        UPDATE_PART_SIZE(SSD_EXTEND_BOTTOM_RIGHT, size, size);
        UPDATE_PART_SIZE(SSD_EXTEND_BOTTOM_LEFT, size, size);
    }

    if (cause & SSD_UPDATE_CAUSE_SIZE) {
        if (ssd->view_width != view->geometry.width) {
            UPDATE_PART_POSITION(SSD_EXTEND_TOP_RIGHT, x3, y1);
            UPDATE_PART_POSITION(SSD_EXTEND_RIGHT, x3, y2);
            UPDATE_PART_SIZE(SSD_EXTEND_TOP, WRAP(w), size);
            UPDATE_PART_SIZE(SSD_EXTEND_BOTTOM, WRAP(w), size);
        }
        if (ssd->view_height != view->geometry.height) {
            UPDATE_PART_POSITION(SSD_EXTEND_BOTTOM, x2, y3);
            UPDATE_PART_POSITION(SSD_EXTEND_BOTTOM_LEFT, x1, y3);
            UPDATE_PART_SIZE(SSD_EXTEND_RIGHT, size, WRAP(h));
            UPDATE_PART_SIZE(SSD_EXTEND_LEFT, size, WRAP(h));
        }
        UPDATE_PART_POSITION(SSD_EXTEND_BOTTOM_RIGHT, x3, y3);
    }

    if (cause & (SSD_UPDATE_CAUSE_TILE | SSD_UPDATE_CAUSE_MAXIMIZE)) {
        bool tiled = view->tiled || view->maximized;
        ky_scene_node_set_enabled(ky_scene_node_from_tree(ssd->extend_tree), !tiled);
    }
}

#undef UPDATE_PART_POSITON
#undef UPDATE_PART_SIZE
#undef WRAP

static void ssd_apply_parts(struct ssd *ssd)
{
    struct ssd_part *part;
    for (int i = 0; i < SSD_PART_COUNT; i++) {
        part = &ssd->parts[i];
        if (part->update & SSD_UPDATE_POS) {
            ky_scene_node_set_position(part->node, part->geo.x, part->geo.y);
            part->update &= ~SSD_UPDATE_POS;
        }
        if (part->update & SSD_UPDATE_SIZE) {
            struct ky_scene_rect *rect = ky_scene_rect_from_node(part->node);
            ky_scene_rect_set_size(rect, part->geo.width, part->geo.height);
            part->update &= ~SSD_UPDATE_SIZE;
        }
        if (part->update & SSD_UPDATE_COLOR) {
            struct ky_scene_rect *rect = ky_scene_rect_from_node(part->node);
            ky_scene_rect_set_color(rect, part->color);
            part->update &= ~SSD_UPDATE_COLOR;
        }
    }
}

static void ssd_update_margin(struct ssd *ssd)
{
    struct kywc_view *view = ssd->kywc_view;

    if (!view->need_ssd) {
        memset(&view->margin, 0, 4 * sizeof(int));
        return;
    }

    struct theme *theme = theme_manager_get_current();
    view->margin.off_x = theme->border_width;
    view->margin.off_y = theme->border_width + theme->title_height;
    view->margin.off_width = 2 * theme->border_width;
    view->margin.off_height = view->margin.off_width + theme->title_height;
}

static void ssd_update_parts(struct ssd *ssd, uint32_t cause)
{
    assert(ssd->created && ssd->kywc_view->need_ssd);

    if (cause & SSD_UPDATE_CAUSE_FULLSCREEN) {
        bool enabled = !ssd->kywc_view->fullscreen;
        ky_scene_node_set_enabled(ky_scene_node_from_tree(ssd->tree), enabled);
    }

    ssd_update_titlebar(ssd, cause);
    ssd_update_border(ssd, cause);
    ssd_update_extend(ssd, cause);

    /* apply all ssd parts changes */
    ssd_apply_parts(ssd);
}

static void ssd_update_buffer(struct ky_scene_buffer *buffer, float scale, void *data)
{
    struct ssd_part *part = data;

    part->scale = scale;
    /* update scene_buffer with new buffer */
    switch (part->type) {
    case SSD_BUTTON_MINIMIZE ... SSD_BUTTON_CLOSE:
        ssd_part_update_theme_buffer(part, false);
        break;
    case SSD_EXTEND_TOP_LEFT ... SSD_EXTEND_LEFT:
        ssd_part_update_theme_buffer(part, part->ssd->kywc_view->activated);
        break;
    case SSD_TITLE_TEXT:
        ssd_update_title(part->ssd, true);
        break;
    }
    kywc_log(KYWC_DEBUG, "%s redraw in %f", ssd_part_name[part->type], scale);
}

static void ssd_destroy_buffer(struct ky_scene_buffer *buffer, void *data)
{
    struct ssd_part *part = data;

    kywc_log(KYWC_DEBUG, "%s node destroy", ssd_part_name[part->type]);
    /* only destroy title text buffers, others are destroy in theme */
    if (part->type == SSD_TITLE_TEXT) {
        kywc_log(KYWC_DEBUG, "%s buffer destroy", ssd_part_name[part->type]);
        wlr_buffer_drop(ky_scene_buffer_get_buffer(buffer));
    }
}

static void ssd_create_parts(struct ssd *ssd, float scale)
{
    struct theme *theme = theme_manager_get_current();

    /* create buffers from bottom to top */
    for (int i = SSD_PART_COUNT - 1; i >= 0; i--) {
        ssd->parts[i].type = i;
        ssd->parts[i].ssd = ssd;

        struct ky_scene_tree *parent;
        if (i <= SSD_BUTTON_CLOSE) {
            parent = ssd->button_tree;
        } else if (i <= SSD_TITLE_RECT) {
            parent = ssd->titlebar_tree;
        } else if (i <= SSD_BORDER_LEFT) {
            parent = ssd->border_tree;
        } else {
            parent = ssd->extend_tree;
        }

        if (i < SSD_TITLE_RECT) {
            struct ky_scene_buffer *buf;
            buf = scaled_buffer_create(parent, scale, ssd_update_buffer, ssd_destroy_buffer,
                                       &ssd->parts[i]);
            ssd->parts[i].node = ky_scene_node_from_buffer(buf);
            ssd->parts[i].scale = scale;

            /* set_buffer will emit output_enter,
             * otherwise we cannot get initial output the view in.
             */
            ssd_part_update_theme_buffer(&ssd->parts[i], false);
        } else {
            float *color = (float[4]){ 0.f, 0.f, 0.f, 0.f };
            if (i == SSD_TITLE_RECT) {
                color =
                    ssd->kywc_view->activated ? theme->active_bg_color : theme->inactive_bg_color;
            } else if (i <= SSD_BORDER_LEFT) {
                color = ssd->kywc_view->activated ? theme->active_border_color
                                                  : theme->inactive_border_color;
            }
            struct ky_scene_rect *rect = ky_scene_rect_create(parent, 0, 0, color);
            ssd->parts[i].node = ky_scene_node_from_rect(rect);
        }

        input_event_node_create(ssd->parts[i].node, &ssd_impl, ssd_get_root, NULL, &ssd->parts[i]);
    }

    /* button is top of titlebar */
    ky_scene_node_raise_to_top(ky_scene_node_from_tree(ssd->button_tree));
}

static void handle_theme_update(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, theme_update);
    ssd_update_margin(ssd);
    ssd_update_parts(ssd, SSD_UPDATE_CAUSE_ALL);
}

static void handle_view_activate(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_activate);
    ssd_update_parts(ssd, SSD_UPDATE_CAUSE_ACTIVATE);
}

static void handle_view_size(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_size);
    ssd_update_parts(ssd, SSD_UPDATE_CAUSE_SIZE);

    ssd->view_width = ssd->kywc_view->geometry.width;
    ssd->view_height = ssd->kywc_view->geometry.height;
}

static void handle_view_tile(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_tile);
    ssd_update_parts(ssd, SSD_UPDATE_CAUSE_TILE);
}

static void handle_view_title(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_title);
    ssd_update_parts(ssd, SSD_UPDATE_CAUSE_TITLE);
}

static void handle_view_maximize(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_maximize);
    ssd_update_parts(ssd, SSD_UPDATE_CAUSE_MAXIMIZE);
}

static void handle_view_fullscreen(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_fullscreen);
    ssd_update_parts(ssd, SSD_UPDATE_CAUSE_FULLSCREEN);
}

static void ssd_parts_create(struct ssd *ssd)
{
    if (ssd->created) {
        return;
    }
    ssd->created = true;
    ssd->view_width = ssd->view_height = 0;

    struct kywc_view *kywc_view = ssd->kywc_view;
    ssd->view_activate.notify = handle_view_activate;
    wl_signal_add(&kywc_view->events.activate, &ssd->view_activate);
    ssd->view_size.notify = handle_view_size;
    wl_signal_add(&kywc_view->events.size, &ssd->view_size);
    ssd->view_tile.notify = handle_view_tile;
    wl_signal_add(&kywc_view->events.tile, &ssd->view_tile);
    ssd->view_title.notify = handle_view_title;
    wl_signal_add(&kywc_view->events.title, &ssd->view_title);
    ssd->view_maximize.notify = handle_view_maximize;
    wl_signal_add(&kywc_view->events.maximize, &ssd->view_maximize);
    ssd->view_fullscreen.notify = handle_view_fullscreen;
    wl_signal_add(&kywc_view->events.fullscreen, &ssd->view_fullscreen);
    ssd->theme_update.notify = handle_theme_update;
    theme_manager_add_update_listener(&ssd->theme_update);

    struct view *view = view_from_kywc_view(kywc_view);
    ssd->tree = ky_scene_tree_create(view->tree);
    ky_scene_node_lower_to_bottom(ky_scene_node_from_tree(ssd->tree));

    /* subtrees in ssd tree */
    ssd->extend_tree = ky_scene_tree_create(ssd->tree);
    ssd->border_tree = ky_scene_tree_create(ssd->tree);
    ssd->titlebar_tree = ky_scene_tree_create(ssd->tree);
    /* buttons is subtree of titlebar, only need to set tree pos */
    ssd->button_tree = ky_scene_tree_create(ssd->titlebar_tree);

    /* detect scale by view geometry.
     * it doesn't matter if setting to 1.0, scale will be set to best value
     * in output_enter listener.
     */
    ssd_create_parts(ssd, view->output->state.scale);
    ssd_update_parts(ssd, SSD_UPDATE_CAUSE_ALL);

    ssd->view_width = kywc_view->geometry.width;
    ssd->view_height = kywc_view->geometry.height;
}

static void ssd_parts_destroy(struct ssd *ssd)
{
    if (!ssd->created) {
        return;
    }
    ssd->created = false;

    wl_list_remove(&ssd->view_activate.link);
    wl_list_remove(&ssd->view_size.link);
    wl_list_remove(&ssd->view_tile.link);
    wl_list_remove(&ssd->view_title.link);
    wl_list_remove(&ssd->view_maximize.link);
    wl_list_remove(&ssd->view_fullscreen.link);
    wl_list_remove(&ssd->theme_update.link);

    // XXX: destroyed in view_destroy, check ssd->tree ?
    ky_scene_node_destroy(ky_scene_node_from_tree(ssd->tree));
}

static void handle_view_decoration(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_decoration);
    ssd_update_margin(ssd);
    /* view may not be mapped */
    if (!ssd->kywc_view->mapped) {
        return;
    }

    if (ssd->kywc_view->need_ssd) {
        ssd_parts_create(ssd);
    } else {
        ssd_parts_destroy(ssd);
    }
}

static void handle_view_map(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_map);
    /* skip if not need ssd */
    if (!ssd->kywc_view->need_ssd) {
        return;
    }
    ssd_parts_create(ssd);
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_unmap);
    ssd_parts_destroy(ssd);
}

static void handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct ssd *ssd = wl_container_of(listener, ssd, view_destroy);
    wl_list_remove(&ssd->view_destroy.link);
    wl_list_remove(&ssd->view_decoration.link);
    wl_list_remove(&ssd->view_map.link);
    wl_list_remove(&ssd->view_unmap.link);
    wl_list_remove(&ssd->link);
    free(ssd);
}

static void handle_new_view(struct wl_listener *listener, void *data)
{
    struct ssd_manager *manager = wl_container_of(listener, manager, new_view);
    struct kywc_view *kywc_view = data;

    struct ssd *ssd = calloc(1, sizeof(struct ssd));
    if (!ssd) {
        return;
    }

    ssd->kywc_view = kywc_view;
    wl_list_insert(&manager->ssds, &ssd->link);

    ssd->view_decoration.notify = handle_view_decoration;
    wl_signal_add(&kywc_view->events.decoration, &ssd->view_decoration);
    ssd->view_map.notify = handle_view_map;
    wl_signal_add(&kywc_view->events.map, &ssd->view_map);
    ssd->view_unmap.notify = handle_view_unmap;
    wl_signal_add(&kywc_view->events.unmap, &ssd->view_unmap);
    ssd->view_destroy.notify = handle_view_destroy;
    wl_signal_add(&kywc_view->events.destroy, &ssd->view_destroy);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct ssd_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    wl_list_remove(&manager->new_view.link);
    free(manager);
}

bool server_decoration_manager_create(struct view_manager *view_manager)
{
    struct ssd_manager *manager = calloc(1, sizeof(struct ssd_manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->ssds);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &manager->server_destroy);
    manager->new_view.notify = handle_new_view;
    kywc_view_add_new_listener(&manager->new_view);

    return true;
}
