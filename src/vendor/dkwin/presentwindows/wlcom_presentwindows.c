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
 * This is NOT a vendored file, but a compositor-native adapter of GXDE KWin's
 * Present Windows effect (ExposeAll, Win+A). Layout, decals and interaction
 * follow upstream's presentwindows.cpp which is kept verbatim in upstream/.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>
#include <wayland-util.h>

#include <kywc/binding.h>
#include <kywc/log.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#include "input/cursor.h"
#include "input/input.h"
#include "input/keyboard.h"
#include "input/seat.h"
#include "output.h"
#include "painter.h"
#include "scene/thumbnail.h"
#include "server.h"
#include "view/workspace.h"
#include "../../../view/view_p.h"

/* KWin presentwindows.cpp: m_fadeDuration = animationTime(150) */
#define FADE_DURATION 150
/* WindowMotionManager settles in about this long */
#define MOVE_DURATION 200
#define TICK_MS 10
/* calculateWindowTransformationsNatural: 20's and 10's margins */
#define LAYOUT_MARGIN 20
/* m_accuracy = PresentWindowsConfig::accuracy() * 20 */
#define LAYOUT_ACCURACY 20
#define OVERLAP_PAD 5
/* paintWindow: data.multiplyBrightness(interpolate(0.40, 1.0, highlight)) */
#define DIM_BRIGHTNESS 0.40f
/* desktop windows settle at highlight 0.3 -> brightness 0.58 -> dim 0.42 */
#define WALLPAPER_DIM 0.42f
/* panel windows are darkened to brightness 0.40 */
#define PANEL_DIM 0.60f
/* decal render: 0.9 * data.opacity() * m_decalOpacity */
#define DECAL_OPACITY 0.9f
/* the icon shader constant carries an extra *0.75 */
#define ICON_OPACITY_FACTOR 0.75f
#define ICON_SIZE 64
#define CAPTION_POINT_SIZE 12
/* Plasma.Button close button: units.iconSizes.medium, 10 px inset */
#define CLOSE_BUTTON_SIZE 32
#define CLOSE_BUTTON_INSET 10
#define MIN_ZOOM 1.05
#define MAX_SCALE_CAP 2.0
#define FILTER_BOX_CAP 1024

struct pv_box {
    double x, y, w, h;
};

struct present_item {
    struct present_view *pv;
    struct view *view;
    struct workspace *workspace;
    struct thumbnail *thumbnail;
    struct ky_scene_tree *tree;
    struct ky_scene_tree *close_tree;
    struct ky_scene_buffer *thumb;
    struct ky_scene_rect *dim;
    struct ky_scene_buffer *icon;
    struct ky_scene_buffer *caption;
    struct ky_scene_buffer *close_bg;
    struct ky_scene_buffer *close_glyph;
    int caption_w, caption_h;
    struct pv_box orig;    /* original geometry, output-local */
    struct pv_box target;  /* layout target, output-local */
    struct pv_box from;    /* animation start */
    struct pv_box to;      /* animation end */
    struct pv_box current; /* animation state */
    struct pv_box close_box;
    double opacity;
    double highlight;
    bool view_alive;
    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;
    struct wl_listener view_destroy;
};

struct restore_entry {
    struct view *view;
    bool was_visible;
    struct wl_list link;
};

struct pending_entry {
    struct view *view;
    struct wl_list link;
};

struct view_entry {
    struct view *view;
    struct workspace *workspace;
};

struct present_view {
    struct view_manager *view_manager;
    struct ky_scene_tree *tree;
    struct ky_scene_rect *backdrop;
    struct ky_scene_rect *panel_dim;
    struct ky_scene_tree *filter_tree;
    struct output *output;
    struct kywc_box area; /* usable area, output-local */
    struct kywc_box full; /* full output, output-local */
    struct present_item *items;
    size_t item_count;
    int highlighted;
    struct wl_list restore_list;
    struct wl_list pending_list;
    struct wl_event_source *timer;
    uint64_t last_ms;
    double elapsed_ms;
    double decal_opacity;
    double backdrop_alpha;
    double panel_alpha;
    char *filter;
    uint32_t last_key;
    bool key_released;
    bool enabled;
    bool closing;
    bool output_listening;
    struct seat_pointer_grab pointer_grab;
    struct seat_keyboard_grab keyboard_grab;
    struct seat_touch_grab touch_grab;
    struct wl_listener output_destroy;
    struct wl_listener server_destroy;
};

static struct present_view *pv;

static void destroy_contents(bool restore);
static void rebuild(struct view *exclude);
static void present_set_enabled(bool enabled);
static void update_filter_frame(void);

static double clamp01(double v)
{
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

static double smoothstep(double t)
{
    t = clamp01(t);
    return t * t * (3.0 - 2.0 * t);
}

static double approach(double current, double target, double step)
{
    if (current < target) {
        return fmin(target, current + step);
    }
    return fmax(target, current - step);
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool box_overlap(const struct pv_box *a, const struct pv_box *b, double pad)
{
    return a->x - pad < b->x + b->w + pad && b->x - pad < a->x + a->w + pad &&
           a->y - pad < b->y + b->h + pad && b->y - pad < a->y + a->h + pad;
}

static struct pv_box box_union(struct pv_box a, struct pv_box b)
{
    struct pv_box u = a;
    if (b.x < u.x) {
        u.w += u.x - b.x;
        u.x = b.x;
    }
    if (b.y < u.y) {
        u.h += u.y - b.y;
        u.y = b.y;
    }
    if (b.x + b.w > u.x + u.w) {
        u.w = b.x + b.w - u.x;
    }
    if (b.y + b.h > u.y + u.h) {
        u.h = b.y + b.h - u.y;
    }
    return u;
}

static bool point_in_box(double x, double y, const struct pv_box *box)
{
    return x >= box->x && y >= box->y && x < box->x + box->w && y < box->y + box->h;
}

static struct pv_box lerp_box(const struct pv_box *from, const struct pv_box *to, double t)
{
    struct pv_box b = {
        .x = from->x + (to->x - from->x) * t,
        .y = from->y + (to->y - from->y) * t,
        .w = from->w + (to->w - from->w) * t,
        .h = from->h + (to->h - from->h) * t,
    };
    return b;
}

static void cursor_local(double *x, double *y)
{
    struct seat *seat = input_manager_get_default_seat();
    *x = seat->cursor->lx - pv->output->geometry.x;
    *y = seat->cursor->ly - pv->output->geometry.y;
}

/* upstream elides captions with Qt::ElideMiddle, matching the window width */
static char *elide_caption_middle(const char *text, int max_width)
{
    int width, height;
    if (!text || !*text) {
        return NULL;
    }
    painter_get_text_size(text, NULL, CAPTION_POINT_SIZE, &width, &height);
    if (width <= max_width) {
        return g_strdup(text);
    }
    char *copy = g_strdup(text);
    if (!copy) {
        return NULL;
    }
    int n = g_utf8_strlen(copy, -1);
    /* largest prefix whose head + ellipsis still fits */
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        char *probe =
            g_strdup_printf("%.*s…", (int)(g_utf8_offset_to_pointer(copy, mid) - copy), copy);
        painter_get_text_size(probe, NULL, CAPTION_POINT_SIZE, &width, &height);
        g_free(probe);
        if (width <= max_width) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    if (lo == 0) {
        g_free(copy);
        return g_strdup("…");
    }
    /* largest suffix that still fits with this prefix */
    int lo2 = 0, hi2 = n - lo;
    while (lo2 < hi2) {
        int mid = (lo2 + hi2 + 1) / 2;
        char *probe = g_strdup_printf("%.*s…%s",
                                      (int)(g_utf8_offset_to_pointer(copy, lo) - copy), copy,
                                      g_utf8_offset_to_pointer(copy, n - mid));
        painter_get_text_size(probe, NULL, CAPTION_POINT_SIZE, &width, &height);
        g_free(probe);
        if (width <= max_width) {
            lo2 = mid;
        } else {
            hi2 = mid - 1;
        }
    }
    char *result = g_strdup_printf("%.*s…%s", (int)(g_utf8_offset_to_pointer(copy, lo) - copy),
                                   copy, g_utf8_offset_to_pointer(copy, n - lo2));
    g_free(copy);
    return result;
}

static bool view_matches_filter(struct view *view, const char *filter)
{
    if (!filter || !*filter) {
        return true;
    }
    char *title = g_utf8_casefold(view->base.title ? view->base.title : "", -1);
    char *app_id = g_utf8_casefold(view->base.app_id ? view->base.app_id : "", -1);
    char *needle = g_utf8_casefold(filter, -1);
    bool match = strstr(title, needle) || strstr(app_id, needle);
    g_free(title);
    g_free(app_id);
    g_free(needle);
    return match;
}

static bool is_pending(struct view *view)
{
    struct pending_entry *entry;
    wl_list_for_each(entry, &pv->pending_list, link) {
        if (entry->view == view) {
            return true;
        }
    }
    return false;
}

static void add_pending(struct view *view)
{
    if (is_pending(view)) {
        return;
    }
    struct pending_entry *entry = calloc(1, sizeof(*entry));
    if (entry) {
        entry->view = view;
        wl_list_insert(&pv->pending_list, &entry->link);
    }
}

static void remove_pending(struct view *view)
{
    struct pending_entry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, &pv->pending_list, link) {
        if (entry->view == view) {
            wl_list_remove(&entry->link);
            free(entry);
            return;
        }
    }
}

static void clear_pending(void)
{
    struct pending_entry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, &pv->pending_list, link) {
        wl_list_remove(&entry->link);
        free(entry);
    }
}

