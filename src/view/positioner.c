#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include "output.h"
#include "view/workspace.h"
#include "view_p.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

/* default grid gap and offset */
#define GRID_GAP_ROW (50)
#define GRID_GAP_COLUMN (50)
#define GRID_OFFSET_TOP (0)
#define GRID_OFFSET_BOTTOM (500)
#define GRID_OFFSET_LEFT (0)
#define GRID_OFFSET_RIGHT (500)

static struct positioner_manager {
    struct wl_list positioners;

    struct wl_listener new_view;
    struct wl_listener new_output;

    struct wl_listener server_destroy;
} *manager = NULL;

/* per output */
struct positioner {
    struct wl_list link;
    struct wl_list places;

    struct kywc_output *kywc_output;
    char *output_name;

    struct {
        struct kywc_box box;
        struct wl_list positioners;
        struct wl_list link;
    } catcher;

    struct kywc_box grid_area, usable_area;

    /* props about grid */
    int off_top, off_bottom, off_left, off_right;
    int gap_row, gap_column;
    /* props about slot in grid */
    int num_step, num_column;
    int key_slot, max_slot;

    struct wl_listener output_on;
    struct wl_listener output_off;
    struct wl_listener output_usable_area;
    struct wl_listener output_destroy;
};

/* per workspace in positioner */
struct place {
    struct wl_list link;
    struct workspace *workspace;
    struct positioner *positioner;

    struct wl_listener workspace_destroy;

    /* current alloc slots number */
    int alloc_number;
    struct slot *slots;
    struct wl_list entries;

    /* last slot that can used */
    int last_slot;
    /* first free slot, -1 means no free slot */
    int free_list;
};

/* pre-alloced per slot in place */
struct slot {
    int prev, next;
    int entries;
};

/* per view insert to place slot */
struct entry {
    struct kywc_view *view;

    struct place *place;
    struct wl_list link;
    int slot;

    bool skip_update;

    struct wl_listener view_premap;
    /* whatever do it every move */
    struct wl_listener view_position;
    struct wl_listener view_minimize;
    struct wl_listener view_unmap;
    struct wl_listener view_destroy;
    struct wl_listener view_workspace;
};

static int clamp_size(int size, int off, int gap)
{
    size -= off;
    if (size < gap) {
        size = gap;
    }
    return size / gap * gap;
}

static bool positioner_update_grid(struct positioner *pos, struct kywc_box *usable)
{
    if (kywc_box_equal(&pos->usable_area, usable)) {
        return false;
    }

    pos->usable_area = *usable;

    /* skip dead area and align to gap multiple */
    struct kywc_box area = {
        .x = usable->x + pos->off_left,
        .y = usable->y + pos->off_top,
        .width = clamp_size(usable->width, pos->off_left + pos->off_right, pos->gap_column),
        .height = clamp_size(usable->height, pos->off_top + pos->off_bottom, pos->gap_row),
    };

    if (kywc_box_equal(&pos->grid_area, &area)) {
        /* do nothing if grid area not changed */
        return false;
    }

    pos->grid_area = area;
    pos->num_column = area.width / pos->gap_column;
    int num_row = area.height / pos->gap_row;
    pos->num_step = MIN(pos->num_column, num_row);

    /* calc max slot number, (pos->num_column - 1, pos->num_step - 1) */
    pos->key_slot = pos->num_step * (pos->num_column - pos->num_step) + pos->num_step - 1;
    pos->max_slot = pos->key_slot + pos->num_step * (pos->num_step - 1) / 2;

    kywc_log(KYWC_INFO, "positioner %s: grid area is (%d, %d) %d x %d max slot %d",
             pos->output_name, pos->grid_area.x, pos->grid_area.y, pos->grid_area.width,
             pos->grid_area.height, pos->max_slot);

    return true;
}

static struct slot *place_find_slot(struct place *place, int slot)
{
    struct positioner *pos = place->positioner;

    if (slot < 0 || slot > pos->max_slot) {
        return NULL;
    }
    /* slot is alreay alloced */
    if (place->alloc_number > slot) {
        return &place->slots[slot];
    }

