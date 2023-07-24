#include <stdlib.h>

#include <wlr/types/wlr_output.h>

#include "output.h"
#include "widget/scaled_buffer.h"

struct scaled_buffer {
    struct ky_scene_buffer *base;
    struct wl_listener node_destroy;

    /* output links */
    struct wl_list outputs;
    struct wl_listener output_enter;
    struct wl_listener output_leave;

    scaled_buffer_update_func_t update;
    scaled_buffer_destroy_func_t destroy;
    void *data;

    /* current buffer scale */
    float scale;
};

struct scaled_output {
    struct wl_list link;
    struct scaled_buffer *buffer;

    struct wlr_output *wlr_output;
    struct wl_listener output_scale;
};

static void scaled_buffer_update(struct scaled_buffer *buffer)
{
    if (wl_list_empty(&buffer->outputs)) {
        return;
    }

    float scale = 0.0;

    struct scaled_output *output;
    /* find max scale in current output list */
    wl_list_for_each(output, &buffer->outputs, link) {
        if (scale < output->wlr_output->scale) {
            scale = output->wlr_output->scale;
        }
    }

    /* no need to redraw if max scale not changed.
     * wlr_scene_buffer_set_buffer will emit output_enter, so loop:
     *  update_buffer -> wlr_scene_buffer_set_buffer ->
     *    output_enter -> update_buffer -> return early here
     */
    if (scale == buffer->scale) {
        return;
    }

    buffer->scale = scale;
    if (buffer->update) {
        buffer->update(buffer->base, buffer->scale, buffer->data);
    }
}

static void handle_output_scale(struct wl_listener *listener, void *data)
{
    struct scaled_output *output = wl_container_of(listener, output, output_scale);
    scaled_buffer_update(output->buffer);
}

static struct scaled_output *scaled_output_create(struct scaled_buffer *buffer,
                                                  struct wlr_output *wlr_output)
{
    /* create output and insert to output list */
    struct scaled_output *output = calloc(1, sizeof(struct scaled_output));
    if (!output) {
        return NULL;
    }

    output->buffer = buffer;
    output->wlr_output = wlr_output;
    wl_list_insert(&buffer->outputs, &output->link);

    /* update buffer if output scale changed */
    struct kywc_output *kywc_output = &output_from_wlr_output(wlr_output)->base;
    output->output_scale.notify = handle_output_scale;
    wl_signal_add(&kywc_output->events.scale, &output->output_scale);

    return output;
}

static void scaled_output_destroy(struct scaled_output *output)
{
    wl_list_remove(&output->link);
    wl_list_remove(&output->output_scale.link);
    free(output);
}

static void handle_output_enter(struct wl_listener *listener, void *data)
{
    struct scaled_buffer *buffer = wl_container_of(listener, buffer, output_enter);
    struct ky_scene_output *scene_output = data;
    struct wlr_output *wlr_output = ky_scene_output_get_output(scene_output);

    if (scaled_output_create(buffer, wlr_output)) {
        scaled_buffer_update(buffer);
    }
}

static struct scaled_output *scaled_output_from_wlr_output(struct scaled_buffer *buffer,
                                                           struct wlr_output *wlr_output)
{
    struct scaled_output *output;
    wl_list_for_each(output, &buffer->outputs, link) {
        if (output->wlr_output == wlr_output) {
            return output;
        }
    }
    return NULL;
}

/* if buffer leave this output, or this output is plug-out or disabled */
static void handle_output_leave(struct wl_listener *listener, void *data)
{
    struct scaled_buffer *buffer = wl_container_of(listener, buffer, output_leave);
    struct ky_scene_output *scene_output = data;
    struct wlr_output *wlr_output = ky_scene_output_get_output(scene_output);

    struct scaled_output *output = scaled_output_from_wlr_output(buffer, wlr_output);
    if (!output) {
        return;
    }

    scaled_output_destroy(output);
    scaled_buffer_update(buffer);
}

static void handle_node_destroy(struct wl_listener *listener, void *data)
{
    struct scaled_buffer *buffer = wl_container_of(listener, buffer, node_destroy);

    wl_list_remove(&buffer->node_destroy.link);
    wl_list_remove(&buffer->output_enter.link);
    wl_list_remove(&buffer->output_leave.link);

    /* destroy outputs */
    struct scaled_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &buffer->outputs, link) {
        scaled_output_destroy(output);
    }

    if (buffer->destroy) {
        buffer->destroy(buffer->base, buffer->data);
    }

    /* scene_buffer is destroyed in node_destroy */
    free(buffer);
}

struct ky_scene_buffer *scaled_buffer_create(struct ky_scene_tree *parent, float scale,
                                             scaled_buffer_update_func_t update,
                                             scaled_buffer_destroy_func_t destroy, void *data)
{
    struct scaled_buffer *buffer = calloc(1, sizeof(struct scaled_buffer));
    if (!buffer) {
        return NULL;
    }

    buffer->scale = scale;
    buffer->update = update;
    buffer->destroy = destroy;
    buffer->data = data;

    wl_list_init(&buffer->outputs);
    buffer->base = ky_scene_buffer_create(parent, NULL);

    /* add output enter and leave listener */
    buffer->output_enter.notify = handle_output_enter;
    ky_scene_buffer_add_output_enter_listener(buffer->base, &buffer->output_enter);
    buffer->output_leave.notify = handle_output_leave;
    ky_scene_buffer_add_output_leave_listener(buffer->base, &buffer->output_leave);

    /* do something when node destroy */
    buffer->node_destroy.notify = handle_node_destroy;
    ky_scene_node_add_destroy_listener(ky_scene_node_from_buffer(buffer->base),
                                       &buffer->node_destroy);

    return buffer->base;
}
