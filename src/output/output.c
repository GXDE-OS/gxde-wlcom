#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_xdg_output_v1.h>

#include <kywc/log.h>

#include "output_p.h"
#include "server.h"

static struct output_manager *output_manager = NULL;
static char *unknown = "unknown";

struct output *output_from_kywc_output(struct kywc_output *kywc_output)
{
    struct output *output = wl_container_of(kywc_output, output, base);
    return output;
}

struct output *output_from_wlr_output(struct wlr_output *wlr_output)
{
    return wlr_output->data;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&output_manager->server_destroy.link);

    free(output_manager);
    output_manager = NULL;
}

static void output_add_mode(struct kywc_output_prop *prop, struct wlr_output_mode *mode)
{
    struct kywc_output_mode *new = calloc(1, sizeof(struct kywc_output_mode));
    if (!new) {
        return;
    }

    new->width = mode->width;
    new->height = mode->height;
    new->refresh = mode->refresh;
    new->preferred = mode->preferred;
    wl_list_insert(&prop->modes, &new->link);
}

static void output_get_prop(struct output *output, struct kywc_output_prop *prop)
{
    struct wlr_output *wlr_output = output->wlr_output;

    prop->capability = 0;
    prop->phys_width = wlr_output->phys_width;
    prop->phys_height = wlr_output->phys_height;
    prop->make = wlr_output->make;
    prop->model = wlr_output->model;
    prop->serial = wlr_output->serial;
    prop->desc = wlr_output->description;

    /* fix zero mode in some backend, like wayland */
    if (wl_list_empty(&wlr_output->modes)) {
        struct wlr_output_mode mode = {
            .width = wlr_output->width,
            .height = wlr_output->height,
            .refresh = wlr_output->refresh,
            .preferred = true,
        };
        output_add_mode(prop, &mode);
    } else {
        struct wlr_output_mode *mode;
        wl_list_for_each(mode, &wlr_output->modes, link) {
            output_add_mode(prop, mode);
        }
    }
}

static void output_get_state(struct output *output, struct kywc_output_state *state)
{
    struct wlr_output *wlr_output = output->wlr_output;

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
    layout_output = wlr_output_layout_get(output_manager->server->layout, wlr_output);
    if (layout_output) {
        state->lx = layout_output->x;
        state->ly = layout_output->y;
    } else {
        state->lx = state->ly = -1;
    }

    state->brightness = 80;
    state->color_temp = 6500;
}

static struct output *output_create(const char *name, struct wlr_output *wlr_output)
{
    struct output *output = calloc(1, sizeof(struct output));
    if (!output) {
        return NULL;
    }

    output->wlr_output = wlr_output;
    wlr_output->data = output;

    struct kywc_output *kywc_output = &output->base;
    kywc_output->name = name;

    wl_signal_init(&kywc_output->events.on);
    wl_signal_init(&kywc_output->events.off);
    wl_signal_init(&kywc_output->events.scale);
    wl_signal_init(&kywc_output->events.transform);
    wl_signal_init(&kywc_output->events.mode);
    wl_signal_init(&kywc_output->events.position);
    wl_signal_init(&kywc_output->events.power);
    wl_signal_init(&kywc_output->events.frame);
    wl_signal_init(&kywc_output->events.destroy);

    output->manager = output_manager;
    wl_list_insert(&output_manager->outputs, &output->link);

    /* get props */
    wl_list_init(&kywc_output->prop.modes);
    output_get_prop(output, &kywc_output->prop);
    /* fill null pointer with unknown, but adapter should avoid null */
    if (!kywc_output->prop.model) {
        kywc_output->prop.model = unknown;
    }
    if (!kywc_output->prop.desc) {
        kywc_output->prop.desc = kywc_output->name;
    }

    /* read config and apply it */
    output_get_state(output, &kywc_output->state);

