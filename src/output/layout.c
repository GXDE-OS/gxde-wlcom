#include <stdio.h>
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

static struct layout_manager {
    struct output_manager *output_manager;
    struct config *config;

    struct wl_list outputs;

    struct wl_listener server_destroy;
    struct wl_listener new_output;
    struct wl_listener primary_output;

    char outputs_layout[16];
    char active_layout[16];

    uint8_t enabled_outputs;

} *layout_manager = NULL;

struct output_layout {
    struct wl_list link;
    struct kywc_output *output;
    uint8_t md5[MD5_DIGEST_LENGTH];
    struct layout_manager *layout_manager;

    struct wl_listener on;
    struct wl_listener off;
    struct wl_listener scale;
    struct wl_listener mode;
    struct wl_listener position;
    struct wl_listener transform;
    struct wl_listener destroy;

    bool primary;
};

struct output_md5 {
    const char *name;
    uint8_t md5[MD5_DIGEST_LENGTH];
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

static void layout_manager_md5_to_uuid(uint8_t *md5, char *uuid)
{
    char md5_str[17];
    md5_to_string(md5, 8, md5_str, sizeof(md5_str));
    strncpy(uuid, md5_str, 15);
    uuid[15] = '\0';
}

static bool layout_manager_config_init(struct layout_manager *layout_manager)
{
    layout_manager->config = config_manager_add_config("layouts", NULL, NULL, NULL, layout_manager);
    return !!layout_manager->config;
}

/* config the sytle of content
 * layouts {
 *          outputs_layout:{
 *              active_layout: active_layout_uuid
 *              active_layout_uuid:layout_uuid:{
 *                    enabled: ...
 *                    primary: ...
 *                    width: ...
 *                    height: ...
 *                    refresh: ...
 *                    transform: ...
 *                    lx: ...
 *                    ly: ...
 *              }
 *              active_layout_uuid:layout_uuid:{
 *                    enabled: ...
 *                    primary: ...
 *                    width: ...
 *                    height: ...
 *                    refresh: ...
 *                    transform: ...
 *                    lx: ...
 *                    ly: ...
 *              }
 *          }
 *
 *       }
 */

static bool layout_manager_get_active_layout_from_config(const char **active_layout)
{
    struct layout_manager *lm = layout_manager;
    if (!lm->config || !lm->config->json) {
        return false;
    }

    /* get outputs_layout from layouts */
    json_object *outputs_layout = json_object_object_get(lm->config->json, lm->outputs_layout);
    if (!outputs_layout) {
        return false;
    }

    json_object *data;
    /* get active_layout from outputs_layout */
    if (json_object_object_get_ex(outputs_layout, "active_layout", &data)) {
        *active_layout = json_object_get_string(data);
    }
    if (!*active_layout) {
        return false;
    }
    return true;
}

static bool output_layout_read_config(struct output_layout *output_layout,
                                      struct kywc_output_state *state, bool *primary)
{
    struct layout_manager *lm = output_layout->layout_manager;
    if (!lm->config || !lm->config->json) {
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
    output_layout_get_layout(output_layout, lm->active_layout, layout);
    json_object *config = json_object_object_get(outputs_layout, layout);
    if (!config) {
        return false;
    }

    if (json_object_object_get_ex(config, "enabled", &data)) {
        state->enabled = json_object_get_boolean(data);
        state->power = state->enabled;
    }
    if (json_object_object_get_ex(config, "primary", &data)) {
        *primary = json_object_get_boolean(data);
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

static void output_layout_write_config(struct output_layout *output_layout)
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

    json_object_object_add(outputs_layout, "active_layout",
                           json_object_new_string(lm->active_layout));

    char layout[32];
    output_layout_get_layout(output_layout, lm->active_layout, layout);

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

static void output_layout_md5_generate(struct output_layout *output_layout)
{
    struct kywc_output_prop *prop = &output_layout->output->prop;
    int len = snprintf(NULL, 0, "%s", prop->desc ? prop->desc : "") + 1;
    char *desc_str = calloc(1, len);
    if (!desc_str) {
        kywc_log(KYWC_ERROR, "output description str malloc failed");
        return;
    }
    snprintf(desc_str, len, "%s", prop->desc ? prop->desc : "");
    md5_generate(desc_str, len, output_layout->md5);
    free(desc_str);
    print_md5(KYWC_INFO, output_layout->md5, "output %s layout_md5: %s",
              output_layout->output->name, str);
}

static void layout_manager_generate_layout(char *layout_uuid, bool is_active_layout)
{
    uint8_t md5[MD5_DIGEST_LENGTH];
    uint8_t actual_cnt = 0;
    struct output_md5 *o_md5s = NULL;

    struct output_layout *ol;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        struct kywc_output *kywc_output = ol->output;
        if (is_active_layout && !kywc_output->state.enabled) {
            continue;
        }
        o_md5s = realloc(o_md5s, (actual_cnt + 1) * sizeof(struct output_md5));
        o_md5s[actual_cnt].name = kywc_output->name;
        uint8_t *ptr_md5 = o_md5s[actual_cnt].md5;
        memcpy(ptr_md5, ol->md5, MD5_DIGEST_LENGTH);
        actual_cnt++;
    }
    if (is_active_layout) {
        layout_manager->enabled_outputs = actual_cnt;
    }

    if (!actual_cnt) {
        return;
    }

    if (actual_cnt == 1) {
        memcpy(md5, o_md5s[0].md5, MD5_DIGEST_LENGTH);
        layout_manager_md5_to_uuid(md5, layout_uuid);
        free(o_md5s);
        return;
    }

    qsort(o_md5s, actual_cnt, sizeof(struct output_md5), compare_output_md5);

    uint8_t *md5s = calloc(actual_cnt, MD5_DIGEST_LENGTH);
    for (uint32_t i = 0; i < actual_cnt; ++i) {
        memcpy(md5s + i * MD5_DIGEST_LENGTH, o_md5s[i].md5, MD5_DIGEST_LENGTH);
        print_md5(KYWC_DEBUG, o_md5s[i].md5, "output[%s] md5: %s", o_md5s[i].name, str);
    }
    md5_generate(md5s, actual_cnt * MD5_DIGEST_LENGTH, md5);
    layout_manager_md5_to_uuid(md5, layout_uuid);
    free(o_md5s);
    free(md5s);
}

static void handle_layout_manager_destroy(struct wl_listener *listener, void *data)
{
    free(layout_manager);
}

static void output_layout_config_output(struct output_layout *output_layout)
{

    struct kywc_output *kywc_output = output_layout->output;
    struct kywc_output_state state = kywc_output->state;
    bool primary = false;
    bool found = output_layout_read_config(output_layout, &state, &primary);
    if (!found) {
        state.enabled = state.power = true;
        if (layout_manager->enabled_outputs == 1 && state.lx != -1 && state.ly != -1) {
            state.lx = 0;
            state.ly = 0;
        }
        // TODO: others
    }

    kywc_output_set_state(kywc_output, &state);

    // TODO: primary config
    if ((layout_manager->enabled_outputs == 1 && !primary)) {
        primary = true;
    }
    if (kywc_output->state.enabled && primary) {
        kywc_output_set_primary(kywc_output);
    }
    output_layout_write_config(output_layout);
}

static void output_layout_handle_on(struct wl_listener *listener, void *data)
{
    struct output_layout *output_layout = wl_container_of(listener, output_layout, on);
    kywc_log(KYWC_INFO, "output_layout: %s handle on", output_layout->output->name);

    layout_manager_generate_layout(layout_manager->active_layout, true);

    struct output_layout *ol;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        output_layout_config_output(ol);
    }
}

static void output_layout_handle_off(struct wl_listener *listener, void *data)
{
    struct output_layout *output_layout = wl_container_of(listener, output_layout, off);
    kywc_log(KYWC_DEBUG, "output_layout: %s handle off", output_layout->output->name);

    char active_layout[16];
    layout_manager_generate_layout(active_layout, true);
    if (strcmp(layout_manager->active_layout, active_layout) != 0) {
        strcpy(layout_manager->active_layout, active_layout);
        kywc_log(KYWC_INFO, "change active layout: %s", active_layout);
    }

    struct output_layout *ol;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        if (ol == output_layout) {
            continue;
        }
        output_layout_config_output(ol);
    }
    output_layout_write_config(output_layout);
}

static void output_layout_handle_mode(struct wl_listener *listener, void *data)
{
    struct output_layout *output_layout = wl_container_of(listener, output_layout, mode);
    output_layout_write_config(output_layout);
    kywc_log(KYWC_DEBUG, "output_layout: %s handle mode", output_layout->output->name);
}

static void output_layout_handle_scale(struct wl_listener *listener, void *data)
{

    struct output_layout *output_layout = wl_container_of(listener, output_layout, scale);
    output_layout_write_config(output_layout);
    kywc_log(KYWC_DEBUG, "output_layout: %s handle scale", output_layout->output->name);
}

static void output_layout_handle_position(struct wl_listener *listener, void *data)
{
    struct output_layout *output_layout = wl_container_of(listener, output_layout, position);
    output_layout_write_config(output_layout);
    kywc_log(KYWC_DEBUG, "output_layout: %s handle position", output_layout->output->name);
}

static void output_layout_handle_transform(struct wl_listener *listener, void *data)
{

    struct output_layout *output_layout = wl_container_of(listener, output_layout, transform);
    output_layout_write_config(output_layout);
    kywc_log(KYWC_DEBUG, "output_layout: %s handle transform", output_layout->output->name);
}

static void output_layout_handle_destroy(struct wl_listener *listener, void *data)
{
    struct output_layout *output_layout = wl_container_of(listener, output_layout, destroy);
    kywc_log(KYWC_DEBUG, "output_layout: %s handle destroy", output_layout->output->name);

    wl_list_remove(&output_layout->on.link);
    wl_list_remove(&output_layout->off.link);
    wl_list_remove(&output_layout->mode.link);
    wl_list_remove(&output_layout->scale.link);
    wl_list_remove(&output_layout->position.link);
    wl_list_remove(&output_layout->transform.link);
    wl_list_remove(&output_layout->destroy.link);
    wl_list_remove(&output_layout->link);

    free(output_layout);

    layout_manager_generate_layout(layout_manager->outputs_layout, false);

    const char *active_layout = NULL;
    layout_manager_get_active_layout_from_config(&active_layout);
    if (active_layout && strcmp(layout_manager->active_layout, active_layout) != 0) {
        strcpy(layout_manager->active_layout, active_layout);
        kywc_log(KYWC_INFO, "change active layout: %s", active_layout);
    }

    struct output_layout *ol;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        output_layout_config_output(ol);
    }
    config_manager_sync();
}

