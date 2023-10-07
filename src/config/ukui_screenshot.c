// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _DEFAULT_SOURCE
#include <drm_fourcc.h>
#include <limits.h>
#include <stdlib.h>

#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output_layout.h>

#include <kywc/log.h>

#include "config_p.h"
#include "painter.h"
#include "server.h"

static const char *registry_bus = "org.ukui.KWin";
static const char *registry_path = "/Screenshot";
static const char *registry_interface = "org.ukui.kwin.Screenshot";

struct screenshot_output {
    struct wl_list link;
    struct wlr_output_layout_output *l_output;
    struct wl_listener output_commit;
    /* when output disabled or destroyed */
    struct wl_listener layout_destroy;

    bool cursor_locked;
};

static struct screenshot_manager {
    struct wl_list outputs;
    struct wlr_output_layout *layout;
    struct wl_listener destroy;

    struct wlr_allocator *allocator;
    struct wlr_renderer *renderer;
    struct wlr_buffer *buffer;
    struct wlr_box geo;

    sd_bus_message *msg;
    bool taking_screenshot;
} *manager = NULL;

static void screenshot_done(void)
{
    /* not finished yet */
    if (!wl_list_empty(&manager->outputs)) {
        return;
    }

    void *data;
    uint32_t format;
    size_t stride;
    struct wlr_buffer *buffer = painter_create_buffer(manager->geo.width, manager->geo.height, 1.0);
    wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format,
                                     &stride);

    wlr_renderer_begin_with_buffer(manager->renderer, manager->buffer);
    wlr_renderer_read_pixels(manager->renderer, format, stride, manager->geo.width,
                             manager->geo.height, 0, 0, 0, 0, data);
    wlr_renderer_end(manager->renderer);

    wlr_buffer_end_data_ptr_access(buffer);
    // kywc_log(KYWC_INFO, "screenshot copy buffer to memory");

    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "/tmp/%s", "kywc_screenshot_XXXXXX.png");
    mkstemps(path, 4);

    painter_buffer_to_file(buffer, path);
    // kywc_log(KYWC_INFO, "screenshot write to png");
    wlr_buffer_drop(buffer);

    sd_bus_reply_method_return(manager->msg, "s", path);
    sd_bus_message_unref(manager->msg);
    manager->msg = NULL;

    manager->taking_screenshot = false;
    // kywc_log(KYWC_INFO, "screenshot done, send reply %s", path);
}

static void screenshot_output_destroy(struct screenshot_output *s_output)
{
    if (s_output->l_output) {
        wlr_output_lock_attach_render(s_output->l_output->output, false);
        if (s_output->cursor_locked) {
            wlr_output_lock_software_cursors(s_output->l_output->output, false);
        }
    }

    wl_list_remove(&s_output->output_commit.link);
    wl_list_remove(&s_output->layout_destroy.link);
    wl_list_remove(&s_output->link);
    free(s_output);
}

static void screenshot_handle_layout_destroy(struct wl_listener *listener, void *data)
{
    struct screenshot_output *s_output = wl_container_of(listener, s_output, layout_destroy);
    s_output->l_output = NULL;
    screenshot_output_destroy(s_output);
}

static void screenshot_handle_output_commit(struct wl_listener *listener, void *data)
{
    struct screenshot_output *s_output = wl_container_of(listener, s_output, output_commit);
    struct wlr_output_event_commit *event = data;
    struct wlr_output *output = s_output->l_output->output;

    if (!(event->state->committed & WLR_OUTPUT_STATE_BUFFER)) {
        return;
    }

    int dst_x = s_output->l_output->x - manager->geo.x;
    int dst_y = s_output->l_output->y - manager->geo.y;
    int width, height;
    wlr_output_effective_resolution(output, &width, &height);

    struct wlr_texture *src_tex = wlr_texture_from_buffer(output->renderer, event->state->buffer);
    struct wlr_render_pass *pass =
        wlr_renderer_begin_buffer_pass(output->renderer, manager->buffer, NULL);

    struct wlr_render_texture_options options = {
        .texture = src_tex,
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
        .dst_box = { dst_x, dst_y, width, height },
        .transform = wlr_output_transform_invert(output->transform),
    };

    wlr_render_pass_add_texture(pass, &options);
    wlr_render_pass_submit(pass);
    wlr_texture_destroy(src_tex);

    // kywc_log(KYWC_INFO, "screenshot output %s copy to (%d, %d) %d x %d", output->name, dst_x,
    // dst_y, width, height);

    screenshot_output_destroy(s_output);
    screenshot_done();
}