    if (!output_manager->has_layout_manager) {
        struct kywc_output_state state = kywc_output->state;
        bool found = output_read_config(output, &state);
        if (!found) {
            state.enabled = state.power = true;

            struct kywc_output_mode *mode = kywc_output_preferred_mode(kywc_output);
            state.width = mode->width;
            state.height = mode->height;
            state.refresh = mode->refresh;

            state.scale = kywc_output_preferred_scale(kywc_output, state.width, state.height);
        }

        kywc_output_set_state(kywc_output, &state);
    }

    wl_signal_emit_mutable(&output_manager->events.new_output, kywc_output);

    /* fix primary output */
    if (!output_manager->primary_output && kywc_output->state.enabled) {
        kywc_output_set_primary(kywc_output);
    }

    return output;
}

static void handle_output_frame(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, frame);
    struct kywc_output *kywc_output = &output->base;
    kywc_log(KYWC_DEBUG, "output %s frame coming", kywc_output->name);

    /* make sure something is done before commit */
    wl_signal_emit_mutable(&kywc_output->events.frame, kywc_output);

    struct wlr_output *wlr_output = output->wlr_output;

    if (!wlr_output->needs_frame) {
        kywc_log(KYWC_DEBUG, "no frame needed, stop commit");
        return;
    }

    if (!wlr_output_attach_render(wlr_output, NULL)) {
        return;
    }

    struct wlr_renderer *renderer = wlr_output->renderer;
    wlr_renderer_begin(renderer, wlr_output->width, wlr_output->height);
    wlr_renderer_clear(renderer, (float[]){ 0.25f, 0.25f, 0.25f, 1 });
    wlr_output_render_software_cursors(wlr_output, NULL);
    wlr_renderer_end(renderer);

    wlr_output_commit(wlr_output);
}

static void handle_output_damage(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, damage);

    wlr_output_schedule_frame(output->wlr_output);
}

static void handle_output_needs_frame(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, needs_frame);

    wlr_output_schedule_frame(output->wlr_output);
}

static void fix_outputs(struct kywc_output *destroy_output)
{
    bool have_enabled_output = false;
    struct output *output_tmp;
    wl_list_for_each(output_tmp, &output_manager->outputs, link) {
        if (!output_tmp->base.state.enabled) {
            continue;
        }

        have_enabled_output = true;
        /* fixup primary output if not fixed by destroy listeners */
        if (destroy_output == output_manager->primary_output) {
            kywc_output_set_primary(&output_tmp->base);
        }
        break;
    }

    /* all outputs are disabled or no output in manager */
    if (!have_enabled_output) {
        struct output *output_tmp;
        wl_list_for_each(output_tmp, &output_manager->outputs, link) {
            /* enable this output to keep one enabled */
            struct kywc_output_state state = output_tmp->base.state;
            state.enabled = state.power = true;
            state.lx = state.ly = 0;
            kywc_output_set_state(&output_tmp->base, &state);

            /* fixup primary with this output */
            if (destroy_output == output_manager->primary_output) {
                kywc_output_set_primary(&output_tmp->base);
            }
            break;
        }
    }

    /* no output to fixup primary */
    if (destroy_output == output_manager->primary_output) {
        kywc_output_set_primary(NULL);
    }
}

static void output_destroy(struct output *output)
{
    struct kywc_output *kywc_output = &output->base;

    kywc_output->destroying = true;
    wl_signal_emit_mutable(&kywc_output->events.destroy, kywc_output);

    wl_list_remove(&output->link);

    /* fix primary and power on all output */
    fix_outputs(kywc_output);

    struct kywc_output_mode *mode, *tmp_mode;
    wl_list_for_each_safe(mode, tmp_mode, &kywc_output->prop.modes, link) {
        wl_list_remove(&mode->link);
        free(mode);
    }

    free(output);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct output *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->damage.link);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->needs_frame.link);

    struct wlr_output *wlr_output = output->wlr_output;
    wlr_output_layout_remove(output_manager->server->layout, wlr_output);

    output_destroy(output);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct server *server = output_manager->server;
    struct wlr_output *wlr_output = data;

    if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
        kywc_log(KYWC_ERROR, "unable to init output renderer");
        return;
    }

    struct output *output = output_create(wlr_output->name, wlr_output);
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

