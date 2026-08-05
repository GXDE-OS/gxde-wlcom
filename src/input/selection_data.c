// SPDX-FileCopyrightText: 2026 CharOfString <root@charofstring.cc>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>

#include <kywc/log.h>

#include "input_p.h"
#include "server.h"

struct selection_writer {
    const char *data;
    size_t size;
    size_t offset;

    int fd;
    struct wl_event_source *event;
    struct wl_list link; /* selection_writers_add 传入的链表 */
};

struct selection_data {
    struct wlr_data_source base;
    struct wl_event_loop *loop;

    void *data;
    size_t size;

    struct wl_list writers; /* struct selection_writer.link */
};

static void selection_writer_destroy(struct selection_writer *writer)
{
    if (writer->event) {
        wl_event_source_remove(writer->event);
    }
    close(writer->fd);
    wl_list_remove(&writer->link);
    free(writer);
}

static bool selection_writer_flush(struct selection_writer *writer)
{
    while (writer->offset < writer->size) {
        ssize_t n = write(writer->fd, writer->data + writer->offset, writer->size - writer->offset);
        if (n > 0) {
            writer->offset += n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && errno == EAGAIN) {
            return false;
        }
        break;
    }

    return true;
}

static int handle_writable(int fd, uint32_t mask, void *data)
{
    struct selection_writer *writer = data;

    if (selection_writer_flush(writer) || (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))) {
        selection_writer_destroy(writer);
    }

    return 0;
}

bool selection_writers_add(struct wl_list *writers, struct wl_event_loop *loop, int fd,
                           const void *data, size_t size)
{
    struct selection_writer *writer = calloc(1, sizeof(struct selection_writer));
    if (!writer) {
        close(fd);
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    writer->data = data;
    writer->size = size;
    writer->fd = fd;
    wl_list_insert(writers, &writer->link);

    if (selection_writer_flush(writer)) {
        selection_writer_destroy(writer);
        return true;
    }

    writer->event = wl_event_loop_add_fd(loop, fd, WL_EVENT_WRITABLE, handle_writable, writer);
    if (!writer->event) {
        selection_writer_destroy(writer);
        return false;
    }

    return true;
}

void selection_writers_finish(struct wl_list *writers)
{
    struct selection_writer *writer, *tmp;
    wl_list_for_each_safe(writer, tmp, writers, link) {
        selection_writer_destroy(writer);
    }
}

static void selection_data_send(struct wlr_data_source *source, const char *mime_type, int32_t fd)
{
    struct selection_data *selection = wl_container_of(source, selection, base);

    if (!selection_writers_add(&selection->writers, selection->loop, fd, selection->data,
                               selection->size)) {
        kywc_log(KYWC_WARN, "clipboard: failed to send %s", mime_type);
    }
}

static void selection_data_destroy(struct wlr_data_source *source)
{
    struct selection_data *selection = wl_container_of(source, selection, base);

    selection_writers_finish(&selection->writers);
    free(selection->data);
    free(selection);
}

static const struct wlr_data_source_impl selection_data_impl = {
    .send = selection_data_send,
    .destroy = selection_data_destroy,
};

bool selection_source_is_compositor(struct wlr_data_source *source)
{
    return source && source->impl == &selection_data_impl;
}

bool seat_set_selection_data(struct seat *seat, const char **mime_types, size_t n_mime_types,
                             void *data, size_t size)
{
    if (!seat || !mime_types || n_mime_types == 0 || !data || size == 0) {
        return false;
    }

    struct selection_data *selection = calloc(1, sizeof(struct selection_data));
    if (!selection) {
        return false;
    }

    struct wl_display *display = seat->manager->server->display;
    wlr_data_source_init(&selection->base, &selection_data_impl);
    selection->loop = wl_display_get_event_loop(display);
    selection->data = data;
    selection->size = size;
    wl_list_init(&selection->writers);

    for (size_t i = 0; i < n_mime_types; i++) {
        char **dst = wl_array_add(&selection->base.mime_types, sizeof(char *));
        if (!dst) {
            break;
        }
        *dst = strdup(mime_types[i]);
        if (!*dst) {
            selection->base.mime_types.size -= sizeof(char *);
            break;
        }
    }

    if (selection->base.mime_types.size == 0) {
        selection->data = NULL;
        wlr_data_source_destroy(&selection->base);
        return false;
    }

    wlr_seat_set_selection(seat->wlr_seat, &selection->base, wl_display_next_serial(display));

    return true;
}