static void screenshot_output_create(struct wlr_output_layout_output *l_output, bool cursor_locked)
{
    struct screenshot_output *s_output = calloc(1, sizeof(struct screenshot_output));
    if (!s_output) {
        return;
    }

    s_output->l_output = l_output;
    s_output->cursor_locked = cursor_locked;
    wl_list_insert(&manager->outputs, &s_output->link);

    s_output->layout_destroy.notify = screenshot_handle_layout_destroy;
    wl_signal_add(&l_output->events.destroy, &s_output->layout_destroy);

    struct wlr_output *output = l_output->output;
    s_output->output_commit.notify = screenshot_handle_output_commit;
    wl_signal_add(&output->events.commit, &s_output->output_commit);

    wlr_output_schedule_frame(output);
    wlr_output_lock_attach_render(output, true);
    if (s_output->cursor_locked) {
        wlr_output_lock_software_cursors(output, true);
    }
}

static struct wlr_buffer *screenshot_create_buffer(int width, int height)
{
    bool need_create = !manager->buffer || // no buffer or smaller than source
                       (manager->buffer->width < width || manager->buffer->height < height);
    if (!need_create) {
        return manager->buffer;
    }

    uint64_t modifier[2] = { DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID };
    struct wlr_drm_format format = { DRM_FORMAT_ARGB8888, 2, 2, modifier };
    struct wlr_buffer *wlr_buffer =
        wlr_allocator_create_buffer(manager->allocator, width, height, &format);
    if (!wlr_buffer) {
        return NULL;
    }

    return wlr_buffer;
}

static int screenshot_fullscreen(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    if (manager->taking_screenshot) {
        const sd_bus_error error = SD_BUS_ERROR_MAKE_CONST(
            "org.ukui.kwin.Screenshot.Error.AlreadyTaking", "A screenshot is already been taken");
        return sd_bus_reply_method_error(msg, &error);
    }

    /* if no layout output in layout */
    if (wl_list_empty(&manager->layout->outputs)) {
        return 0;
    }

    uint32_t overlay_cursor = 0;
    // CK(sd_bus_message_read(msg, "b", &overlay_cursor));

    struct wlr_box geo;
    wlr_output_layout_get_box(manager->layout, NULL, &geo);

    struct wlr_buffer *buffer = screenshot_create_buffer(geo.width, geo.height);
    if (!buffer) {
        const sd_bus_error error =
            SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_NO_MEMORY, "Alloc buffer failed");
        return sd_bus_reply_method_error(msg, &error);
    }

    if (manager->buffer != buffer) {
        wlr_buffer_drop(manager->buffer);
        manager->buffer = buffer;
    }

    manager->geo = geo;
    manager->msg = sd_bus_message_ref(msg);
    manager->taking_screenshot = true;

    uint32_t layout_output_count = 0;
    struct wlr_output_layout_output *l_output;
    wl_list_for_each(l_output, &manager->layout->outputs, link) {
        screenshot_output_create(l_output, !!overlay_cursor);
        layout_output_count++;
    }

    /* clear buffer */
    if (layout_output_count > 1) {
        wlr_renderer_begin_with_buffer(manager->renderer, manager->buffer);
        wlr_renderer_clear(manager->renderer, (float[4]){ 0.f, 0.f, 0.f, 0.f });
        wlr_renderer_end(manager->renderer);
    }

    // kywc_log(KYWC_INFO, "screenshot fullscreen start: (%d, %d) %d x %d cursor %d", geo.x, geo.y,
    //         geo.width, geo.height, overlay_cursor);

    return 1;
}

static const sd_bus_vtable screenshot_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("screenshotFullscreen", "", "s", screenshot_fullscreen, 0),
    // SD_BUS_METHOD("screenshotFullscreen", "b", "s", screenshot_fullscreen, 0),
    SD_BUS_VTABLE_END,
};

static void handle_config_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->destroy.link);
    wlr_buffer_drop(manager->buffer);

    free(manager);
    manager = NULL;
}

bool ukui_screenshot_create(struct config_manager *config_manager)
{
    manager = calloc(1, sizeof(struct screenshot_manager));
    if (!manager) {
        return false;
    }

    struct config *config = config_manager_add_config(NULL, registry_bus, registry_path,
                                                      registry_interface, screenshot_vtable, NULL);
    if (!config) {
        free(manager);
        manager = NULL;
        return false;
    }

    wl_list_init(&manager->outputs);
    manager->layout = config_manager->server->layout;
    manager->allocator = config_manager->server->allocator;
    manager->renderer = config_manager->server->renderer;

    manager->destroy.notify = handle_config_destroy;
    wl_signal_add(&config->events.destroy, &manager->destroy);

    return true;
}