    /* resize to 2 times util bigger than slot */
    int alloc_number = place->alloc_number ? place->alloc_number : 16;
    while (alloc_number <= slot) {
        alloc_number *= 2;
    }
    alloc_number = MIN(alloc_number, pos->max_slot + 1);

    size_t alloc_size = alloc_number * sizeof(struct slot);
    void *data = realloc(place->slots, alloc_size);
    if (!data) {
        return NULL;
    }

    /* mark new alloced entries as free */
    size_t current = place->alloc_number * sizeof(struct slot);
    memset((char *)data + current, 0, alloc_size - current);
    place->slots = data;
    place->alloc_number = alloc_number;
    kywc_log(KYWC_DEBUG, "positioner %s(%s): alloc %d slots", pos->output_name,
             place->workspace->name, place->alloc_number);

    return &place->slots[slot];
}

static bool positioner_calc_slot(struct positioner *pos, struct entry *entry, int *slot)
{
    struct kywc_view *view = entry->view;

    /* not in positioner managed geometry */
    if (!kywc_box_contains_point(&pos->grid_area, view->geometry.x, view->geometry.y)) {
        return false;
    }

    int column = (view->geometry.x - view->margin.off_x - pos->grid_area.x) / pos->gap_column;
    int row = (view->geometry.y - view->margin.off_y - pos->grid_area.y) / pos->gap_row;
    /* dead area, not managed */
    if (row > column) {
        return false;
    }

    /* fast return if in diagonal */
    if (row == column) {
        *slot = row;
        return true;
    }

    /* calc slot */
    int num = pos->num_step * (column - row) + row;
    int lost = (column - row - 1) + pos->num_step - pos->num_column;
    *slot = lost <= 0 ? num : num - (1 + lost) * lost / 2;
    return true;
}

static bool positioner_calc_position(struct positioner *pos, int slot, int *x, int *y)
{
    if (slot > pos->max_slot) {
        return false;
    }

    int column = 0, row = 0;
    if (slot <= pos->key_slot) {
        row = slot % pos->num_step;
        column = slot / pos->num_step + row;
    } else {
        int num = pos->key_slot;
        column = pos->num_column - 1;
        row = pos->num_step - 1;
        for (int i = 0; i < row; i++) {
            num += row - i;
            int off = num - slot;
            if (off >= 0) {
                column -= off;
                row -= off + i + 1;
                break;
            }
        }
    }

    /* slot top-left coord */
    *x = column * pos->gap_column + pos->grid_area.x;
    *y = row * pos->gap_row + pos->grid_area.y;
    return true;
}

static bool slot_is_suitable(struct positioner *pos, struct kywc_view *view, int slot)
{
    /* get slot top-left coord, then combine output usable area */
    struct kywc_box geo;
    if (!positioner_calc_position(pos, slot, &geo.x, &geo.y)) {
        return false;
    }

    geo.width = pos->usable_area.x + pos->usable_area.width - geo.x;
    geo.height = pos->usable_area.y + pos->usable_area.height - geo.y;

    /* for view, maybe add ssd margin */
    int width = view->geometry.width + view->margin.off_width;
    int height = view->geometry.height + view->margin.off_height;

    if (geo.width >= width && geo.height >= height) {
        view->geometry.x = geo.x + view->margin.off_x;
        view->geometry.y = geo.y + view->margin.off_y;
        return true;
    }
    return false;
}

static void positioner_move_views(struct positioner *pos, struct kywc_box *src_box,
                                  struct kywc_box *dst_box, bool skip_update)
{
    double frac_x = (double)dst_box->width / src_box->width;
    double frac_y = (double)dst_box->height / src_box->height;
    int dx, dy, nx, ny;

    struct place *place;
    wl_list_for_each(place, &pos->places, link) {
        struct entry *entry, *tmp;
        wl_list_for_each_safe(entry, tmp, &place->entries, link) {
            if (!entry->view->movable) {
                continue;
            }

            dx = MAX(entry->view->geometry.x - entry->view->margin.off_x - src_box->x, 0);
            dy = MAX(entry->view->geometry.y - entry->view->margin.off_y - src_box->y, 0);
            nx = ceil(dx * frac_x) + dst_box->x + entry->view->margin.off_x;
            ny = ceil(dy * frac_y) + dst_box->y + entry->view->margin.off_y;

            /* move to dst */
            entry->skip_update = skip_update;
            kywc_view_move(entry->view, nx, ny);
            entry->skip_update = false;
        }
    }
}