struct output_manager *output_manager_create(struct server *server)
{
    output_manager = calloc(1, sizeof(struct output_manager));
    if (!output_manager) {
        return NULL;
    }

    output_manager->server = server;
    wl_list_init(&output_manager->outputs);
    wl_signal_init(&output_manager->events.new_output);
    wl_signal_init(&output_manager->events.primary_output);
    wl_signal_init(&output_manager->events.configured);

    output_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &output_manager->server_destroy);

    wlr_xdg_output_manager_v1_create(server->display, server->layout);
    output_manager->new_output.notify = handle_new_output;
    wl_signal_add(&server->backend->events.new_output, &output_manager->new_output);

    output_manager_config_init(output_manager);
    output_manager->has_layout_manager = layout_manager_create(server);

    kde_output_management_create(server);
    wlr_output_management_create(server);

    return output_manager;
}

void kywc_output_set_primary(struct kywc_output *kywc_output)
{
    if (output_manager->primary_output == kywc_output) {
        return;
    }

    kywc_log(KYWC_INFO, "primary output is changed to %s",
             kywc_output ? kywc_output->name : "none");
    output_manager->primary_output = kywc_output;
    wl_signal_emit_mutable(&output_manager->events.primary_output, kywc_output);
}

void kywc_output_add_new_listener(struct wl_listener *listener)
{
    wl_signal_add(&output_manager->events.new_output, listener);
}

void kywc_output_add_primary_listener(struct wl_listener *listener)
{
    wl_signal_add(&output_manager->events.primary_output, listener);
}

void output_manager_add_configured_listener(struct wl_listener *listener)
{
    wl_signal_add(&output_manager->events.configured, listener);
}

void output_manager_emit_configured(void)
{
    wl_signal_emit_mutable(&output_manager->events.configured, output_manager);
}

float kywc_output_preferred_scale(struct kywc_output *kywc_output, int width, int height)
{
    float scale = 1.0;
    if (kywc_output->prop.phys_width == 0 || kywc_output->prop.phys_height == 0) {
        return scale;
    }

    float dpi_x = (float)width / (kywc_output->prop.phys_width / 25.4);
    float dpi_y = (float)height / (kywc_output->prop.phys_height / 25.4);
    kywc_log(KYWC_DEBUG, "Output %s resolution: %dx%d, dpi_x: %f, dpi_y: %f", kywc_output->name,
             width, height, dpi_x, dpi_y);

    float dpi_max = dpi_x > dpi_y ? dpi_x : dpi_y;
    float dpi_ratio = dpi_max / 96;
    if (dpi_ratio > 1.0) {
        int multi = dpi_ratio / 0.25;
        scale = multi * 0.25;
    }
    return scale;
}

struct kywc_output_mode *kywc_output_preferred_mode(struct kywc_output *kywc_output)
{
    struct kywc_output_mode *mode;
    wl_list_for_each(mode, &kywc_output->prop.modes, link) {
        if (mode->preferred) {
            return mode;
        }
    }
    // No preferred mode, choose the first one
    return wl_container_of(kywc_output->prop.modes.prev, mode, link);
}

struct kywc_output *kywc_output_from_resource(struct wl_resource *resource)
{
    struct wlr_output *wlr_output = wl_resource_get_user_data(resource);
    struct output *output = output_from_wlr_output(wlr_output);

    return output ? &output->base : NULL;
}

