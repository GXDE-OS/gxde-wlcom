// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_buffer.h>

#include <kywc/identifier.h>

#include "effect_p.h"
#include "output.h"
#include "painter.h"
#include "util/dbus.h"
#include "util/file.h"
#include "util/string.h"
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

struct watermark_entry {
    struct wl_list link;
    struct watermark *watermark;

    struct ky_scene_buffer *scene_buffer;

    struct output *output;
    struct wl_listener output_geometry;
    struct wl_listener output_disable;
};

struct watermark {
    struct wl_list link;
    const char *name;
    struct watermark_effect *effect;

    struct wl_list entries;    // per output
    struct wlr_buffer *buffer; // from image file
    struct wl_listener new_enabled_output;

    struct ky_scene_tree *tree;
    struct watermark_info info;
};

struct watermark_effect {
    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    struct effect_manager *manager;
    struct dbus_object *dbus; // for dbus
    struct dbus_object *dbus_v2;

    struct watermark watermark; // default
    struct wl_list watermarks;
};

static struct wlr_buffer *watermark_get_or_create_buffer(struct watermark *watermark)
{
    if (watermark->buffer) {
        return watermark->buffer;
    }

    struct draw_info info = { .image = watermark->info.file };
    struct wlr_buffer *buffer = painter_draw_buffer(&info);
    if (!buffer) {
        return NULL;
    }

    watermark->buffer = buffer;
    return buffer;
}

static void watermark_entry_destroy(struct watermark_entry *entry)
{
    wl_list_remove(&entry->link);
    wl_list_remove(&entry->output_disable.link);
    wl_list_remove(&entry->output_geometry.link);
    ky_scene_node_destroy(&entry->scene_buffer->node);
    free(entry);
}

static void watermark_entry_handle_output_disable(struct wl_listener *listener, void *data)
{
    struct watermark_entry *entry = wl_container_of(listener, entry, output_disable);
    watermark_entry_destroy(entry);
}

static void watermark_entry_update_buffer(struct watermark_entry *entry)
{
    struct watermark *watermark = entry->watermark;
    struct kywc_box *geo = &entry->output->geometry;

    struct wlr_buffer *buffer = watermark_get_or_create_buffer(watermark);
    if (!buffer) {
        return;
    }

    if (entry->scene_buffer->buffer != buffer) {
        ky_scene_buffer_set_buffer(entry->scene_buffer, buffer);
    }

    int x = geo->x, y = geo->y;
    int width = buffer->width;
    int height = buffer->height;

    switch (watermark->info.expand) {
    case EXPAND_TYPE_NONE:
        x += watermark->info.x;
        y += watermark->info.y;
        break;
    case EXPAND_TYPE_REPEAT:
        ky_scene_buffer_set_repeated(entry->scene_buffer, true);
        /* fallthrough to screen */
    case EXPAND_TYPE_SCREEN:
        width = geo->width;
        height = geo->height;
        break;
    case EXPAND_TYPE_SCREEN_RATIO:;
        float output_ratio = (float)geo->width / geo->height;
        float buffer_ratio = (float)width / height;
        if (output_ratio < buffer_ratio) {
            width = geo->width;
            height = geo->width / buffer_ratio;
            y += (geo->height - height) / 2;
        } else {
            height = geo->height;
            width = geo->height * buffer_ratio;
            x += (geo->width - width) / 2;
        }
        break;
    }

    ky_scene_node_set_position(&entry->scene_buffer->node, x, y);
    ky_scene_buffer_set_dest_size(entry->scene_buffer, width, height);
}

static void watermark_entry_handle_output_geometry(struct wl_listener *listener, void *data)
{
    struct watermark_entry *entry = wl_container_of(listener, entry, output_geometry);
    /*  update buffer when output geometry changed */
    watermark_entry_update_buffer(entry);
}

static bool watermark_entry_create(struct watermark *watermark, struct output *output)
{
    struct watermark_entry *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return false;
    }

    entry->watermark = watermark;
    wl_list_insert(&watermark->entries, &entry->link);

    entry->output = output;
    entry->output_disable.notify = watermark_entry_handle_output_disable;
    wl_signal_add(&output->events.disable, &entry->output_disable);
    entry->output_geometry.notify = watermark_entry_handle_output_geometry;
    wl_signal_add(&output->events.geometry, &entry->output_geometry);

    entry->scene_buffer = ky_scene_buffer_create(watermark->tree, NULL);
    ky_scene_buffer_set_opacity(entry->scene_buffer, watermark->info.opacity);
    ky_scene_node_set_input_bypassed(&entry->scene_buffer->node, true);

    watermark_entry_update_buffer(entry);

    return true;
}