/*
 * ExposeAll shows the windows of every workspace, one grid per screen.
 * This adapter lays out the windows of the current output only, like KWin
 * does for a single screen.
 */
/* a view belongs to the effect when its geometry intersects the effect area;
 * comparing view->output fails when outputs overlap (clone/mirror) */
static bool view_intersects_area(const struct view *view, const struct kywc_box *area)
{
    const struct kywc_box *geo = &view->base.geometry;
    return geo->x < area->x + area->width && geo->x + geo->width > area->x &&
           geo->y < area->y + area->height && geo->y + geo->height > area->y;
}

static int collect_views(struct view_entry **entries_out, struct view *exclude)
{
    uint32_t workspace_count = workspace_manager_get_count();
    struct view_entry *entries = NULL;
    size_t n = 0, cap = 0;
    for (uint32_t i = 0; i < workspace_count; i++) {
        struct workspace *workspace = workspace_by_position(i);
        if (!workspace) {
            continue;
        }
        struct view_proxy *proxy;
        wl_list_for_each(proxy, &workspace->view_proxies, workspace_link) {
            struct view *view = proxy->view;
            /* exclude is dying: view_destroy emits its signal while the view
             * may still be mapped and linked, so it must not be re-collected */
            if (view == exclude || !view->base.mapped ||
                view->base.role != KYWC_VIEW_ROLE_NORMAL || view->base.skip_switcher ||
                !view_is_activatable(view) ||
                !view_intersects_area(view, &pv->output->geometry) ||
                is_pending(view) || !view_matches_filter(view, pv->filter)) {
                continue;
            }
            bool seen = false;
            for (size_t k = 0; k < n; k++) {
                if (entries[k].view == view) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                entries = realloc(entries, cap * sizeof(*entries));
                if (!entries) {
                    return 0;
                }
            }
            entries[n++] = (struct view_entry){ .view = view, .workspace = workspace };
        }
    }
    /* deterministic order like upstream's std::sort on the window list */
    for (size_t i = 1; i < n; i++) {
        struct view_entry e = entries[i];
        size_t j = i;
        while (j > 0) {
            struct view *a = entries[j - 1].view;
            struct view *b = e.view;
            if (a->base.geometry.y < b->base.geometry.y ||
                (a->base.geometry.y == b->base.geometry.y &&
                 a->base.geometry.x < b->base.geometry.x)) {
                break;
            }
            entries[j] = entries[j - 1];
            j--;
        }
        entries[j] = e;
    }
    *entries_out = entries;
    return (int)n;
}

/* upstream presentwindows.h: width / w->width() * w->height() */
static double height_for_width(const struct pv_box *orig, double width)
{
    if (orig->w <= 0) {
        return 0;
    }
    return width * orig->h / orig->w;
}

static bool layout_blocked(const struct pv_box *cand, size_t self, const struct pv_box *targets,
                           size_t n, const struct pv_box *inner)
{
    if (cand->x < inner->x || cand->y < inner->y ||
        cand->x + cand->w > inner->x + inner->w || cand->y + cand->h > inner->y + inner->h) {
        return true;
    }
    for (size_t i = 0; i < n; i++) {
        if (i != self && box_overlap(cand, &targets[i], OVERLAP_PAD)) {
            return true;
        }
    }
    return false;
}

/*
 * Port of PresentWindowsEffect::calculateWindowTransformationsNatural.
 * targets[] enters holding each window's original geometry (output-local)
 * and leaves holding the layout targets. area is the usable area, full the
 * full output geometry (both output-local).
 */