static void layout_manager_handle_primary_output(struct wl_listener *listener, void *data)
{
    struct kywc_output *output = data;
    if (!output) {
        return;
    }
    kywc_log(KYWC_DEBUG, "handle active_layout[%s] primary output: %s",
             layout_manager->active_layout, output->name);
    struct output_layout *ol;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        ol->primary = ol->output == output;
        output_layout_write_config(ol);
    }
}

#define OUTPUT_LAYOUT_ADD_SIGNAL(signal)                                                           \
    output_layout->signal.notify = output_layout_handle_##signal;                                  \
    wl_signal_add(&kywc_output->events.signal, &output_layout->signal);

static void layout_manager_handle_new_output(struct wl_listener *listener, void *data)
{
    struct output_layout *output_layout = calloc(1, sizeof(struct output_layout));
    if (!output_layout) {
        return;
    }

    struct kywc_output *kywc_output = data;
    output_layout->output = kywc_output;
    output_layout->layout_manager = layout_manager;

    output_layout_md5_generate(output_layout);
    wl_list_insert(&layout_manager->outputs, &output_layout->link);

    layout_manager_generate_layout(layout_manager->outputs_layout, false);

    const char *active_layout = NULL;
    layout_manager_get_active_layout_from_config(&active_layout);
    if (active_layout && strcmp(layout_manager->active_layout, active_layout) != 0) {
        strcpy(layout_manager->active_layout, active_layout);
        kywc_log(KYWC_INFO, "change active layout %s", active_layout);
    } else if (!active_layout) {
        layout_manager_generate_layout(layout_manager->active_layout, true);
    }

    struct output_layout *ol;
    wl_list_for_each(ol, &layout_manager->outputs, link) {
        output_layout_config_output(ol);
    }

    OUTPUT_LAYOUT_ADD_SIGNAL(on);
    OUTPUT_LAYOUT_ADD_SIGNAL(off);
    OUTPUT_LAYOUT_ADD_SIGNAL(mode);
    OUTPUT_LAYOUT_ADD_SIGNAL(scale);
    OUTPUT_LAYOUT_ADD_SIGNAL(position);
    OUTPUT_LAYOUT_ADD_SIGNAL(transform);
    OUTPUT_LAYOUT_ADD_SIGNAL(destroy);
}

#undef OUTPUT_LAYOUT_ADD_SIGNAL

bool layout_manager_create(struct server *server)
{
    layout_manager = calloc(1, sizeof(struct layout_manager));
    if (!layout_manager) {
        return false;
    }

    wl_list_init(&layout_manager->outputs);

    /* listener new_output signal */
    layout_manager->new_output.notify = layout_manager_handle_new_output;
    kywc_output_add_new_listener(&layout_manager->new_output);

    /* listener primary_output signal */
    layout_manager->primary_output.notify = layout_manager_handle_primary_output;
    kywc_output_add_primary_listener(&layout_manager->primary_output);

    layout_manager->server_destroy.notify = handle_layout_manager_destroy;
    server_add_destroy_listener(server, &layout_manager->server_destroy);

    layout_manager_config_init(layout_manager);

    return true;
}
