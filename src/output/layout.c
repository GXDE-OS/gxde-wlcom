#include <stdlib.h>

#include "config.h"
#include "output_p.h"
#include "util/md5.h"

#define print_md5(verb, md5, fmt, ...)                                                             \
    do {                                                                                           \
        char md5_str[MD5_DIGEST_LENGTH * 2 + 1];                                                   \
        char *str = md5_to_string(md5, MD5_DIGEST_LENGTH, md5_str, sizeof(md5_str));               \
        if (str) {                                                                                 \
            kywc_log(verb, fmt, ##__VA_ARGS__);                                                    \
        }                                                                                          \
    } while (0);

struct layout_manager {
    struct wl_list outputs;
    struct config *config;

    struct wl_listener server_destroy;
    struct wl_listener new_output;
    struct wl_listener configured;

    char outputs_layout[16];
};

struct output_layout {
    struct wl_list link;
    struct kywc_output *output;
    struct layout_manager *layout_manager;

    struct wl_listener destroy;

    uint8_t md5[MD5_DIGEST_LENGTH];

    struct kywc_output_state state;
    bool primary;
};

struct output_md5 {
    const char *name;
    uint8_t *md5;
};

static void output_layout_get_layout(struct output_layout *output_layout, const char *active_layout,
                                     char *layout)
{
    char md5_str[17];
    md5_to_string(output_layout->md5, 8, md5_str, sizeof(md5_str));

    strncpy(layout, active_layout, 15);
    layout[15] = ':';
    strncpy(layout + 16, md5_str, 15);
    layout[31] = '\0';
}

static const char *layout_manager_get_active_layout(struct layout_manager *layout_manager)
{
    struct layout_manager *lm = layout_manager;
    if (!lm->config || !lm->config->json) {
        return NULL;
    }

    /* get outputs_layout from layouts */
    json_object *outputs_layout = json_object_object_get(lm->config->json, lm->outputs_layout);
    if (!outputs_layout) {
        return NULL;
    }

    json_object *data;
    /* get active_layout from outputs_layout */
    if (json_object_object_get_ex(outputs_layout, "active_layout", &data)) {
        return json_object_get_string(data);
    }

    return NULL;
}

static bool output_layout_read_config(struct output_layout *output_layout,
                                      const char *active_layout)
{
    struct layout_manager *lm = output_layout->layout_manager;
    if (!lm->config || !lm->config->json || !active_layout) {
        return false;
    }

    /* get outputs_layout from layouts */
    json_object *outputs_layout = json_object_object_get(lm->config->json, lm->outputs_layout);
    if (!outputs_layout) {
        return false;
    }

    json_object *data;

    /* finally, get layout config */
    char layout[32];
    output_layout_get_layout(output_layout, active_layout, layout);
    json_object *config = json_object_object_get(outputs_layout, layout);
    if (!config) {
        return false;
    }

    struct kywc_output_state *state = &output_layout->state;
    if (json_object_object_get_ex(config, "enabled", &data)) {
        state->enabled = json_object_get_boolean(data);
        state->power = state->enabled;
    }
    if (json_object_object_get_ex(config, "primary", &data)) {
        output_layout->primary = json_object_get_boolean(data);
    }
    if (json_object_object_get_ex(config, "width", &data)) {
        state->width = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "height", &data)) {
        state->height = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "refresh", &data)) {
        state->refresh = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "transform", &data)) {
        state->transform = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "scale", &data)) {
        state->scale = json_object_get_double(data);
    }
    if (json_object_object_get_ex(config, "lx", &data)) {
        state->lx = json_object_get_int(data);
    }
    if (json_object_object_get_ex(config, "ly", &data)) {
        state->ly = json_object_get_int(data);
    }
    return true;
}