static void present_layout(struct pv_box *targets, size_t n, const struct kywc_box *area,
                           const struct kywc_box *full)
{
    struct pv_box *orig = calloc(n, sizeof(*orig));
    if (!orig) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        orig[i] = targets[i];
    }

    if (n == 1) {
        /* just move the window to its original location to save time */
        struct pv_box *only = &targets[0];
        if (only->x >= full->x && only->y >= full->y &&
            only->x + only->w <= full->x + full->width &&
            only->y + only->h <= full->y + full->height) {
            free(orig);
            return;
        }
    }

    struct pv_box bounds = { area->x, area->y, area->width, area->height };
    int *direction = calloc(n, sizeof(int));
    if (!direction) {
        free(orig);
        return;
    }
    int dir = 0;
    for (size_t i = 0; i < n; i++) {
        bounds = box_union(bounds, targets[i]);
        direction[i] = dir++;
        if (dir == 4) {
            dir = 0;
        }
    }

    /* iterate over all windows, if two overlap push them apart _slightly_ as
     * we try to brute-force the most optimal positions over many iterations */
    bool overlap;
    do {
        overlap = false;
        for (size_t w = 0; w < n; w++) {
            struct pv_box *target_w = &targets[w];
            for (size_t e = 0; e < n; e++) {
                if (w == e) {
                    continue;
                }
                struct pv_box *target_e = &targets[e];
                if (!box_overlap(target_w, target_e, OVERLAP_PAD)) {
                    continue;
                }
                overlap = true;

                /* determine pushing direction */
                double dx = target_e->x + target_e->w / 2 - (target_w->x + target_w->w / 2);
                double dy = target_e->y + target_e->h / 2 - (target_w->y + target_w->h / 2);
                if (dx == 0 && dy == 0) {
                    dx = 1;
                }
                /* approximate a vector of ~m_accuracy magnitude */
                double manhattan = fabs(dx) + fabs(dy);
                dx *= LAYOUT_ACCURACY / manhattan;
                dy *= LAYOUT_ACCURACY / manhattan;
                target_w->x -= dx;
                target_w->y -= dy;
                target_e->x += dx;
                target_e->y += dy;

                /* split the screen into nine sections, pull w towards the
                 * corner of its section; alternate corners on edge sections */
                int x_section = (int)((target_w->x - bounds.x) / (bounds.w / 3.0));
                int y_section = (int)((target_w->y - bounds.y) / (bounds.h / 3.0));
                if (x_section != 1 || y_section != 1) {
                    if (x_section == 1) {
                        x_section = direction[w] / 2 ? 2 : 0;
                    }
                    if (y_section == 1) {
                        y_section = direction[w] % 2 ? 2 : 0;
                    }
                }
                double px = 0, py = 0;
                double cx = target_w->x + target_w->w / 2;
                double cy = target_w->y + target_w->h / 2;
                if (x_section == 0 && y_section == 0) {
                    px = bounds.x - cx;
                    py = bounds.y - cy;
                } else if (x_section == 2 && y_section == 0) {
                    px = bounds.x + bounds.w - cx;
                    py = bounds.y - cy;
                } else if (x_section == 2 && y_section == 2) {
                    px = bounds.x + bounds.w - cx;
                    py = bounds.y + bounds.h - cy;
                } else if (x_section == 0 && y_section == 2) {
                    px = bounds.x - cx;
                    py = bounds.y + bounds.h - cy;
                }
                if (px != 0 || py != 0) {
                    manhattan = fabs(px) + fabs(py);
                    px *= LAYOUT_ACCURACY / manhattan;
                    py *= LAYOUT_ACCURACY / manhattan;
                    target_w->x += px;
                    target_w->y += py;
                }

                /* update the bounding rect */
                bounds = box_union(bounds, *target_w);
                bounds = box_union(bounds, *target_e);
            }
        }
    } while (overlap);
    free(direction);

    /* work out the scaling; the 20's and 10's are so the windows don't touch
     * the edge of the screen */
    double scale;
    if (bounds.x == area->x && bounds.y == area->y && bounds.w == area->width &&
        bounds.h == area->height) {
        scale = 1.0; /* don't add borders to the screen */
    } else if (area->width / bounds.w < area->height / bounds.h) {
        scale = (area->width - LAYOUT_MARGIN) / bounds.w;
    } else {
        scale = (area->height - LAYOUT_MARGIN) / bounds.h;
    }
    /* make the bounding rect fill the screen for the transform below */
    bounds = (struct pv_box){
        .x = (bounds.x * scale - (area->width - LAYOUT_MARGIN - bounds.w * scale) / 2 - 10.0) /
             scale,
        .y = (bounds.y * scale - (area->height - LAYOUT_MARGIN - bounds.h * scale) / 2 - 10.0) /
             scale,
        .w = area->width / scale,
        .h = area->height / scale,
    };
    /* move all windows back onto the screen and set their scale */
    for (size_t i = 0; i < n; i++) {
        targets[i] = (struct pv_box){
            .x = (targets[i].x - bounds.x) * scale + area->x,
            .y = (targets[i].y - bounds.y) * scale + area->y,
            .w = targets[i].w * scale,
            .h = targets[i].h * scale,
        };
    }

    /* try to fill the gaps by enlarging windows if they have the space */
    double border_inset = 10.0 / scale;
    struct pv_box inner = {
        .x = area->x + border_inset,
        .y = area->y + border_inset,
        .w = area->width - 2 * border_inset,
        .h = area->height - 2 * border_inset,
    };
    bool moved;
    do {
        moved = false;
        for (size_t w = 0; w < n; w++) {
            /* this may cause slight distortion if enlarged a large amount */
            double width_diff = LAYOUT_ACCURACY;
            double height_diff = height_for_width(&orig[w], targets[w].w + width_diff) -
                                 targets[w].h;
            double x_diff = width_diff / 2;  /* also move in the direction of the */
            double y_diff = height_diff / 2; /* enlarge, allows center windows to grow */
            /* height_diff (and y_diff) are re-computed after each successful
             * enlargement attempt to minimize the aspect ratio error */
            for (int attempt = 0; attempt < 4; attempt++) {
                struct pv_box cand = targets[w];
                switch (attempt) {
                case 0: /* top-right */
                    cand.x += x_diff;
                    cand.y -= y_diff + height_diff;
                    break;
                case 1: /* bottom-right */
                    cand.x += x_diff;
                    cand.y += y_diff;
                    break;
                case 2: /* bottom-left */
                    cand.x -= x_diff + width_diff;
                    cand.y += y_diff;
                    break;
                case 3: /* top-left */
                    cand.x -= x_diff + width_diff;
                    cand.y -= y_diff + height_diff;
                    break;
                }
                cand.w += width_diff;
                cand.h += height_diff;
                if (layout_blocked(&cand, w, targets, n, &inner)) {
                    continue;
                }
                targets[w] = cand;
                moved = true;
                height_diff = height_for_width(&orig[w], targets[w].w + width_diff) -
                              targets[w].h;
                y_diff = height_diff / 2;
            }
        }
    } while (moved);

    /* the expanding above can enlarge windows over 1.0/2.0 scale; that's not
     * wanted, but adding this to the loop above would never terminate */
    for (size_t w = 0; w < n; w++) {
        double s = targets[w].w / orig[w].w;
        if (s > MAX_SCALE_CAP || (s > 1.0 && (orig[w].w > 300 || orig[w].h > 300))) {
            s = (orig[w].w > 300 || orig[w].h > 300) ? 1.0 : MAX_SCALE_CAP;
            double cx = targets[w].x + targets[w].w / 2;
            double cy = targets[w].y + targets[w].h / 2;
            targets[w].w = orig[w].w * s;
            targets[w].h = orig[w].h * s;
            targets[w].x = cx - targets[w].w / 2;
            targets[w].y = cy - targets[w].h / 2;
        }
    }
    free(orig);
}

/* paintWindow: scale the highlighted window to at least 105% or to cover
 * 1/16 of the screen size - yet keep it in screen bounds */
static double zoom_cap_for(struct present_item *item)
{
    double eff_w = item->current.w;
    double eff_h = item->current.h;
    if (eff_w < 1 || eff_h < 1) {
        return 1.0;
    }
    double xr = pv->full.width / eff_w;
    double yr = pv->full.height / eff_h;
    double t_scale = xr < yr ? fmax(xr / 4.0, yr / 32.0) : fmax(xr / 32.0, yr / 4.0);
    if (t_scale < MIN_ZOOM) {
        t_scale = MIN_ZOOM;
    }
    if (eff_w * t_scale > pv->full.width) {
        t_scale = pv->full.width / eff_w;
    }
    if (eff_h * t_scale > pv->full.height) {
        t_scale = pv->full.height / eff_h;
    }
    return t_scale;
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data);
static void handle_thumbnail_destroy(struct wl_listener *listener, void *data);
static void handle_item_view_destroy(struct wl_listener *listener, void *data);
static void handle_output_destroy(struct wl_listener *listener, void *data);

static void destroy_item(struct present_item *item)
{
    if (item->view_alive) {
        wl_list_remove(&item->view_destroy.link);
    }
    if (item->thumbnail) {
        thumbnail_destroy(item->thumbnail);
        item->thumbnail = NULL;
    }
    ky_scene_node_destroy(&item->tree->node);
    free(item);
}

static void add_restore_entry(struct view *view)
{
    struct restore_entry *entry;
    wl_list_for_each(entry, &pv->restore_list, link) {
        if (entry->view == view) {
            return;
        }
    }
    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return;
    }
    entry->view = view;
    entry->was_visible = view->tree->node.enabled;
    wl_list_insert(&pv->restore_list, &entry->link);
}

static void remove_restore_entry(struct view *view)
{
    struct restore_entry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, &pv->restore_list, link) {
        if (entry->view == view) {
            wl_list_remove(&entry->link);
            free(entry);
            return;
        }
    }
}