static void watermark_destroy_entries(struct watermark *watermark)
{
    wl_list_remove(&watermark->new_enabled_output.link);
    wl_list_init(&watermark->new_enabled_output.link);

    struct watermark_entry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, &watermark->entries, link) {
        watermark_entry_destroy(entry);
    }

    wlr_buffer_drop(watermark->buffer);
    watermark->buffer = NULL;

    free(watermark->info.file);
    watermark->info.file = NULL;
}

static void handle_new_enabled_output(struct wl_listener *listener, void *data)
{
    struct watermark *watermark = wl_container_of(listener, watermark, new_enabled_output);
    struct kywc_output *output = data;
    watermark_entry_create(watermark, output_from_kywc_output(output));
}

static void watermark_create_entries(struct watermark *watermark, struct watermark_info *info)
{
    char *file = string_expand_path(info->file);
    if (!file || !file_exists(file)) {
        free(file);
        return;
    }

    watermark->info = *info;
    watermark->info.file = file;

    enum layer layer = watermark->info.topmost ? LAYER_ON_SCREEN_DISPLAY : LAYER_WATERMARK;
    struct view_layer *view_layer = view_manager_get_layer(layer, false);
    watermark->tree = view_layer->tree;

    struct ky_scene *scene = watermark->effect->manager->server->scene;
    struct ky_scene_output *output;
    wl_list_for_each(output, &scene->outputs, link) {
        watermark_entry_create(watermark, output_from_wlr_output(output->output));
    }

    output_manager_add_new_enabled_listener(&watermark->new_enabled_output);
}

static void watermark_init(struct watermark *watermark, struct watermark_effect *effect)
{
    watermark->effect = effect;
    wl_list_init(&watermark->entries);
    wl_list_init(&watermark->new_enabled_output.link);
    watermark->new_enabled_output.notify = handle_new_enabled_output;
}

static struct watermark *watermark_create(struct watermark_effect *effect)
{
    struct watermark *watermark = calloc(1, sizeof(*watermark));
    if (!watermark) {
        return NULL;
    }

    watermark_init(watermark, effect);
    wl_list_insert(&effect->watermarks, &watermark->link);
    watermark->name = kywc_identifier_uuid_generate();

    return watermark;
}

static void watermark_destroy(struct watermark *watermark)
{
    watermark_destroy_entries(watermark);
    wl_list_remove(&watermark->link);
    free((void *)watermark->name);
    free(watermark);
}

static struct watermark *watermark_by_name(struct watermark_effect *effect, const char *name)
{
    struct watermark *watermark;
    wl_list_for_each(watermark, &effect->watermarks, link) {
        if (strcmp(watermark->name, name) == 0) {
            return watermark;
        }
    }
    return NULL;
}