static void output_layout_write_config(struct output_layout *output_layout,
                                       const char *active_layout)
{
    struct layout_manager *lm = output_layout->layout_manager;
    if (!lm->config || !lm->config->json) {
        return;
    }

    /* get outputs_layout in layouts, create if no */
    json_object *outputs_layout = json_object_object_get(lm->config->json, lm->outputs_layout);
    if (!outputs_layout) {
        outputs_layout = json_object_new_object();
        json_object_object_add(lm->config->json, lm->outputs_layout, outputs_layout);
    }

    json_object_object_add(outputs_layout, "active_layout", json_object_new_string(active_layout));

    char layout[32];
    output_layout_get_layout(output_layout, active_layout, layout);

    json_object *config = json_object_object_get(outputs_layout, layout);
    if (!config) {
        config = json_object_new_object();
        json_object_object_add(outputs_layout, layout, config);
    }

    struct kywc_output_state *state = &output_layout->output->state;
    json_object_object_add(config, "enabled", json_object_new_boolean(state->enabled));
    json_object_object_add(config, "primary", json_object_new_boolean(output_layout->primary));
    json_object_object_add(config, "width", json_object_new_int(state->width));
    json_object_object_add(config, "height", json_object_new_int(state->height));
    json_object_object_add(config, "refresh", json_object_new_int(state->refresh));
    json_object_object_add(config, "scale", json_object_new_double(state->scale));
    json_object_object_add(config, "transform", json_object_new_int(state->transform));
    json_object_object_add(config, "lx", json_object_new_int(state->lx));
    json_object_object_add(config, "ly", json_object_new_int(state->ly));
}

static int compare_output_md5(const void *p1, const void *p2)
{
    const char *v1 = ((struct output_md5 *)p1)->name;
    const char *v2 = ((struct output_md5 *)p2)->name;
    return strcmp(v1, v2);
}

static void layout_manager_md5_to_uuid(uint8_t *md5, char *uuid)
{
    char md5_str[17];
    md5_to_string(md5, 8, md5_str, sizeof(md5_str));
    strncpy(uuid, md5_str, 15);
    uuid[15] = '\0';
}

static void layout_manager_generate_layout(struct layout_manager *layout_manager, char *layout_uuid,
                                           bool is_active_layout)
{
    struct output_md5 *o_md5s = NULL;
    int actual_cnt = 0;

    struct output_layout *ol;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        struct kywc_output *kywc_output = ol->output;
        if (is_active_layout && !kywc_output->state.enabled) {
            continue;
        }

        o_md5s = realloc(o_md5s, (actual_cnt + 1) * sizeof(struct output_md5));
        o_md5s[actual_cnt].name = kywc_output->name;
        o_md5s[actual_cnt].md5 = ol->md5;
        actual_cnt++;
    }

    if (!actual_cnt) {
        return;
    }

    if (actual_cnt == 1) {
        layout_manager_md5_to_uuid(o_md5s[0].md5, layout_uuid);
        free(o_md5s);
        return;
    }

    qsort(o_md5s, actual_cnt, sizeof(struct output_md5), compare_output_md5);

    uint8_t *md5s = calloc(actual_cnt, MD5_DIGEST_LENGTH);
    for (int i = 0; i < actual_cnt; ++i) {
        memcpy(md5s + i * MD5_DIGEST_LENGTH, o_md5s[i].md5, MD5_DIGEST_LENGTH);
        print_md5(KYWC_DEBUG, o_md5s[i].md5, "output[%s] md5: %s", o_md5s[i].name, str);
    }

    uint8_t md5[MD5_DIGEST_LENGTH];
    md5_generate(md5s, actual_cnt * MD5_DIGEST_LENGTH, md5);
    layout_manager_md5_to_uuid(md5, layout_uuid);

    free(o_md5s);
    free(md5s);
}