static bool create_item(struct present_item *item, struct view *view, struct workspace *workspace,
                        const struct pv_box *orig, const struct pv_box *target)
{
    memset(item, 0, sizeof(*item));
    item->pv = pv;
    item->view = view;
    item->workspace = workspace;
    item->orig = *orig;
    item->target = *target;
    item->from = *orig;
    item->to = *target;
    item->current = *orig;
    item->highlight = 1.0;
    item->opacity = (view->base.minimized || workspace != workspace_manager_get_current()) ? 0.0
                                                                                           : 1.0;
    item->tree = ky_scene_tree_create(pv->tree);
    if (!item->tree) {
        return false;
    }
    ky_scene_node_set_position(&item->tree->node, (int)lround(orig->x), (int)lround(orig->y));

    item->thumb = ky_scene_buffer_create(item->tree, NULL);
    ky_scene_buffer_set_dest_size(item->thumb, (int)lround(orig->w), (int)lround(orig->h));
    item->dim = ky_scene_rect_create(item->tree, (int)lround(orig->w), (int)lround(orig->h),
                                     (float[4]){ 0.0f, 0.0f, 0.0f, 0.0f });

    /* centered icon decal, 64 px like upstream's iconFrame */
    float scale = pv->output->base.state.scale;
    struct wlr_buffer *icon_buffer = view_get_icon_buffer_by_size(view, ICON_SIZE, scale);
    item->icon = ky_scene_buffer_create(item->tree, icon_buffer);
    if (icon_buffer) {
        wlr_buffer_drop(icon_buffer);
    }
    ky_scene_buffer_set_dest_size(item->icon, ICON_SIZE, ICON_SIZE);

    /* caption decal below the icon, white 12 pt, elided to the window width */
    char *caption =
        elide_caption_middle(view->base.title ? view->base.title : "", (int)lround(target->w));
    item->caption = ky_scene_buffer_create(item->tree, NULL);
    if (caption) {
        struct draw_info info = {
            .width = 4096,
            .height = 4096,
            .scale = 2.0f,
            .text = caption,
            .font_size = CAPTION_POINT_SIZE,
            .font_rgba = (float[4]){ 1.0f, 1.0f, 1.0f, 1.0f },
            .align = TEXT_ALIGN_CENTER,
            .auto_resize = AUTO_RESIZE_ONLY,
        };
        struct wlr_buffer *caption_buffer = painter_draw_buffer(&info);
        if (caption_buffer) {
            ky_scene_buffer_set_buffer(item->caption, caption_buffer);
            painter_buffer_get_dest_size(caption_buffer, &item->caption_w, &item->caption_h);
            ky_scene_buffer_set_dest_size(item->caption, item->caption_w, item->caption_h);
            wlr_buffer_drop(caption_buffer);
        }
        g_free(caption);
    }

    /* close button: Plasma-style rounded square with a window-close glyph */
    item->close_tree = ky_scene_tree_create(item->tree);
    ky_scene_node_set_enabled(&item->close_tree->node, false);
    struct draw_info bg_info = {
        .width = CLOSE_BUTTON_SIZE,
        .height = CLOSE_BUTTON_SIZE,
        .scale = 2.0f,
        .solid_rgba = (float[4]){ 0.15f, 0.16f, 0.18f, 0.90f },
        .border_rgba = (float[4]){ 0.75f, 0.76f, 0.78f, 0.35f },
        .border_width = 1,
        .border_mask = BORDER_MASK_ALL,
        .corner_mask = CORNER_MASK_ALL,
        .corner_radius = 6,
    };
    struct wlr_buffer *bg_buffer = painter_draw_buffer(&bg_info);
    item->close_bg = ky_scene_buffer_create(item->close_tree, bg_buffer);
    ky_scene_buffer_set_dest_size(item->close_bg, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE);
    if (bg_buffer) {
        wlr_buffer_drop(bg_buffer);
    }
    struct draw_info glyph_info = {
        .width = 4096,
        .height = 4096,
        .scale = 2.0f,
        .text = "✕",
        .font_size = 15,
        .font_rgba = (float[4]){ 1.0f, 1.0f, 1.0f, 1.0f },
        .align = TEXT_ALIGN_CENTER,
        .auto_resize = AUTO_RESIZE_ONLY,
    };
    struct wlr_buffer *glyph_buffer = painter_draw_buffer(&glyph_info);
    item->close_glyph = ky_scene_buffer_create(item->close_tree, glyph_buffer);
    if (glyph_buffer) {
        int glyph_w = 0, glyph_h = 0;
        painter_buffer_get_dest_size(glyph_buffer, &glyph_w, &glyph_h);
        ky_scene_buffer_set_dest_size(item->close_glyph, glyph_w, glyph_h);
        ky_scene_node_set_position(&item->close_glyph->node, (CLOSE_BUTTON_SIZE - glyph_w) / 2,
                                   (CLOSE_BUTTON_SIZE - glyph_h) / 2);
        wlr_buffer_drop(glyph_buffer);
    }

    /* the real window tree is replaced by the live thumbnail while shown */
    if (view->tree->node.enabled) {
        ky_scene_node_set_enabled(&view->tree->node, false);
    }
    item->view_alive = true;
    item->view_destroy.notify = handle_item_view_destroy;
    wl_signal_add(&view->base.events.destroy, &item->view_destroy);
    item->thumbnail = thumbnail_create_from_view(view, THUMBNAIL_DISABLE_SHADOW, 1.0f);
    if (!item->thumbnail) {
        wl_list_remove(&item->view_destroy.link);
        ky_scene_node_destroy(&item->tree->node);
        free(item);
        return false;
    }
    item->thumbnail_update.notify = handle_thumbnail_update;
    thumbnail_add_update_listener(item->thumbnail, &item->thumbnail_update);
    item->thumbnail_destroy.notify = handle_thumbnail_destroy;
    thumbnail_add_destroy_listener(item->thumbnail, &item->thumbnail_destroy);
    thumbnail_mark_wants_update(item->thumbnail, true);
    thumbnail_update(item->thumbnail);
    return true;
}

static int index_of_view(struct view *view)
{
    for (size_t i = 0; i < pv->item_count; i++) {
        if (pv->items[i].view == view) {
            return (int)i;
        }
    }
    return -1;
}

static bool create_items(struct view *exclude)
{
    struct view_entry *entries = NULL;
    int n = collect_views(&entries, exclude);
    if (n <= 0) {
        free(entries);
        return false;
    }

    struct pv_box *orig = calloc(n, sizeof(*orig));
    struct pv_box *targets = calloc(n, sizeof(*targets));
    pv->items = calloc(n, sizeof(*pv->items));
    if (!orig || !targets || !pv->items) {
        free(orig);
        free(targets);
        free(entries);
        return false;
    }
    for (int i = 0; i < n; i++) {
        struct kywc_box *geometry = &entries[i].view->base.geometry;
        orig[i] = (struct pv_box){
            .x = geometry->x - pv->output->geometry.x,
            .y = geometry->y - pv->output->geometry.y,
            .w = geometry->width,
            .h = geometry->height,
        };
        targets[i] = orig[i];
        add_restore_entry(entries[i].view);
    }
    present_layout(targets, n, &pv->area, &pv->full);

    size_t created = 0;
    for (int i = 0; i < n; i++) {
        if (create_item(&pv->items[created], entries[i].view, entries[i].workspace, &orig[i],
                        &targets[i])) {
            created++;
        }
    }
    free(orig);
    free(targets);
    free(entries);
    pv->item_count = created;
    if (!created) {
        free(pv->items);
        pv->items = NULL;
        return false;
    }
    return true;
}

