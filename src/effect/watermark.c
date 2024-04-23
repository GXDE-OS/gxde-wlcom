// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>

#include "config.h"
#include "effect_p.h"
#include "painter.h"
#include "view/view.h"

enum expand_type {
    EXPAND_TYPE_NONE = 0,
    EXPAND_TYPE_REPEAT,
    EXPAND_TYPE_SCREEN,
    EXPAND_TYPE_SCREEN_RATIO,
};

struct watermark_info {
    char *file;
    double opacity;
    int x, y; // only used when expand == EXPAND_TYPE_NONE
    enum expand_type expand;
    bool topmost;
};

struct watermark_buffer {
    struct wl_list link;
    struct wlr_buffer *buffer;
};

struct watermark {
    struct wl_list link;
    struct watermark_effect *effect;

    struct ky_scene_buffer *scene_buffer;
    struct watermark_buffer *buffer;

    struct ky_scene_output *output;
    struct wl_listener output_commit; // wlr_output
    struct wl_listener output_destroy;
};

struct watermark_effect {
    struct wl_list watermarks; // per output
    struct wl_list buffers;    // watermark_buffer

    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    struct effect_manager *manager;
    struct config *config; // for dbus
    struct wl_listener new_output;

    struct ky_scene_tree *tree;
    struct watermark_info info;
};

static const char *registry_bus = "org.ukui.KWin";
static const char *registry_path = "/Watermark";
static const char *registry_interface = "org.ukui.kwin.Watermark";

static struct watermark_buffer *watermark_get_or_create_buffer(struct watermark_effect *effect,
                                                               struct ky_scene_output *output)
{
    struct watermark_buffer *buffer;
    wl_list_for_each(buffer, &effect->buffers, link) {
        /* if png is used, reuse this buffer always */
        return buffer;
    }

    buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        return NULL;
    }

    struct draw_info info = {
        .png_path = effect->info.file,
    };
    buffer->buffer = painter_draw_buffer(&info);

    wl_list_insert(&effect->buffers, &buffer->link);
    return buffer;
}

static void watermark_buffer_destroy(struct watermark_buffer *buffer)
{
    wl_list_remove(&buffer->link);
    wlr_buffer_drop(buffer->buffer);
    free(buffer);
}

static void watermark_destroy(struct watermark *watermark)
{
    wl_list_remove(&watermark->link);
    wl_list_remove(&watermark->output_destroy.link);
    wl_list_remove(&watermark->output_commit.link);
    ky_scene_node_destroy(&watermark->scene_buffer->node);
    free(watermark);
}

static void watermark_handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct watermark *watermark = wl_container_of(listener, watermark, output_destroy);
    watermark_destroy(watermark);
}

static void watermark_update_buffer(struct watermark *watermark)
{
    struct watermark_effect *effect = watermark->effect;
    struct ky_scene_output *output = watermark->output;

    struct watermark_buffer *buffer = watermark_get_or_create_buffer(effect, output);
    if (watermark->buffer != buffer) {
        ky_scene_buffer_set_buffer(watermark->scene_buffer, buffer->buffer);
        watermark->buffer = buffer;
    }

    int x = output->x, y = output->y;
    int width = watermark->buffer->buffer->width;
    int height = watermark->buffer->buffer->height;

    switch (effect->info.expand) {
    case EXPAND_TYPE_NONE:
        x += effect->info.x;
        y += effect->info.y;
        break;
    case EXPAND_TYPE_REPEAT:
        ky_scene_buffer_set_repeated(watermark->scene_buffer, true);
        /* fallthrought to screen */
    case EXPAND_TYPE_SCREEN:
        wlr_output_effective_resolution(output->output, &width, &height);
        break;
    case EXPAND_TYPE_SCREEN_RATIO:;
        int output_width, output_height;
        wlr_output_effective_resolution(output->output, &output_width, &output_height);

        float output_ratio = (float)output_width / output_height;
        float buffer_ratio = (float)width / height;
        if (output_ratio < buffer_ratio) {
            width = output_width;
            height = output_width / buffer_ratio;
            y += (output_height - height) / 2;
        } else {
            height = output_height;
            width = output_height * buffer_ratio;
            x += (output_width - width) / 2;
        }
        break;
    }

    ky_scene_node_set_position(&watermark->scene_buffer->node, x, y);
    ky_scene_buffer_set_dest_size(watermark->scene_buffer, width, height);
}

static void watermark_handle_output_commit(struct wl_listener *listener, void *data)
{
    struct watermark *watermark = wl_container_of(listener, watermark, output_commit);
    struct wlr_output_event_commit *event = data;

    /* only update buffer when output effective_resolution changed */
    if (event->state->committed &
        (WLR_OUTPUT_STATE_SCALE | WLR_OUTPUT_STATE_TRANSFORM | WLR_OUTPUT_STATE_MODE)) {
        watermark_update_buffer(watermark);
    }
}

static struct watermark *watermark_create(struct watermark_effect *effect,
                                          struct ky_scene_output *output)
{
    struct watermark *watermark = calloc(1, sizeof(*watermark));
    if (!watermark) {
        return NULL;
    }

    watermark->effect = effect;
    wl_list_insert(&effect->watermarks, &watermark->link);