static void layout_manager_config_outputs(struct layout_manager *layout_manager)
{
    /* update current outputs layout */
    layout_manager_generate_layout(layout_manager, layout_manager->outputs_layout, false);

    const char *active_layout = layout_manager_get_active_layout(layout_manager);
    kywc_log(KYWC_INFO, "outputs %s active: %s", layout_manager->outputs_layout, active_layout);

    /* get all outputs configuration */
    struct output_layout *ol;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        ol->state = ol->output->state;
        if (output_layout_read_config(ol, active_layout)) {
            continue;
        }

        ol->state.enabled = ol->state.power = true;

        struct kywc_output_mode *mode = kywc_output_preferred_mode(ol->output);
        ol->state.width = mode->width;
        ol->state.height = mode->height;
        ol->state.refresh = mode->refresh;

        ol->state.scale =
            kywc_output_preferred_scale(ol->output, ol->state.width, ol->state.height);
    }

    /* fix primary output if needed */
    struct kywc_output *pending_primary = NULL;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        if (ol->primary && ol->state.enabled) {
            pending_primary = ol->output;
            break;
        }
    }

    wl_list_for_each(ol, &layout_manager->outputs, link) {
        if (!ol->state.enabled) {
            continue;
        }
        kywc_output_set_state(ol->output, &ol->state);
        if (!pending_primary) {
            pending_primary = ol->output;
        }
    }

    kywc_output_set_primary(pending_primary);

    wl_list_for_each(ol, &layout_manager->outputs, link) {
        if (ol->state.enabled) {
            continue;
        }
        kywc_output_set_state(ol->output, &ol->state);
    }

    output_manager_emit_configured();
}

static void output_layout_handle_destroy(struct wl_listener *listener, void *data)
{
    struct output_layout *output_layout = wl_container_of(listener, output_layout, destroy);
    kywc_log(KYWC_DEBUG, "output_layout: %s handle destroy", output_layout->output->name);

    wl_list_remove(&output_layout->link);
    wl_list_remove(&output_layout->destroy.link);

    struct layout_manager *layout_manager = output_layout->layout_manager;
    if (!wl_list_empty(&layout_manager->outputs)) {
        layout_manager_config_outputs(layout_manager);
    }

    free(output_layout);
}

static void output_layout_md5_generate(struct output_layout *output_layout)
{
    struct kywc_output_prop *prop = &output_layout->output->prop;

    md5_generate(prop->desc, strlen(prop->desc) + 1, output_layout->md5);
    print_md5(KYWC_INFO, output_layout->md5, "output %s md5: %s", output_layout->output->name, str);
}

static void layout_manager_handle_new_output(struct wl_listener *listener, void *data)
{
    struct output_layout *output_layout = calloc(1, sizeof(struct output_layout));
    if (!output_layout) {
        return;
    }

    struct layout_manager *layout_manager = wl_container_of(listener, layout_manager, new_output);
    struct kywc_output *kywc_output = data;

    output_layout->output = kywc_output;
    output_layout->layout_manager = layout_manager;
    wl_list_insert(&layout_manager->outputs, &output_layout->link);

    output_layout_md5_generate(output_layout);
    layout_manager_config_outputs(layout_manager);

    output_layout->destroy.notify = output_layout_handle_destroy;
    wl_signal_add(&kywc_output->events.destroy, &output_layout->destroy);
}

static void layout_manager_handle_configured(struct wl_listener *listener, void *data)
{
    struct layout_manager *layout_manager = wl_container_of(listener, layout_manager, configured);

    char active_layout[16];
    layout_manager_generate_layout(layout_manager, active_layout, true);

    struct output_layout *ol;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        output_layout_write_config(ol, active_layout);
    }
}

static void handle_layout_manager_destroy(struct wl_listener *listener, void *data)
{
    struct layout_manager *layout_manager =
        wl_container_of(listener, layout_manager, server_destroy);

    wl_list_remove(&layout_manager->server_destroy.link);
    wl_list_remove(&layout_manager->configured.link);
    wl_list_remove(&layout_manager->new_output.link);

    free(layout_manager);
}

bool layout_manager_create(struct server *server)
{
    struct layout_manager *layout_manager = calloc(1, sizeof(struct layout_manager));
    if (!layout_manager) {
        return false;
    }

    wl_list_init(&layout_manager->outputs);

    /* listener new_output signal */
    layout_manager->new_output.notify = layout_manager_handle_new_output;
    kywc_output_add_new_listener(&layout_manager->new_output);

    /* listener output configured signal */
    layout_manager->configured.notify = layout_manager_handle_configured;
    output_manager_add_configured_listener(&layout_manager->configured);

    layout_manager->server_destroy.notify = handle_layout_manager_destroy;
    server_add_destroy_listener(server, &layout_manager->server_destroy);

    layout_manager->config = config_manager_add_config("layouts", NULL, NULL, NULL, layout_manager);

    return true;
}