static void destroy_contents(bool restore)
{
    for (size_t i = 0; i < pv->item_count; i++) {
        destroy_item(&pv->items[i]);
    }
    free(pv->items);
    pv->items = NULL;
    pv->item_count = 0;
    pv->highlighted = -1;
    if (pv->filter_tree) {
        ky_scene_node_destroy(&pv->filter_tree->node);
        pv->filter_tree = NULL;
    }
    if (restore) {
        struct restore_entry *entry, *tmp;
        wl_list_for_each_safe(entry, tmp, &pv->restore_list, link) {
            /* windows closed via the effect are on their way out: re-showing
             * their tree would flicker it back before the destroy lands */
            if (!is_pending(entry->view)) {
                ky_scene_node_set_enabled(&entry->view->tree->node, entry->was_visible);
            }
            wl_list_remove(&entry->link);
            free(entry);
        }
    }
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct present_item *item = wl_container_of(listener, item, thumbnail_update);
    struct thumbnail_update_event *event = data;
    if (event->buffer_changed && item->thumb->buffer != event->buffer) {
        ky_scene_buffer_set_buffer(item->thumb, event->buffer);
    } else {
        ky_scene_node_push_damage(&item->thumb->node, KY_SCENE_DAMAGE_HARMLESS, NULL);
    }
}

static void handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct present_item *item = wl_container_of(listener, item, thumbnail_destroy);
    wl_list_remove(&item->thumbnail_update.link);
    wl_list_remove(&item->thumbnail_destroy.link);
    item->thumbnail = NULL;
}

static void handle_item_view_destroy(struct wl_listener *listener, void *data)
{
    struct present_item *item = wl_container_of(listener, item, view_destroy);
    item->view_alive = false;
    remove_restore_entry(item->view);
    if (is_pending(item->view)) {
        /* closed via the effect: the grid already excludes it */
        remove_pending(item->view);
    } else if (pv->enabled && !pv->closing) {
        /* closed externally: re-flow the grid without the dying window */
        rebuild(item->view);
    }
}

/* the styled filter frame: "Filter:" plus the typed text, 20 pt, at the top
 * center of the screen (upstream updateFilterFrame) */
static void update_filter_frame(void)
{
    if (pv->filter_tree) {
        ky_scene_node_destroy(&pv->filter_tree->node);
        pv->filter_tree = NULL;
    }
    if (!pv->filter || !*pv->filter) {
        return;
    }
    char *text = g_strdup_printf("Filter:\n%s", pv->filter);
    struct draw_info info = {
        .width = 4096,
        .height = 4096,
        .scale = 2.0f,
        .text = text,
        .font_size = 20,
        .font_rgba = (float[4]){ 1.0f, 1.0f, 1.0f, 1.0f },
        .align = TEXT_ALIGN_LEFT,
        .auto_resize = AUTO_RESIZE_EXTEND,
    };
    struct wlr_buffer *buffer = painter_draw_buffer(&info);
    g_free(text);
    if (!buffer) {
        return;
    }
    int w = 0, h = 0;
    painter_buffer_get_dest_size(buffer, &w, &h);
    if (w <= 0 || h <= 0) {
        wlr_buffer_drop(buffer);
        return;
    }
    pv->filter_tree = ky_scene_tree_create(pv->tree);
    ky_scene_node_set_position(&pv->filter_tree->node, pv->area.x + pv->area.width / 2,
                               pv->area.y + pv->area.height / 10);
    struct draw_info bg_info = {
        .width = w,
        .height = h,
        .scale = 2.0f,
        .solid_rgba = (float[4]){ 0.10f, 0.10f, 0.10f, 0.85f },
        .border_rgba = (float[4]){ 0.8f, 0.8f, 0.8f, 0.25f },
        .border_width = 1,
        .border_mask = BORDER_MASK_ALL,
        .corner_mask = CORNER_MASK_ALL,
        .corner_radius = 8,
    };
    struct wlr_buffer *bg_buffer = painter_draw_buffer(&bg_info);
    struct ky_scene_buffer *bg = ky_scene_buffer_create(pv->filter_tree, bg_buffer);
    ky_scene_buffer_set_dest_size(bg, w, h);
    if (bg_buffer) {
        wlr_buffer_drop(bg_buffer);
    }
    struct ky_scene_buffer *frame_text = ky_scene_buffer_create(pv->filter_tree, buffer);
    ky_scene_buffer_set_dest_size(frame_text, w, h);
    wlr_buffer_drop(buffer);
}

static bool show_present(void)
{
    struct seat *seat = input_manager_get_default_seat();
    pv->output = input_current_output(seat);
    if (!pv->output) {
        return false;
    }

    struct kywc_box *geometry = &pv->output->geometry;
    struct kywc_box *usable = &pv->output->usable_area;
    pv->area = (struct kywc_box){
        .x = usable->x - geometry->x,
        .y = usable->y - geometry->y,
        .width = usable->width,
        .height = usable->height,
    };
    pv->full = (struct kywc_box){ 0, 0, geometry->width, geometry->height };
    ky_scene_node_set_position(&pv->tree->node, geometry->x, geometry->y);
    ky_scene_rect_set_size(pv->backdrop, pv->area.width, pv->area.height);
    ky_scene_node_set_position(&pv->backdrop->node, pv->area.x, pv->area.y);

    /* dim the panel strips outside the usable area, like upstream darkens
     * dock windows with multiplyBrightness(interpolate(0.40, 1.0, highlight)) */
    struct kywc_box strips[4];
    int strip_count = 0;
    if (pv->area.y > 0) {
        strips[strip_count++] = (struct kywc_box){ 0, 0, geometry->width, pv->area.y };
    }
    if (pv->area.y + pv->area.height < geometry->height) {
        strips[strip_count++] = (struct kywc_box){
            0, pv->area.y + pv->area.height, geometry->width,
            geometry->height - pv->area.y - pv->area.height
        };
    }
    if (pv->area.x > 0) {
        strips[strip_count++] = (struct kywc_box){ 0, 0, pv->area.x, geometry->height };
    }
    if (pv->area.x + pv->area.width < geometry->width) {
        strips[strip_count++] = (struct kywc_box){
            pv->area.x + pv->area.width, 0, geometry->width - pv->area.x - pv->area.width,
            geometry->height
        };
    }
    if (strip_count > 0) {
        struct kywc_box panel = strips[0];
        for (int i = 1; i < strip_count; i++) {
            struct pv_box u =
                box_union((struct pv_box){ panel.x, panel.y, panel.width, panel.height },
                          (struct pv_box){ strips[i].x, strips[i].y, strips[i].width,
                                           strips[i].height });
            panel = (struct kywc_box){ (int)u.x, (int)u.y, (int)u.w, (int)u.h };
        }
        ky_scene_rect_set_size(pv->panel_dim, panel.width, panel.height);
        ky_scene_node_set_position(&pv->panel_dim->node, panel.x, panel.y);
        ky_scene_node_set_enabled(&pv->panel_dim->node, true);
    } else {
        ky_scene_node_set_enabled(&pv->panel_dim->node, false);
    }

    if (!create_items(NULL)) {
        destroy_contents(true);
        return false;
    }
    update_filter_frame();

    /* like upstream's m_needInitialSelection: highlight the window under the
     * cursor, if any, otherwise nothing is highlighted */
    pv->highlighted = -1;
    double lx, ly;
    cursor_local(&lx, &ly);
    for (size_t i = 0; i < pv->item_count; i++) {
        if (point_in_box(lx, ly, &pv->items[i].current)) {
            pv->highlighted = (int)i;
            break;
        }
    }

    pv->elapsed_ms = 0;
    pv->decal_opacity = 0.0;
    pv->backdrop_alpha = 0.0;
    pv->panel_alpha = 0.0;
    ky_scene_node_set_enabled(&pv->tree->node, true);
    view_manager_show_desktop(false, true);

    pv->output_destroy.notify = handle_output_destroy;
    wl_signal_add(&pv->output->base.events.destroy, &pv->output_destroy);
    pv->output_listening = true;
    return true;
}