static void positioner_handle_output_on(struct wl_listener *listener, void *data)
{
    struct positioner *pos = wl_container_of(listener, pos, output_on);
    struct output *output = output_from_kywc_output(pos->kywc_output);

    positioner_update_grid(pos, &output->usable_area);

    if (!wl_list_empty(&pos->catcher.link)) {
        positioner_move_views(pos, &pos->catcher.box, &pos->usable_area, false);
        wl_list_remove(&pos->catcher.link);
        wl_list_init(&pos->catcher.link);
    }
}

static void positioner_handle_output_off(struct wl_listener *listener, void *data)
{
    struct positioner *src = wl_container_of(listener, src, output_off);

    struct positioner *pos, *dst = NULL;
    wl_list_for_each(pos, &manager->positioners, link) {
        if (pos != src && pos->kywc_output && pos->kywc_output->state.enabled) {
            dst = pos;
            break;
        }
    }

    if (!dst) {
        kywc_log(KYWC_INFO, "no cather for output %s", src->output_name);
        return;
    }

    wl_list_insert(&dst->catcher.positioners, &src->catcher.link);
    src->catcher.box = dst->usable_area;
    positioner_move_views(src, &src->usable_area, &dst->usable_area, true);

    struct positioner *tmp;
    wl_list_for_each_safe(pos, tmp, &src->catcher.positioners, catcher.link) {
        wl_list_remove(&pos->catcher.link);
        wl_list_insert(&dst->catcher.positioners, &pos->catcher.link);
        pos->catcher.box = dst->usable_area;
        positioner_move_views(pos, &src->usable_area, &dst->usable_area, true);
    }
}

static void positioner_handle_output_usable_area(struct wl_listener *listener, void *data)
{
    struct positioner *pos = wl_container_of(listener, pos, output_usable_area);
    struct output *output = output_from_kywc_output(pos->kywc_output);

    struct kywc_box old = pos->usable_area;
    if (!positioner_update_grid(pos, &output->usable_area)) {
        return;
    }

    positioner_move_views(pos, &old, &pos->usable_area, false);

    struct positioner *tmp;
    wl_list_for_each(tmp, &pos->catcher.positioners, catcher.link) {
        tmp->catcher.box = pos->usable_area;
        positioner_move_views(tmp, &old, &pos->usable_area, true);
    }
}

static void positioner_handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct positioner *pos = wl_container_of(listener, pos, output_destroy);

    if (pos->kywc_output->state.enabled) {
        positioner_handle_output_off(&pos->output_off, NULL);
    }

    wl_list_remove(&pos->output_destroy.link);
    wl_list_remove(&pos->output_on.link);
    wl_list_remove(&pos->output_off.link);
    wl_list_remove(&pos->output_usable_area.link);

    /* don't destroy positioner */
    pos->kywc_output = NULL;
}

static struct positioner *positioner_from_output(struct kywc_output *kywc_output)
{
    struct positioner *pos;
    wl_list_for_each(pos, &manager->positioners, link) {
        /* we don't destroy positioner when output destroy */
        if (pos->kywc_output == kywc_output || !strcmp(pos->output_name, kywc_output->name)) {
            return pos;
        }
    }
    return NULL;
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct kywc_output *kywc_output = data;

    struct positioner *pos = positioner_from_output(kywc_output);
    if (!pos) {
        if (!(pos = calloc(1, sizeof(struct positioner)))) {
            return;
        }
        wl_list_init(&pos->places);
        wl_list_insert(&manager->positioners, &pos->link);
        pos->output_name = strdup(kywc_output->name);

        wl_list_init(&pos->catcher.link);
        wl_list_init(&pos->catcher.positioners);
    }

    pos->kywc_output = kywc_output;

    pos->output_on.notify = positioner_handle_output_on;
    wl_signal_add(&kywc_output->events.on, &pos->output_on);
    pos->output_off.notify = positioner_handle_output_off;
    wl_signal_add(&kywc_output->events.off, &pos->output_off);

