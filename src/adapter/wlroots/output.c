#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_xdg_output_v1.h>

#include "output.h"
#include "wlroots_p.h"

static void output_find_best_mode(struct wlr_output *wlr_output, int32_t width, int32_t height,
                                  int32_t refresh, struct wlr_output_mode **best)
{
    /* find best mode */
    struct wlr_output_mode *m;
    wl_list_for_each(m, &wlr_output->modes, link) {
        if (width == m->width && height == m->height) {
            if (refresh == m->refresh) {
                *best = m;
                break;
            }
            if (!*best || m->refresh > (*best)->refresh) {
                *best = m;
            }
        }
    }
}

static void output_ensure_mode(struct wlr_output *wlr_output, struct wlr_output_mode *mode)
{
    if (wlr_output_test(wlr_output)) {
        return;
    }

    kywc_log(KYWC_ERROR, "mode rejected, falling back to another mode");
    struct wlr_output_mode *m;
    wl_list_for_each(m, &wlr_output->modes, link) {
        if (mode && mode == m) {
            continue;
        }

        wlr_output_set_mode(wlr_output, m);
        if (wlr_output_test(wlr_output)) {
            break;
        }
    }
}

static void wlroots_output_get_prop(struct output *output, struct kywc_output_prop *prop)
{
    struct wlr_output *wlr_output = output->data;

    prop->capability = 0;
    prop->port = wlr_drm_connector_get_id(wlr_output);
    prop->phys_width = wlr_output->phys_width;
    prop->phys_height = wlr_output->phys_height;
    prop->make = wlr_output->make;
    prop->model = wlr_output->model;
    prop->serial = wlr_output->serial;
    prop->desc = wlr_output->description;

    struct wlr_output_mode *mode;
    wl_list_for_each(mode, &wlr_output->modes, link) {
        struct kywc_output_mode *new = calloc(1, sizeof(struct kywc_output_mode));
        if (!new) {
            continue;
        }

        new->width = mode->width;
        new->height = mode->height;
        new->refresh = mode->refresh;
        new->preferred = mode->preferred;
        wl_list_insert(&prop->modes, &new->link);
    }
}

static void wlroots_output_get_state(struct output *output, struct kywc_output_state *state)
{
    struct wlr_output *wlr_output = output->data;
    struct wlroots_server *wlroots = wlr_output->data;

    // FIXME: enabled and power state
    state->enabled = wlr_output->enabled;
    state->power = wlr_output->enabled;
    state->width = wlr_output->width;
    state->height = wlr_output->height;
    state->refresh = wlr_output->refresh;
    state->transform = wlr_output->transform;
    state->scale = wlr_output->scale;

    // state->vrr_policy = wlr_output->adaptive_sync_status;
    struct wlr_output_layout_output *layout_output;
    layout_output = wlr_output_layout_get(wlroots->layout, wlr_output);
    if (layout_output) {
        state->lx = layout_output->x;
        state->ly = layout_output->y;
    } else {
        state->lx = state->ly = -1;
    }

    state->brightness = 80;
    state->color_temp = 6500;
}