static void teardown(void)
{
    wl_event_source_timer_update(pv->timer, 0);
    struct seat *seat = input_manager_get_default_seat();
    /* grabs stay active through the closing animation so input remains
     * swallowed, like upstream keeps the effect activated until it ends;
     * seat_end_* is a no-op when the grab is not the active one */
    seat_end_pointer_grab(seat, &pv->pointer_grab);
    seat_end_keyboard_grab(seat, &pv->keyboard_grab);
    seat_end_touch_grab(seat, &pv->touch_grab);
    cursor_lock_image(seat->cursor, false);
    cursor_rebase(seat->cursor);
    ky_scene_node_set_enabled(&pv->tree->node, false);
    destroy_contents(true);
    clear_pending();
    /* like upstream: the filter does not survive deactivation */
    free(pv->filter);
    pv->filter = NULL;
    if (pv->output_listening) {
        wl_list_remove(&pv->output_destroy.link);
        pv->output_listening = false;
    }
    pv->output = NULL;
    pv->enabled = false;
    pv->closing = false;
}

static void present_set_enabled(bool enabled)
{
    if (!pv || pv->enabled == enabled) {
        return;
    }
    struct seat *seat = input_manager_get_default_seat();
    if (enabled) {
        if (pv->closing) {
            /* re-opened while closing: animate back from the current state */
            pv->closing = false;
            pv->elapsed_ms = 0;
            for (size_t i = 0; i < pv->item_count; i++) {
                pv->items[i].from = pv->items[i].current;
                pv->items[i].to = pv->items[i].target;
            }
            output_schedule_frame(pv->output->wlr_output);
            return;
        }
        if (!show_present()) {
            return;
        }
        pv->enabled = true;
        pv->closing = false;
        cursor_set_image(seat->cursor, CURSOR_DEFAULT);
        cursor_lock_image(seat->cursor, true);
        seat_start_pointer_grab(seat, &pv->pointer_grab);
        seat_start_keyboard_grab(seat, &pv->keyboard_grab);
        seat_start_touch_grab(seat, &pv->touch_grab);
        pv->last_ms = 0;
        wl_event_source_timer_update(pv->timer, TICK_MS);
    } else {
        pv->enabled = false;
        pv->closing = true;
        pv->elapsed_ms = 0;
        for (size_t i = 0; i < pv->item_count; i++) {
            struct present_item *item = &pv->items[i];
            item->from = item->current;
            item->to = item->orig;
        }
        output_schedule_frame(pv->output->wlr_output);
    }
}