static void output_find_best_mode(struct wlr_output *wlr_output, int32_t width, int32_t height,
                                  int32_t refresh, struct wlr_output_mode **best)
{
    /* find best mode */
    struct wlr_output_mode *m;
    wl_list_for_each(m, &wlr_output->modes, link) {
        if (width != m->width || height != m->height) {
            continue;
        }
        if (refresh == m->refresh) {
            *best = m;
            break;
        }
        if (!*best || m->refresh > (*best)->refresh) {
            *best = m;
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

static bool output_set_state(struct output *output, struct kywc_output_state *state)
{
    struct wlr_output *wlr_output = output->wlr_output;
    struct server *server = output_manager->server;

    bool enabled = state->enabled && state->power;
    wlr_output_enable(wlr_output, enabled);

    if (enabled) {
        struct wlr_output_mode *best = NULL;
        if (state->width <= 0 || state->height <= 0) {
            kywc_log(KYWC_INFO, "set preferred mode as no config found");
            best = wlr_output_preferred_mode(wlr_output);
        } else {
            output_find_best_mode(wlr_output, state->width, state->height, state->refresh, &best);
        }

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
    struct wlr_output_layout_output *loutput = wlr_output_layout_get(server->layout, wlr_output);
    bool need_layout = state->enabled;
    bool have_layout = !!loutput;
    bool going_on = need_layout && !have_layout;
    bool going_off = !need_layout && have_layout;

    /* if output disabled, skip output_layout_add */
    if (going_on && (state->lx == -1 || state->ly == -1)) {
        wlr_output_layout_add_auto(server->layout, wlr_output);
    } else if (going_on) {
        wlr_output_layout_add(server->layout, wlr_output, state->lx, state->ly);
    } else if (going_off) {
        /* layout output will destroyed */
        wlr_output_layout_remove(server->layout, wlr_output);
    } else if (need_layout && have_layout && (loutput->x != state->lx || loutput->y != state->ly)) {
        /* if output logical size changed, layout_change alreay is emited in
         * output_commit. only need move when (x, y) of output is different.
         */
        wlr_output_layout_add(server->layout, wlr_output, state->lx, state->ly);
    }

    return true;
}

bool kywc_output_set_state(struct kywc_output *kywc_output, struct kywc_output_state *state)
{
    struct output *output = output_from_kywc_output(kywc_output);
    if (!output_set_state(output, state)) {
        return false;
    }

    struct kywc_output_state *current = &kywc_output->state;
    struct kywc_output_state old = kywc_output->state;
    output_get_state(output, current);

    // XXX: fix current.enabled for dpms power
    current->enabled = state->enabled;

    /* check state changes */
    if (current->enabled != old.enabled) {
        if (!current->enabled) {
            wl_signal_emit_mutable(&kywc_output->events.off, kywc_output);
            output_write_config(output);
            return true;
        } else {
            wl_signal_emit_mutable(&kywc_output->events.on, kywc_output);
        }
    }

    if (current->power != old.power) {
        wl_signal_emit_mutable(&kywc_output->events.power, kywc_output);
    }

    if (current->width != old.width || current->height != old.height ||
        current->refresh != old.refresh) {
        wl_signal_emit_mutable(&kywc_output->events.mode, kywc_output);
    }

    if (current->transform != old.transform) {
        wl_signal_emit_mutable(&kywc_output->events.transform, kywc_output);
    }

    if (current->scale != old.scale) {
        wl_signal_emit_mutable(&kywc_output->events.scale, kywc_output);
    }

    if (current->lx != old.lx || current->ly != old.ly) {
        wl_signal_emit_mutable(&kywc_output->events.position, kywc_output);
    }

    output_write_config(output);

    return true;
}

struct kywc_output *kywc_output_by_name(const char *name)
{
    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (!strcmp(name, output->base.name)) {
            return &output->base;
        }
    }

    return NULL;
}

void kywc_output_effective_geometry(struct kywc_output *kywc_output, struct kywc_box *box)
{
    struct kywc_output_state *state = &kywc_output->state;

    if (state->transform % 2 == 0) {
        box->width = state->width;
        box->height = state->height;
    } else {
        box->width = state->height;
        box->height = state->width;
    }

    box->x = state->lx;
    box->y = state->ly;
    box->width /= state->scale;
    box->height /= state->scale;
}

bool kywc_output_contains_point(struct kywc_output *kywc_output, int x, int y)
{
    struct kywc_box geo;
    kywc_output_effective_geometry(kywc_output, &geo);

    return kywc_box_contains_point(&geo, x, y);
}
