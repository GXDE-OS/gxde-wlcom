#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include <kywc/log.h>

#include "output_p.h"
#include "server.h"

static struct output_manager *output_manager = NULL;
static char *unknown = "unknown";

static struct output *output_from_kywc_output(struct kywc_output *kywc_output)
{
    struct output *output = wl_container_of(kywc_output, output, base);
    return output;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&output_manager->server_destroy.link);

    free(output_manager);
    output_manager = NULL;
}

struct output_manager *output_manager_create(struct server *server)
{
    output_manager = calloc(1, sizeof(struct output_manager));
    if (!output_manager) {
        return NULL;
    }

    wl_list_init(&output_manager->outputs);
    wl_signal_init(&output_manager->events.new_output);
    wl_signal_init(&output_manager->events.primary_output);

    output_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &output_manager->server_destroy);

    output_manager_config_init(output_manager);

    kde_output_management_create(server);

    return output_manager;
}

void kywc_output_set_primary(struct kywc_output *kywc_output)
{
    if (output_manager->primary_output == kywc_output) {
        return;
    }

    kywc_log(KYWC_DEBUG, "primary output is changed to %s",
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

struct output *output_create(const char *name, const struct output_impl *impl, void *data)
{
    struct output *output = calloc(1, sizeof(struct output));
    if (!output) {
        return NULL;
    }

    struct kywc_output *kywc_output = &output->base;

    output->impl = impl;
    output->data = data;
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
    output->impl->get_prop(output, &kywc_output->prop);
    /* fill null pointer with unknown, but adapter should avoid null */
    if (!kywc_output->prop.model) {
        kywc_output->prop.model = unknown;
    }

    /* read config and apply it */
    output->impl->get_state(output, &kywc_output->state);
    struct kywc_output_state state = kywc_output->state;
    bool found = output_read_config(output, &state);
    if (!found) {
        state.enabled = state.power = true;
        // TODO: others
    }

    kywc_output_set_state(kywc_output, &state);

    // TODO: primary config
    if (kywc_output->state.enabled) {
        kywc_output_set_primary(kywc_output);
    }

    wl_signal_emit_mutable(&output_manager->events.new_output, kywc_output);

    return output;
}

struct kywc_output *kywc_output_from_resource(struct wl_resource *resource)
{
    void *data = wl_resource_get_user_data(resource);

    struct output *output;
    wl_list_for_each(output, &output_manager->outputs, link) {
        if (output->data == data) {
            return &output->base;
        }
    }

    return NULL;
}

bool kywc_output_set_state(struct kywc_output *kywc_output, struct kywc_output_state *state)
{
    struct output *output = output_from_kywc_output(kywc_output);
    if (!output->impl->set_state(output, state)) {
        return false;
    }

    struct kywc_output_state *current = &kywc_output->state;
    struct kywc_output_state old = kywc_output->state;
    output->impl->get_state(output, current);

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

void output_frame(struct output *output)
{
    struct kywc_output *kywc_output = &output->base;
    kywc_log(KYWC_DEBUG, "output %s frame coming", kywc_output->name);

    /* make sure something is done before commit */
    wl_signal_emit_mutable(&kywc_output->events.frame, kywc_output);

    output->impl->frame(output);
}

void output_destroy(struct output *output)
{
    struct kywc_output *kywc_output = &output->base;

    wl_signal_emit_mutable(&kywc_output->events.destroy, kywc_output);

    wl_list_remove(&output->link);

    bool have_enabled_output = false;
    struct output *output_tmp;
    wl_list_for_each(output_tmp, &output_manager->outputs, link) {
        if (!output_tmp->base.state.enabled) {
            continue;
        }

        have_enabled_output = true;
        /* fixup primary output if not fixed by destroy listeners */
        if (kywc_output == output_manager->primary_output) {
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

            // XXX: send primary before enable or not ?
            /* fixup primary with this output */
            if (kywc_output == output_manager->primary_output) {
                kywc_output_set_primary(&output_tmp->base);
            }
            break;
        }
    }

    /* no output to fixup primary */
    if (kywc_output == output_manager->primary_output) {
        kywc_output_set_primary(NULL);
    }

    struct kywc_output_mode *mode, *tmp_mode;
    wl_list_for_each_safe(mode, tmp_mode, &kywc_output->prop.modes, link) {
        wl_list_remove(&mode->link);
        free(mode);
    }

    free(output);
}