bool present_windows_toggle(void)
{
    if (!pv) {
        return false;
    }
    present_set_enabled(!pv->enabled);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Animation                                                                */

static void apply_item_visuals(struct present_item *item)
{
    struct pv_box *current = &item->current;
    ky_scene_node_set_position(&item->tree->node, (int)lround(current->x),
                               (int)lround(current->y));

    /* port of the paintWindow zoom: interpolate(1.0, tScale, highlight),
     * clamped into the screen like upstream's tRect adjustment */
    double zoomed_w = current->w;
    double zoomed_h = current->h;
    double tx = current->x;
    double ty = current->y;
    if (!pv->closing && item->highlight > 0.001) {
        double t_scale = zoom_cap_for(item);
        double growth_w = (t_scale - 1.0) * current->w * item->highlight;
        double growth_h = (t_scale - 1.0) * current->h * item->highlight;
        zoomed_w = current->w + growth_w;
        zoomed_h = current->h + growth_h;
        tx = current->x - growth_w / 2;
        ty = current->y - growth_h / 2;
        double clamped_x =
            fmax(tx, pv->full.x) + fmin(0.0, pv->full.x + pv->full.width - (tx + zoomed_w));
        double clamped_y =
            fmax(ty, pv->full.y) + fmin(0.0, pv->full.y + pv->full.height - (ty + zoomed_h));
        tx += (clamped_x - tx) * item->highlight;
        ty += (clamped_y - ty) * item->highlight;
    }
    int offset_x = (int)lround(tx - current->x);
    int offset_y = (int)lround(ty - current->y);
    ky_scene_node_set_position(&item->thumb->node, offset_x, offset_y);
    ky_scene_buffer_set_dest_size(item->thumb, (int)lround(zoomed_w), (int)lround(zoomed_h));
    ky_scene_node_set_position(&item->dim->node, offset_x, offset_y);
    ky_scene_rect_set_size(item->dim, (int)lround(zoomed_w), (int)lround(zoomed_h));
    /* multiplyBrightness(interpolate(0.40, 1.0, highlight)): a dim rect above
     * the thumbnail, alpha 0.6 * (1 - highlight) */
    ky_scene_rect_set_color(item->dim,
                            (float[4]){ 0.0f, 0.0f, 0.0f,
                                        (float)((1.0 - DIM_BRIGHTNESS) * (1.0 - item->highlight)) });

    /* decals: 0.9 * opacity * decalOpacity, icon carries an extra *0.75 */
    double decal = DECAL_OPACITY * item->opacity * pv->decal_opacity;
    ky_scene_buffer_set_opacity(item->icon, (float)(decal * ICON_OPACITY_FACTOR));
    ky_scene_buffer_set_opacity(item->caption, (float)decal);
    ky_scene_node_set_position(&item->icon->node, (int)lround(current->w / 2) - ICON_SIZE / 2,
                               (int)lround(current->h / 2) - ICON_SIZE / 2);
    ky_scene_node_set_position(&item->caption->node,
                               (int)lround(current->w / 2) - item->caption_w / 2,
                               (int)lround(current->h / 2) + ICON_SIZE - item->caption_h / 2);

    /* the close button follows the highlighted window's zoomed rect, shown
     * only when the cursor is inside its (unzoomed) target geometry and the
     * window is big enough, like upstream's updateCloseWindow */
    item->close_box = (struct pv_box){ tx, ty, zoomed_w, zoomed_h };
    bool show_close = false;
    if (!pv->closing && pv->highlighted >= 0 && item == &pv->items[pv->highlighted]) {
        double lx, ly;
        cursor_local(&lx, &ly);
        bool tiny = 2 * CLOSE_BUTTON_SIZE > item->target.w &&
                    2 * CLOSE_BUTTON_SIZE > item->target.h;
        show_close = !tiny && point_in_box(lx, ly, &item->target);
    }
    ky_scene_node_set_enabled(&item->close_tree->node, show_close);
    /* TopRight corner, 10 px inset (GXDE decorations close on the right) */
    ky_scene_node_set_position(&item->close_tree->node,
                               offset_x + (int)lround(zoomed_w) - CLOSE_BUTTON_SIZE -
                                   CLOSE_BUTTON_INSET,
                               offset_y + CLOSE_BUTTON_INSET);
}

static int tick(void *data)
{
    struct present_view *p = data;
    if (!p->enabled && !p->closing) {
        return 0;
    }
    uint64_t now = now_ms();
    double dt = p->last_ms ? (double)(now - p->last_ms) : TICK_MS;
    p->last_ms = now;
    if (dt > 100) {
        dt = 100;
    }
    p->elapsed_ms += dt;

    double ease = smoothstep(p->elapsed_ms / MOVE_DURATION);
    double global = p->closing ? 0.0 : 1.0;
    p->decal_opacity = approach(p->decal_opacity, global, dt / FADE_DURATION);
    p->backdrop_alpha = approach(p->backdrop_alpha, global * WALLPAPER_DIM, dt / FADE_DURATION);
    p->panel_alpha = approach(p->panel_alpha, global * PANEL_DIM, dt / FADE_DURATION);
    ky_scene_rect_set_color(p->backdrop, (float[4]){ 0.0f, 0.0f, 0.0f, (float)p->backdrop_alpha });
    ky_scene_rect_set_color(p->panel_dim, (float[4]){ 0.0f, 0.0f, 0.0f, (float)p->panel_alpha });

    for (size_t i = 0; i < p->item_count; i++) {
        struct present_item *item = &p->items[i];
        item->current = lerp_box(&item->from, &item->to, ease);
        item->opacity = approach(item->opacity, 1.0, dt / FADE_DURATION);
        double highlight_target = (!p->closing && (int)i == p->highlighted) ? 1.0 : 0.0;
        item->highlight = approach(item->highlight, highlight_target, dt / FADE_DURATION);
        apply_item_visuals(item);
    }

    if (p->closing && p->elapsed_ms >= MOVE_DURATION + TICK_MS) {
        teardown();
        return 0;
    }
    output_schedule_frame(p->output->wlr_output);
    wl_event_source_timer_update(p->timer, TICK_MS);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Input                                                                     */

static void update_hover(void)
{
    if (pv->filter && *pv->filter) {
        /* hovering does not change the highlight while filtering */
        return;
    }
    double lx, ly;
    cursor_local(&lx, &ly);
    int index = -1;
    for (size_t i = 0; i < pv->item_count; i++) {
        if (point_in_box(lx, ly, &pv->items[i].current)) {
            index = (int)i;
            break;
        }
    }
    /* like upstream: hovering a window highlights it, leaving all windows
     * removes the highlight */
    if (index != pv->highlighted) {
        pv->highlighted = index;
        output_schedule_frame(pv->output->wlr_output);
    }
}

static void activate_item(int index)
{
    if (index < 0 || index >= (int)pv->item_count) {
        return;
    }
    struct present_item *item = &pv->items[index];
    struct view *view = item->view;
    if (view->base.minimized) {
        kywc_view_set_minimized(&view->base, false);
    }
    if (item->workspace && item->workspace != workspace_manager_get_current()) {
        workspace_activate(item->workspace);
    }
    kywc_view_activate(&view->base);
    view_set_focus(view, input_manager_get_default_seat());
}

/* port of PresentWindowsEffect::relativeWindow: step one window at a time,
 * xdiff/ydiff of +-1000 walk to the far end */
static int relative_item(int index, int xdiff, int ydiff, bool wrap)
{
    if (pv->item_count == 0) {
        return -1;
    }
    if (index < 0 || index >= (int)pv->item_count) {
        index = 0;
    }
    int current = index;
    int steps = xdiff != 0 ? abs(xdiff) : abs(ydiff);
    for (int i = 0; i < steps; i++) {
        struct pv_box *w = &pv->items[current].current;
        int next = -1;
        for (int e = 0; e < (int)pv->item_count; e++) {
            if (e == current) {
                continue;
            }
            struct pv_box *ea = &pv->items[e].current;
            struct pv_box band;
            bool candidate;
            if (xdiff > 0) {
                /* detect right: a band across the desktop at w's row */
                band = (struct pv_box){ 0, w->y, pv->full.width, w->h };
                candidate = box_overlap(&band, ea, 0) && ea->x > w->x;
            } else if (xdiff < 0) {
                /* detect left */
                band = (struct pv_box){ 0, w->y, pv->full.width, w->h };
                candidate = box_overlap(&band, ea, 0) && ea->x + ea->w < w->x + w->w;
            } else if (ydiff > 0) {
                /* detect down: a band down the desktop at w's column */
                band = (struct pv_box){ w->x, 0, w->w, pv->full.height };
                candidate = box_overlap(&band, ea, 0) && ea->y > w->y;
            } else {
                /* detect up */
                band = (struct pv_box){ w->x, 0, w->w, pv->full.height };
                candidate = box_overlap(&band, ea, 0) && ea->y + ea->h < w->y + w->h;
            }
            if (!candidate) {
                continue;
            }
            if (next < 0) {
                next = e;
                continue;
            }
            struct pv_box *na = &pv->items[next].current;
            bool better = xdiff > 0 ? ea->x < na->x
                        : xdiff < 0 ? ea->x + ea->w > na->x + na->w
                        : ydiff > 0 ? ea->y < na->y
                                    : ea->y + ea->h > na->y + na->h;
            if (better) {
                next = e;
            }
        }
        if (next < 0) {
            if (wrap) {
                /* we are at the far end, wrap to the opposite extreme */
                if (xdiff != 0) {
                    return relative_item(current, xdiff > 0 ? -1000 : 1000, 0, false);
                }
                return relative_item(current, 0, ydiff > 0 ? -1000 : 1000, false);
            }
            break; /* no more windows in this direction */
        }
        current = next;
    }
    return current;
}

static void filter_append(const char *text)
{
    if (!pv->filter) {
        pv->filter = calloc(1, FILTER_BOX_CAP);
    }
    if (!pv->filter) {
        return;
    }
    size_t len = strlen(pv->filter);
    size_t add = strlen(text);
    if (len + add >= FILTER_BOX_CAP - 1) {
        return;
    }
    memcpy(pv->filter + len, text, add);
    pv->filter[len + add] = '\0';
}

static void filter_pop(void)
{
    if (!pv->filter || !*pv->filter) {
        return;
    }
    char *end = pv->filter + strlen(pv->filter);
    char *prev = g_utf8_prev_char(end);
    *prev = '\0';
}

static void close_item(int index)
{
    if (index < 0 || index >= (int)pv->item_count) {
        return;
    }
    struct view *view = pv->items[index].view;
    add_pending(view);
    kywc_view_close(&view->base);
    if (pv->highlighted == index) {
        pv->highlighted = -1;
    }
    rebuild(NULL);
}

static bool pointer_motion(struct seat_pointer_grab *grab, uint32_t time, double lx, double ly)
{
    update_hover();
    return true;
}

static bool pointer_button(struct seat_pointer_grab *grab, uint32_t time, uint32_t button,
                           bool pressed)
{
    if (pressed || (button != BTN_LEFT && button != BTN_RIGHT) || pv->closing) {
        return true;
    }
    double lx, ly;
    cursor_local(&lx, &ly);
    int hovered = -1;
    for (size_t i = 0; i < pv->item_count; i++) {
        if (point_in_box(lx, ly, &pv->items[i].current)) {
            hovered = (int)i;
            break;
        }
    }

    if (hovered >= 0) {
        struct present_item *item = &pv->items[hovered];
        bool close_hit = item->close_tree->node.enabled &&
                         point_in_box(lx, ly, &item->close_box);
        if (close_hit && button == BTN_LEFT) {
            /* the close button consumes the click: the effect stays open */
            close_item(hovered);
        } else if (button == BTN_LEFT) {
            activate_item(hovered);
            present_set_enabled(false);
        } else {
            /* right click on a window closes it, the effect stays open */
            close_item(hovered);
        }
    } else if (button == BTN_LEFT) {
        /* click on the desktop exits */
        present_set_enabled(false);
    }
    return true;
}

static bool pointer_axis(struct seat_pointer_grab *grab, uint32_t time, bool vertical,
                         double value)
{
    return true;
}

static void pointer_cancel(struct seat_pointer_grab *grab)
{
    present_set_enabled(false);
}

static const struct seat_pointer_grab_interface pointer_impl = {
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .cancel = pointer_cancel,
};

static bool keyboard_key(struct seat_keyboard_grab *grab, struct keyboard *keyboard,
                         uint32_t time, uint32_t key, bool pressed, uint32_t modifiers)
{
    if (!pressed) {
        if (pv->last_key == key) {
            pv->key_released = true;
        }
        return true;
    }
    bool repeat = (pv->last_key == key && !pv->key_released);
    pv->last_key = key;
    pv->key_released = false;
    if (pv->closing) {
        return true;
    }

    switch (key) {
    case KEY_ESC:
        present_set_enabled(false);
        return true;
    case KEY_ENTER:
    case KEY_KPENTER:
        if (pv->highlighted >= 0) {
            activate_item(pv->highlighted);
        }
        present_set_enabled(false);
        return true;
    case KEY_LEFT:
        pv->highlighted = relative_item(pv->highlighted, -1, 0, !repeat);
        output_schedule_frame(pv->output->wlr_output);
        return true;
    case KEY_RIGHT:
        pv->highlighted = relative_item(pv->highlighted, 1, 0, !repeat);
        output_schedule_frame(pv->output->wlr_output);
        return true;
    case KEY_UP:
        pv->highlighted = relative_item(pv->highlighted, 0, -1, !repeat);
        output_schedule_frame(pv->output->wlr_output);
        return true;
    case KEY_DOWN:
        pv->highlighted = relative_item(pv->highlighted, 0, 1, !repeat);
        output_schedule_frame(pv->output->wlr_output);
        return true;
    case KEY_HOME:
        pv->highlighted = relative_item(pv->highlighted, -1000, 0, false);
        output_schedule_frame(pv->output->wlr_output);
        return true;
    case KEY_END:
        pv->highlighted = relative_item(pv->highlighted, 1000, 0, false);
        output_schedule_frame(pv->output->wlr_output);
        return true;
    case KEY_PAGEUP:
        pv->highlighted = relative_item(pv->highlighted, 0, -1000, false);
        output_schedule_frame(pv->output->wlr_output);
        return true;
    case KEY_PAGEDOWN:
        pv->highlighted = relative_item(pv->highlighted, 0, 1000, false);
        output_schedule_frame(pv->output->wlr_output);
        return true;
    case KEY_BACKSPACE:
        if (pv->filter && *pv->filter) {
            filter_pop();
            pv->highlighted = -1;
            rebuild(NULL);
        }
        return true;
    case KEY_DELETE:
        if (pv->filter && *pv->filter) {
            pv->filter[0] = '\0';
            pv->highlighted = -1;
            rebuild(NULL);
        }
        return true;
    case KEY_TAB:
        return true; /* nothing at the moment */
    default:
        if (key == KEY_A && (modifiers & WLR_MODIFIER_LOGO)) {
            present_set_enabled(false);
            return true;
        }
        if (!(modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))) {
            char buffer[8] = { 0 };
            int n = xkb_state_key_get_utf8(keyboard->wlr_keyboard->xkb_state, key + 8, buffer,
                                           sizeof(buffer));
            if (n > 0) {
                /* like upstream: clearing the highlight makes rearrangeWindows
                 * re-select the first window of the filtered set */
                pv->highlighted = -1;
                filter_append(buffer);
                rebuild(NULL);
            }
        }
        return true;
    }
}

static void keyboard_cancel(struct seat_keyboard_grab *grab)
{
    present_set_enabled(false);
}

static const struct seat_keyboard_grab_interface keyboard_impl = {
    .key = keyboard_key,
    .cancel = keyboard_cancel,
};

static bool touch_event(struct seat_touch_grab *grab, uint32_t time, bool down)
{
    return pointer_button(&pv->pointer_grab, time, BTN_LEFT, down);
}

static bool touch_motion(struct seat_touch_grab *grab, uint32_t time, double lx, double ly)
{
    return pointer_motion(&pv->pointer_grab, time, lx, ly);
}

static void touch_cancel(struct seat_touch_grab *grab)
{
    present_set_enabled(false);
}

static const struct seat_touch_grab_interface touch_impl = {
    .touch = touch_event,
    .motion = touch_motion,
    .cancel = touch_cancel,
};

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&pv->output_destroy.link);
    pv->output_listening = false;
    pv->output = NULL;
    if (pv->enabled || pv->closing) {
        teardown();
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&pv->server_destroy.link);
    if (pv->enabled || pv->closing) {
        teardown();
    }
    wl_event_source_remove(pv->timer);
    ky_scene_node_destroy(&pv->tree->node);
    free(pv->filter);
    free(pv);
    pv = NULL;
}