static bool wlroots_output_set_state(struct output *output, struct kywc_output_state *state)
{
    struct wlr_output *wlr_output = output->data;
    struct wlroots_server *wlroots = wlr_output->data;
    struct wlr_output_mode *best = NULL;

    if (state->width <= 0 || state->height <= 0) {
        kywc_log(KYWC_INFO, "set preferred mode as no config found");
        best = wlr_output_preferred_mode(wlr_output);
    } else {
        output_find_best_mode(wlr_output, state->width, state->height, state->refresh, &best);
    }

    bool enabled = state->enabled && state->power;
    wlr_output_enable(wlr_output, enabled);

    if (enabled) {
        if (best) {
            wlr_output_set_mode(wlr_output, best);
        } else {
            wlr_output_set_custom_mode(wlr_output, state->width, state->height, state->refresh);
        }
        output_ensure_mode(wlr_output, best);
        wlr_output_set_transform(wlr_output, state->transform);
        wlr_output_set_scale(wlr_output, state->scale);
    }

    if (!wlr_output_commit(wlr_output)) {
        kywc_log(KYWC_ERROR, "Failed to commit output: %s", wlr_output->name);
        return false;
    }

    /* after output commit, we get actual status */
    struct wlr_output_layout_output *loutput = wlr_output_layout_get(wlroots->layout, wlr_output);
    bool need_layout = state->enabled;
    bool have_layout = !!loutput;
    bool going_on = need_layout && !have_layout;
    bool going_off = !need_layout && have_layout;

    /* if output disabled, skip output_layout_add */
    if (going_on && (state->lx == -1 || state->ly == -1)) {
        wlr_output_layout_add_auto(wlroots->layout, wlr_output);
    } else if (going_on) {
        wlr_output_layout_add(wlroots->layout, wlr_output, state->lx, state->ly);
    } else if (going_off) {
        /* layout output will destroyed */
        wlr_output_layout_remove(wlroots->layout, wlr_output);
    } else if (need_layout && have_layout && (loutput->x != state->lx || loutput->y != state->ly)) {
        /* if output logical size changed, layout_change alreay is emited in
         * output_commit. only need move when (x, y) of output is different.
         */
        wlr_output_layout_add(wlroots->layout, wlr_output, state->lx, state->ly);
    }

    return true;
}

static void wlroots_output_frame(struct output *output)
{
    struct wlr_output *wlr_output = output->data;

    if (!wlr_output_attach_render(wlr_output, NULL)) {
        return;
    }

    if (!wlr_output->needs_frame) {
        kywc_log(KYWC_DEBUG, "no frame needed, stop commit");
        wlr_output_rollback(wlr_output);
        return;
    }

    struct wlr_renderer *renderer = wlr_output->renderer;
    wlr_renderer_begin(renderer, wlr_output->width, wlr_output->height);
    wlr_renderer_clear(renderer, (float[]){ 0.25f, 0.25f, 0.25f, 1 });
    wlr_output_render_software_cursors(wlr_output, NULL);
    wlr_renderer_end(renderer);

    wlr_output_commit(wlr_output);
}

static const struct output_impl wlroots_output_impl = {
    .get_prop = wlroots_output_get_prop,
    .get_state = wlroots_output_get_state,
    .set_state = wlroots_output_set_state,
    .frame = wlroots_output_frame,
};

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->damage.link);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->needs_frame.link);

    output_destroy(output);
}

static void handle_output_frame(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, frame);
    output_frame(output);
}

static void handle_output_damage(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, damage);
    struct wlr_output *wlr_output = output->data;

    wlr_output_schedule_frame(wlr_output);
}

static void handle_output_needs_frame(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, needs_frame);
    struct wlr_output *wlr_output = output->data;

    wlr_output_schedule_frame(wlr_output);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct wlroots_server *wlroots = wl_container_of(listener, wlroots, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output->data = wlroots;

    if (!wlr_output_init_render(wlr_output, wlroots->allocator, wlroots->renderer)) {
        kywc_log(KYWC_ERROR, "unable to init output renderer");
        return;
    }

    struct output *output = output_create(wlr_output->name, &wlroots_output_impl, wlr_output);
    if (!output) {
        return;
    }

    output->frame.notify = handle_output_frame;
    output->destroy.notify = handle_output_destroy;
    output->damage.notify = handle_output_damage;
    output->needs_frame.notify = handle_output_needs_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    wl_signal_add(&wlr_output->events.damage, &output->damage);
    wl_signal_add(&wlr_output->events.needs_frame, &output->needs_frame);
}

bool wlroots_output_init(struct wlroots_server *wlroots)
{
    wlr_xdg_output_manager_v1_create(wlroots->server->display, wlroots->layout);

    wlroots->new_output.notify = handle_new_output;
    wl_signal_add(&wlroots->backend->events.new_output, &wlroots->new_output);

    return true;
}