    struct output *output = output_from_kywc_output(kywc_output);
    pos->output_usable_area.notify = positioner_handle_output_usable_area;
    wl_signal_add(&output->events.usable_area, &pos->output_usable_area);

    pos->output_destroy.notify = positioner_handle_output_destroy;
    wl_signal_add(&kywc_output->events.destroy, &pos->output_destroy);

    // TODO: maybe add positoner grid props configuration
    pos->off_top = GRID_OFFSET_TOP;
    pos->off_bottom = GRID_OFFSET_BOTTOM;
    pos->off_left = GRID_OFFSET_LEFT;
    pos->off_right = GRID_OFFSET_RIGHT;
    pos->gap_column = GRID_GAP_COLUMN;
    pos->gap_row = GRID_GAP_ROW;

    if (kywc_output->state.enabled) {
        positioner_update_grid(pos, &output->usable_area);
        positioner_handle_output_on(&pos->output_on, NULL);
    }
}

static void place_handle_workspace_destroy(struct wl_listener *listener, void *data)
{
    struct place *place = wl_container_of(listener, place, workspace_destroy);

    wl_list_remove(&place->workspace_destroy.link);
    wl_list_remove(&place->link);

    free(place->slots);
    free(place);
}

static struct place *positioner_get_place(struct positioner *pos, struct workspace *workspace)
{
    if (!pos || !workspace) {
        return NULL;
    }

    struct place *place;
    wl_list_for_each(place, &pos->places, link) {
        if (place->workspace == workspace) {
            return place;
        }
    }

    /* create a new place for this workspace */
    place = calloc(1, sizeof(struct place));
    if (!place) {
        return NULL;
    }

    place->workspace = workspace;
    place->positioner = pos;
    /* mark free list is empty */
    place->free_list = -1;

    /* pre-alloc some slots */
    if (!place_find_slot(place, MIN(31, pos->max_slot / 4))) {
        free(place);
        return NULL;
    }

    wl_list_init(&place->entries);
    wl_list_insert(&pos->places, &place->link);

    place->workspace_destroy.notify = place_handle_workspace_destroy;
    wl_signal_add(&workspace->events.destroy, &place->workspace_destroy);

    return place;
}

static void place_insert_free_slot(struct place *place, int slot)
{
    struct slot *slot_p = place_find_slot(place, slot);
    slot_p->entries = 0;

    int cur = -1;
    int next = place->free_list;

#if 1 // 0 if just insert to head
    while (next < slot && next >= 0) {
        cur = next;
        next = place->slots[cur].next;
    }
#endif
    place->slots[slot].prev = cur;
    place->slots[slot].next = next;

    /* not the head */
    if (cur >= 0) {
        place->slots[cur].next = slot;
    } else {
        place->free_list = slot;
    }

    /* not the tail */
    if (next >= 0) {
        place->slots[next].prev = slot;
    }
}

#if 0
static void place_debug_free_list(struct place *place)
{
    kywc_log(KYWC_INFO, "---- debug free list ----");
    kywc_log(KYWC_INFO, "free list: %d last slot: %d", place->free_list, place->last_slot);
    int start = place->free_list;
    while (start >= 0) {
        struct slot *slot_p = &place->slots[start];
        kywc_log(KYWC_INFO, "entry slot %d [prev = %d, next = %d]", start, slot_p->prev,
                 slot_p->next);
        start = slot_p->next;
    }
    kywc_log(KYWC_INFO, "----                 ----");
}
#endif

static void place_update_slot(struct place *place, int slot)
{
    struct slot *slot_p = place_find_slot(place, slot);

    /* if insert to a new slot, mark all skiped slot as free */
    if (slot >= place->last_slot) {
        for (int i = place->last_slot; i < slot; i++) {
            place_insert_free_slot(place, i);
        }
        place->last_slot = slot + 1;
        /* insert to free list slot, so remove this slot */
    } else if (slot_p->entries == 0) {
        /* update free_list if remove the first one */
        if (slot == place->free_list) {
            place->free_list = slot_p->next;
        }
        struct slot *prev_p = place_find_slot(place, slot_p->prev);
        if (prev_p) {
            prev_p->next = slot_p->next;
        }
        struct slot *next_p = place_find_slot(place, slot_p->next);
        if (next_p) {
            next_p->prev = slot_p->prev;
        }
    }

    slot_p->entries++;
}