static void rebuild(struct view *exclude)
{
    if (!pv->enabled || pv->closing) {
        return;
    }
    /* remember the current geometry and highlight so windows animate from
     * where they are and the highlighted window is kept if still visible */
    size_t old_count = pv->item_count;
    struct view *highlighted_view =
        pv->highlighted >= 0 ? pv->items[pv->highlighted].view : NULL;
    struct view **views = NULL;
    struct pv_box *boxes = NULL;
    if (old_count) {
        views = calloc(old_count, sizeof(*views));
        boxes = calloc(old_count, sizeof(*boxes));
        for (size_t i = 0; i < old_count && views && boxes; i++) {
            views[i] = pv->items[i].view;
            boxes[i] = pv->items[i].current;
        }
    }
    destroy_contents(false);
    if (!create_items(exclude)) {
        free(views);
        free(boxes);
        /* no windows left (e.g. the last one was just closed): exit */
        present_set_enabled(false);
        return;
    }
    for (size_t i = 0; i < pv->item_count; i++) {
        if (views && boxes) {
            for (size_t k = 0; k < old_count; k++) {
                if (views[k] == pv->items[i].view) {
                    pv->items[i].from = boxes[k];
                    pv->items[i].current = boxes[k];
                    break;
                }
            }
        }
    }
    free(views);
    free(boxes);

    if (pv->filter && *pv->filter) {
        /* filtering: highlight the first (top-left) window like findFirstWindow */
        pv->highlighted = 0;
    } else {
        pv->highlighted = highlighted_view ? index_of_view(highlighted_view) : -1;
        if (pv->highlighted < 0) {
            pv->highlighted = 0;
        }
    }
    pv->elapsed_ms = 0;
    update_filter_frame();
    output_schedule_frame(pv->output->wlr_output);
}

static void shortcut_action(struct key_binding *binding, void *data)
{
    present_windows_toggle();
}

bool present_windows_create(struct view_manager *view_manager)
{
    pv = calloc(1, sizeof(*pv));
    if (!pv) {
        return false;
    }
    pv->view_manager = view_manager;
    pv->highlighted = -1;
    wl_list_init(&pv->restore_list);
    wl_list_init(&pv->pending_list);

    struct view_layer *layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
    pv->tree = ky_scene_tree_create(layer->tree);
    ky_scene_node_set_enabled(&pv->tree->node, false);
    pv->backdrop = ky_scene_rect_create(pv->tree, 0, 0, (float[4]){ 0.0f, 0.0f, 0.0f, 0.0f });
    pv->panel_dim = ky_scene_rect_create(pv->tree, 0, 0, (float[4]){ 0.0f, 0.0f, 0.0f, 0.0f });

    pv->timer = wl_event_loop_add_timer(view_manager->server->event_loop, tick, pv);
    if (!pv->timer) {
        goto fail;
    }
    pv->pointer_grab = (struct seat_pointer_grab){ .interface = &pointer_impl, .data = pv };
    pv->keyboard_grab = (struct seat_keyboard_grab){ .interface = &keyboard_impl, .data = pv };
    pv->touch_grab = (struct seat_touch_grab){ .interface = &touch_impl, .data = pv };
    pv->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &pv->server_destroy);

    struct key_binding *binding =
        kywc_key_binding_create("Win+a:no", "toggle present windows (all desktops)");
    if (!binding ||
        !kywc_key_binding_register(binding, KEY_BINDING_TYPE_TOGGLE_SHOW_WINDOWS, shortcut_action,
                                   pv)) {
        if (binding) {
            kywc_key_binding_destroy(binding);
        }
        kywc_log(KYWC_ERROR, "(PresentWindows) Failed to register the Win+a shortcut");
        wl_list_remove(&pv->server_destroy.link);
        goto fail;
    }
    return true;

fail:
    if (pv->timer) {
        wl_event_source_remove(pv->timer);
    }
    ky_scene_node_destroy(&pv->tree->node);
    free(pv);
    pv = NULL;
    return false;
}