    watermark->output = output;
    watermark->output_destroy.notify = watermark_handle_output_destroy;
    wl_signal_add(&output->events.destroy, &watermark->output_destroy);
    watermark->output_commit.notify = watermark_handle_output_commit;
    wl_signal_add(&output->output->events.commit, &watermark->output_commit);

    watermark->scene_buffer = ky_scene_buffer_create(effect->tree, NULL);
    ky_scene_buffer_set_opacity(watermark->scene_buffer, effect->info.opacity);
    ky_scene_node_set_input_bypassed(&watermark->scene_buffer->node, true);

    watermark_update_buffer(watermark);

    return watermark;
}

static void effect_destroy_watermarks(struct watermark_effect *effect)
{
    wl_list_remove(&effect->new_output.link);
    wl_list_init(&effect->new_output.link);

    struct watermark *watermark, *tmp;
    wl_list_for_each_safe(watermark, tmp, &effect->watermarks, link) {
        watermark_destroy(watermark);
    }

    struct watermark_buffer *buffer, *buffer_tmp;
    wl_list_for_each_safe(buffer, buffer_tmp, &effect->buffers, link) {
        watermark_buffer_destroy(buffer);
    }

    free(effect->info.file);
    effect->info.file = NULL;
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct watermark_effect *effect = wl_container_of(listener, effect, new_output);
    struct ky_scene_output *output = data;
    watermark_create(effect, output);
}

static bool effect_load_image(struct watermark_effect *effect, struct watermark_info *info)
{
    FILE *fp = fopen(info->file, "rb");
    if (!fp) {
        return false;
    }

    uint8_t header[8] = { 0 }; // magic number
    fread(header, sizeof(header), 1, fp);
    fclose(fp);

    bool is_png =
        (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 &&
         header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A);
    if (!is_png) {
        return false;
    }

    effect->info = *info;
    effect->info.file = strdup(info->file);

    return true;
}

static void effect_create_watermarks(struct watermark_effect *effect, struct watermark_info *info)
{
    if (!effect_load_image(effect, info)) {
        return;
    }

    enum layer layer = effect->info.topmost ? LAYER_ON_SCREEN_DISPLAY : LAYER_WATERMARK;
    struct view_layer *view_layer = view_manager_get_layer(layer, false);
    effect->tree = view_layer->tree;

    struct ky_scene *scene = effect->manager->server->scene;
    struct ky_scene_output *output;
    wl_list_for_each(output, &scene->outputs, link) {
        watermark_create(effect, output);
    }

    effect->new_output.notify = handle_new_output;
    wl_signal_add(&scene->events.new_output, &effect->new_output);
}

static int watermark_update(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    char *name = NULL;
    double opacity = 0;
    CK(sd_bus_message_read(m, "sd", &name, &opacity));

    struct watermark_effect *effect = userdata;
    /* destroy current watermarks, create new watermarks */
    effect_destroy_watermarks(effect);

    if (*name) {
        struct watermark_info info = {
            .file = name,
            .opacity = opacity,
            .expand = EXPAND_TYPE_SCREEN,
            .topmost = true,
        };
        effect_create_watermarks(effect, &info);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int watermark_update_ex(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    char *name = NULL;
    double opacity = 0;
    int x, y, expand, topmost;
    CK(sd_bus_message_read(m, "sdiiib", &name, &opacity, &x, &y, &expand, &topmost));

    struct watermark_effect *effect = userdata;
    /* destroy current watermarks, create new watermarks */
    effect_destroy_watermarks(effect);

    if (*name) {
        struct watermark_info info = {
            .file = name,
            .opacity = opacity,
            .x = x,
            .y = y,
            .expand = expand,
            .topmost = topmost,
        };
        effect_create_watermarks(effect, &info);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable watermark_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("updateWatermark", "sd", "", watermark_update, 0),
    SD_BUS_METHOD("updateWatermarkEx", "sdiiib", "", watermark_update_ex, 0),
    SD_BUS_VTABLE_END,
};

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct watermark_effect *effect = wl_container_of(listener, effect, enable);
    assert(wl_list_empty(&effect->watermarks));
    effect->config = config_manager_add_config(NULL, registry_bus, registry_path,
                                               registry_interface, watermark_vtable, effect);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct watermark_effect *effect = wl_container_of(listener, effect, disable);
    effect_destroy_watermarks(effect);
    config_destroy(effect->config);
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct watermark_effect *effect = wl_container_of(listener, effect, destroy);
    assert(wl_list_empty(&effect->watermarks));
    free(effect);
}

bool watermark_effect_create(struct effect_manager *manager)
{
    struct watermark_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    /* enabled by default */
    effect->effect = effect_create("watermark", 0, true, NULL);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    effect->manager = manager;
    wl_list_init(&effect->buffers);
    wl_list_init(&effect->watermarks);
    wl_list_init(&effect->new_output.link);

    effect->enable.notify = handle_effect_enable;
    wl_signal_add(&effect->effect->events.enable, &effect->enable);
    effect->disable.notify = handle_effect_disable;
    wl_signal_add(&effect->effect->events.disable, &effect->disable);
    effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&effect->effect->events.destroy, &effect->destroy);

    if (effect->effect->enabled) {
        handle_effect_enable(&effect->enable, NULL);
    }

    return true;
}