static void place_insert_entry(struct place *place, struct entry *entry, int slot)
{
    entry->slot = slot;
    entry->place = place;
    wl_list_insert(&place->entries, &entry->link);

    struct slot *slot_p = place_find_slot(place, slot);
    if (!slot_p) {
        return;
    }

    place_update_slot(place, slot);
}

static void place_remove_entry(struct place *place, struct entry *entry)
{
    struct slot *slot_p = place_find_slot(place, entry->slot);
    if (slot_p && --slot_p->entries == 0) {
        place_insert_free_slot(place, entry->slot);
    }

    entry->place = NULL;
    entry->slot = -1;
    wl_list_remove(&entry->link);
}

static void place_update_entry(struct place *place, struct entry *entry, int slot)
{
    if (place == entry->place && slot == entry->slot) {
        return;
    }

    struct slot *slot_p = place_find_slot(entry->place, entry->slot);
    if (slot_p && --slot_p->entries == 0) {
        place_insert_free_slot(entry->place, entry->slot);
    }

    if (place != entry->place) {
        wl_list_remove(&entry->link);
        wl_list_insert(&place->entries, &entry->link);
        entry->place = place;
    }

    entry->slot = slot;
    slot_p = place_find_slot(place, slot);
    if (!slot_p) {
        return;
    }

    place_update_slot(place, slot);
}

static void entry_handle_view_premap(struct wl_listener *listener, void *data)
{
    struct entry *entry = wl_container_of(listener, entry, view_premap);
    struct kywc_view *kywc_view = entry->view;

    struct view *view = view_from_kywc_view(kywc_view);
    struct positioner *pos = positioner_from_output(view->output);
    struct place *place = positioner_get_place(pos, view->workspace);
    /* no output or workspace */
    if (!place) {
        return;
    }

    if (kywc_view->minimized || kywc_view->fullscreen) {
        place_insert_entry(place, entry, -1);
        return;
    }

    /* move to parent's center position */
    if (view->parent && view->parent->base.mapped) {
        struct kywc_view *parent = &view->parent->base;
        int center_x = parent->geometry.x + parent->geometry.width / 2;
        int center_y = parent->geometry.y + parent->geometry.height / 2;
        kywc_view->geometry.x = center_x - kywc_view->geometry.width / 2;
        kywc_view->geometry.y = center_y - kywc_view->geometry.height / 2;
        place_insert_entry(place, entry, -1);
        return;
    }

    /* if view size is too bigger or no slot */
    if (!slot_is_suitable(pos, kywc_view, 0) ||
        (place->last_slot > pos->max_slot && place->free_list < 0)) {
        /* show view in usable_area top-left */
        kywc_view->geometry.x = pos->usable_area.x + kywc_view->margin.off_x;
        kywc_view->geometry.y = pos->usable_area.y + kywc_view->margin.off_y;
        place_insert_entry(place, entry, 0);
        return;
    }

    struct slot *slot_p = NULL;
    /* find a suitable positioner slot, set view current (x, y) */
    int slot = place->free_list;
    while (slot >= 0) {
        if (slot_is_suitable(pos, kywc_view, slot)) {
            slot_p = place_find_slot(place, slot);
            break;
        }
        slot = place->slots[slot].next;
    }

    /* no free slot or not suitable slot in free list */
    if (!slot_p) {
        for (slot = place->last_slot; slot <= pos->max_slot; slot++) {
            if (slot_is_suitable(pos, kywc_view, slot)) {
                slot_p = place_find_slot(place, slot);
                break;
            }
        }
    }

    if (!slot_p) {
        /* show view in usable_area top-left */
        kywc_view->geometry.x = pos->usable_area.x + kywc_view->margin.off_x;
        kywc_view->geometry.y = pos->usable_area.y + kywc_view->margin.off_y;
        place_insert_entry(place, entry, 0);
        return;
    }

    place_insert_entry(place, entry, slot);
}