static int watermark_update(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    char *file = NULL;
    double opacity = 0;
    CK(sd_bus_message_read(m, "sd", &file, &opacity));

    struct watermark_effect *effect = userdata;
    watermark_destroy_entries(&effect->watermark);

    if (file && *file) {
        struct watermark_info info = {
            .file = file,
            .opacity = opacity,
            .expand = EXPAND_TYPE_SCREEN,
            .topmost = true,
        };
        watermark_create_entries(&effect->watermark, &info);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static int watermark_update_ex(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    char *file = NULL;
    double opacity = 0;
    int x, y, expand, topmost;
    CK(sd_bus_message_read(m, "sdiiib", &file, &opacity, &x, &y, &expand, &topmost));

    struct watermark_effect *effect = userdata;
    /* destroy current watermark entries, create new entries */
    watermark_destroy_entries(&effect->watermark);

    if (file && *file) {
        struct watermark_info info = {
            .file = file,
            .opacity = opacity,
            .x = x,
            .y = y,
            .expand = expand,
            .topmost = topmost,
        };
        watermark_create_entries(&effect->watermark, &info);
    }

    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable watermark_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("updateWatermark", "sd", "", watermark_update, 0),
    SD_BUS_METHOD("updateWatermarkEx", "sdiiib", "", watermark_update_ex, 0),
    SD_BUS_VTABLE_END,
};

static int watermark2_create(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    char *file = NULL;
    double opacity = 0;
    int x, y, expand, topmost;
    CK(sd_bus_message_read(m, "sdiiib", &file, &opacity, &x, &y, &expand, &topmost));

    struct watermark_effect *effect = userdata;
    struct watermark *watermark = watermark_create(effect);
    if (!watermark) {
        return sd_bus_reply_method_return(m, "s", NULL);
    }

    if (file && *file) {
        struct watermark_info info = {
            .file = file,
            .opacity = opacity,
            .x = x,
            .y = y,
            .expand = expand,
            .topmost = topmost,
        };
        watermark_create_entries(watermark, &info);
    }

    return sd_bus_reply_method_return(m, "s", watermark->name);
}

static int watermark2_update(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    char *name = NULL, *file = NULL;
    double opacity = 0;
    int x, y, expand, topmost;
    CK(sd_bus_message_read(m, "ssdiiib", &name, &file, &opacity, &x, &y, &expand, &topmost));

    struct watermark_effect *effect = userdata;
    struct watermark *watermark = watermark_by_name(effect, name);
    if (!watermark) {
        return sd_bus_reply_method_return(m, "b", false);
    }

    /* destroy current watermark entries, create new entries */
    watermark_destroy_entries(watermark);

    if (file && *file) {
        struct watermark_info info = {
            .file = file,
            .opacity = opacity,
            .x = x,
            .y = y,
            .expand = expand,
            .topmost = topmost,
        };
        watermark_create_entries(watermark, &info);
    }

    return sd_bus_reply_method_return(m, "b", true);
}

static int watermark2_destroy(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    char *name = NULL;
    CK(sd_bus_message_read(m, "s", &name));

    struct watermark_effect *effect = userdata;
    struct watermark *watermark = watermark_by_name(effect, name);
    if (!watermark) {
        return sd_bus_reply_method_return(m, NULL);
    }

    watermark_destroy(watermark);

    return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable watermark2_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("createWatermark", "sdiiib", "s", watermark2_create, 0),
    SD_BUS_METHOD("updateWatermark", "ssdiiib", "b", watermark2_update, 0),
    SD_BUS_METHOD("destroyWatermark", "s", "", watermark2_destroy, 0),
    SD_BUS_VTABLE_END,
};

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct watermark_effect *effect = wl_container_of(listener, effect, enable);
    assert(wl_list_empty(&effect->watermarks));
    effect->dbus = dbus_register_object("org.ukui.KWin", "/Watermark", "org.ukui.kwin.Watermark",
                                        watermark_vtable, effect);
    effect->dbus_v2 = dbus_register_object(NULL, "/com/kylin/Wlcom/Watermark",
                                           "com.kylin.Wlcom.Watermark", watermark2_vtable, effect);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct watermark_effect *effect = wl_container_of(listener, effect, disable);
    watermark_destroy_entries(&effect->watermark);
    struct watermark *watermark, *tmp;
    wl_list_for_each_safe(watermark, tmp, &effect->watermarks, link) {
        watermark_destroy(watermark);
    }
    dbus_unregister_object(effect->dbus);
    dbus_unregister_object(effect->dbus_v2);
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct watermark_effect *effect = wl_container_of(listener, effect, destroy);
    assert(wl_list_empty(&effect->watermarks));
    wl_list_remove(&effect->destroy.link);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    free(effect);
}

static bool handle_effect_configure(struct effect *effect, const struct effect_option *option)
{
    if (effect_option_is_enabled_option(option)) {
        return true;
    }

    return false;
}

static const struct effect_interface watemark_effect_impl = {
    .configure = handle_effect_configure,
};

bool watermark_effect_create(struct effect_manager *manager)
{
    struct watermark_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    /* enabled by default */
    effect->effect = effect_create("watermark", 0, true, &watemark_effect_impl, NULL);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    effect->manager = manager;
    wl_list_init(&effect->watermarks);
    watermark_init(&effect->watermark, effect);

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