static void entry_handle_view_position(struct wl_listener *listener, void *data)
{
    struct entry *entry = wl_container_of(listener, entry, view_position);
    if (entry->skip_update) {
        return;
    }

    struct kywc_view *kywc_view = entry->view;
    struct view *view = view_from_kywc_view(kywc_view);

    /* the view is on the most output */
    struct kywc_output *kywc_output = view->output;
    struct output *output = output_from_kywc_output(kywc_output);
    int lx = kywc_view->geometry.x - kywc_view->margin.off_x;
    int ly = kywc_view->geometry.y - kywc_view->margin.off_y;

    if (!kywc_box_contains_point(&output->geometry, lx, ly)) {
        kywc_output = kywc_output_at_point(lx, ly);
    }

    struct positioner *pos = positioner_from_output(kywc_output);
    struct place *place = positioner_get_place(pos, view->workspace);

    /* calc the new slot in the new positioner */
    int slot = -1;
    positioner_calc_slot(pos, entry, &slot);
    place_update_entry(place, entry, slot);
}

static void entry_handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct entry *entry = wl_container_of(listener, entry, view_minimize);
    struct kywc_view *kywc_view = entry->view;

    if (kywc_view->minimized) {
        place_update_entry(entry->place, entry, -1);
    } else {
        /* restore slot */
        entry_handle_view_position(&entry->view_position, NULL);
    }
}

static void entry_handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct entry *entry = wl_container_of(listener, entry, view_unmap);

    place_remove_entry(entry->place, entry);
}

static void entry_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct entry *entry = wl_container_of(listener, entry, view_destroy);

    /* only need to free all resources */
    wl_list_remove(&entry->view_destroy.link);
    wl_list_remove(&entry->view_unmap.link);
    wl_list_remove(&entry->view_minimize.link);
    wl_list_remove(&entry->view_position.link);
    wl_list_remove(&entry->view_premap.link);
    wl_list_remove(&entry->view_workspace.link);

    free(entry);
}

static void entry_handle_view_workspace(struct wl_listener *listener, void *data)
{
    struct entry *entry = wl_container_of(listener, entry, view_workspace);
    /* view is not mapped, like xwayland shell */
    if (!entry->place) {
        return;
    }

    struct kywc_view *kywc_view = entry->view;
    struct view *view = view_from_kywc_view(kywc_view);

    /* we assume that the position of view is not changed */
    struct positioner *pos = entry->place->positioner;
    struct place *new = positioner_get_place(pos, view->workspace);

    place_update_entry(new, entry, entry->slot);
}

static void handle_new_view(struct wl_listener *listener, void *data)
{
    struct entry *entry = calloc(1, sizeof(struct entry));
    if (!entry) {
        return;
    }

    struct kywc_view *kywc_view = data;
    entry->view = kywc_view;

    entry->view_premap.notify = entry_handle_view_premap;
    wl_signal_add(&kywc_view->events.premap, &entry->view_premap);
    entry->view_position.notify = entry_handle_view_position;
    wl_signal_add(&kywc_view->events.position, &entry->view_position);
    entry->view_minimize.notify = entry_handle_view_minimize;
    wl_signal_add(&kywc_view->events.minimize, &entry->view_minimize);
    entry->view_unmap.notify = entry_handle_view_unmap;
    wl_signal_add(&kywc_view->events.unmap, &entry->view_unmap);
    entry->view_destroy.notify = entry_handle_view_destroy;
    wl_signal_add(&kywc_view->events.destroy, &entry->view_destroy);

    struct view *view = view_from_kywc_view(kywc_view);
    entry->view_workspace.notify = entry_handle_view_workspace;
    wl_signal_add(&view->events.workspace, &entry->view_workspace);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    wl_list_remove(&manager->new_output.link);
    wl_list_remove(&manager->new_view.link);

    struct positioner *pos, *tmp;
    wl_list_for_each_safe(pos, tmp, &manager->positioners, link) {
        wl_list_remove(&pos->link);
        free(pos->output_name);
        free(pos);
    }

    free(manager);
    manager = NULL;
}

bool positioner_manager_create(struct view_manager *view_manager)
{
    manager = calloc(1, sizeof(struct positioner_manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->positioners);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &manager->server_destroy);

    manager->new_output.notify = handle_new_output;
    kywc_output_add_new_listener(&manager->new_output);
    manager->new_view.notify = handle_new_view;
    kywc_view_add_new_listener(&manager->new_view);

    return true;
}
