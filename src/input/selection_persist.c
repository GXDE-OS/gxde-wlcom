// SPDX-FileCopyrightText: 2026 GXDE OS Team
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _DEFAULT_SOURCE
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

/* 单次剪贴板内容的缓存上限, 超过就放弃该条目, 避免合成器被超大内容拖垮 */
#define PERSIST_MAX_SIZE (64 * 1024 * 1024)
#define PERSIST_READ_CHUNK 16384

struct persist_selection;

/* 缓存下来的一种 mime 类型 */
struct persist_entry {
    struct persist_selection *persist;
    char *mime_type;
    char *data;
    size_t size;
    size_t cap;
    bool complete;
    bool failed;

    int fd; /* 管道读端, 读完为 -1 */
    struct wl_event_source *event;

    struct wl_list writers; /* struct selection_writer.link */
    struct wl_list link;    /* persist_selection.entries */
};

struct persist_selection {
    struct wlr_data_source base; /* 接管选区后由合成器持有的源 */
    struct persist_manager *manager;

    struct wl_list entries; /* struct persist_entry.link */
    size_t total;
    bool owned; /* 已经作为选区安装 */

    struct wl_event_source *install;
};

/* 剪贴板持久化, 每个 seat 一份 */
struct persist_manager {
    struct seat *seat;
    struct wl_display *display;
    struct wl_event_loop *loop;

    struct persist_selection *cache;

    struct wl_listener set_selection;
    struct wl_listener seat_destroy;
};

static const struct wlr_data_source_impl persist_source_impl;

static void persist_entry_finish_read(struct persist_entry *entry)
{
    if (entry->event) {
        wl_event_source_remove(entry->event);
        entry->event = NULL;
    }
    if (entry->fd >= 0) {
        close(entry->fd);
        entry->fd = -1;
    }
}

static void persist_entry_destroy(struct persist_entry *entry)
{
    selection_writers_finish(&entry->writers);

    persist_entry_finish_read(entry);
    wl_list_remove(&entry->link);
    free(entry->mime_type);
    free(entry->data);
    free(entry);
}

/**
 * 尽可能把管道里的数据读进缓存, 读到 EOF 才算完整。
 * 只标记条目失败, 不会销毁条目本身, 调用方拿到的指针始终有效。
 */
static void persist_entry_drain(struct persist_entry *entry)
{
    struct persist_selection *persist = entry->persist;

    if (entry->fd < 0) {
        return;
    }

    while (true) {
        if (entry->size == entry->cap) {
            size_t cap = entry->cap ? entry->cap * 2 : PERSIST_READ_CHUNK;
            if (persist->total - entry->size + cap > PERSIST_MAX_SIZE) {
                kywc_log(KYWC_DEBUG, "clipboard: %s exceeds cache limit, give up", entry->mime_type);
                entry->failed = true;
                persist_entry_finish_read(entry);
                return;
            }
            char *data = realloc(entry->data, cap);
            if (!data) {
                entry->failed = true;
                persist_entry_finish_read(entry);
                return;
            }
            persist->total += cap - entry->cap;
            entry->data = data;
            entry->cap = cap;
        }

        ssize_t n = read(entry->fd, entry->data + entry->size, entry->cap - entry->size);
        if (n > 0) {
            entry->size += n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && errno == EAGAIN) {
            /* 数据还没写完, 等下一次可读 */
            return;
        }

        entry->complete = n == 0;
        entry->failed = n < 0;
        persist_entry_finish_read(entry);
        return;
    }
}

static int handle_readable(int fd, uint32_t mask, void *data)
{
    persist_entry_drain(data);
    return 0;
}

static bool mime_type_ignored(const char *mime_type)
{
    /* XWayland 桥接出来的 X11 伪 target, 缓存它们没有意义 */
    static const char *ignored[] = {
        "TARGETS", "MULTIPLE", "SAVE_TARGETS", "TIMESTAMP", "DELETE", "INSERT_SELECTION",
        "INSERT_PROPERTY",
    };

    for (size_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++) {
        if (!strcmp(mime_type, ignored[i])) {
            return true;
        }
    }

    return false;
}

static void persist_entry_create(struct persist_selection *persist, struct wlr_data_source *source,
                                 const char *mime_type)
{
    int fds[2];
    if (pipe(fds) < 0) {
        kywc_log(KYWC_WARN, "clipboard: failed to create pipe for %s", mime_type);
        return;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    /* 写端交给客户端, 保持阻塞语义, 只有读端归合成器 */
    int flags = fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    }

    struct persist_entry *entry = calloc(1, sizeof(struct persist_entry));
    if (!entry) {
        close(fds[0]);
        close(fds[1]);
        return;
    }

    entry->persist = persist;
    entry->mime_type = strdup(mime_type);
    entry->fd = fds[0];
    wl_list_init(&entry->writers);
    if (!entry->mime_type) {
        entry->fd = -1;
        close(fds[0]);
        close(fds[1]);
        wl_list_insert(&persist->entries, &entry->link);
        persist_entry_destroy(entry);
        return;
    }

    entry->event = wl_event_loop_add_fd(persist->manager->loop, fds[0], WL_EVENT_READABLE,
                                        handle_readable, entry);
    wl_list_insert(&persist->entries, &entry->link);

    /* wlroots 把写端交给客户端后会自行关闭它 */
    wlr_data_source_send(source, mime_type, fds[1]);
}

static struct persist_selection *persist_selection_create(struct persist_manager *manager,
                                                          struct wlr_data_source *source)
{
    struct persist_selection *persist = calloc(1, sizeof(struct persist_selection));
    if (!persist) {
        return NULL;
    }

    wlr_data_source_init(&persist->base, &persist_source_impl);
    persist->manager = manager;
    wl_list_init(&persist->entries);

    char **mime_type;
    wl_array_for_each(mime_type, &source->mime_types) {
        if (!mime_type_ignored(*mime_type)) {
            persist_entry_create(persist, source, *mime_type);
        }
    }

    if (wl_list_empty(&persist->entries)) {
        wlr_data_source_destroy(&persist->base);
        return NULL;
    }

    return persist;
}

static struct persist_entry *persist_find_entry(struct persist_selection *persist,
                                                const char *mime_type)
{
    struct persist_entry *entry;
    wl_list_for_each(entry, &persist->entries, link) {
        if (!strcmp(entry->mime_type, mime_type)) {
            return entry;
        }
    }

    return NULL;
}

static void persist_source_send(struct wlr_data_source *source, const char *mime_type, int32_t fd)
{
    struct persist_selection *persist = wl_container_of(source, persist, base);

    struct persist_entry *entry = persist->manager ? persist_find_entry(persist, mime_type) : NULL;
    if (!entry) {
        close(fd);
        return;
    }

    /* 选区安装前缓存就已经读完了, entry->data 在写出期间不会再变 */
    selection_writers_add(&entry->writers, persist->manager->loop, fd, entry->data, entry->size);
}

static void persist_source_destroy(struct wlr_data_source *source)
{
    struct persist_selection *persist = wl_container_of(source, persist, base);

    if (persist->install) {
        wl_event_source_remove(persist->install);
    }
    /* seat 先于 wlr_seat 销毁, manager 可能已经先走一步 */
    if (persist->manager && persist->manager->cache == persist) {
        persist->manager->cache = NULL;
    }

    struct persist_entry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, &persist->entries, link) {
        persist_entry_destroy(entry);
    }

    free(persist);
}

static const struct wlr_data_source_impl persist_source_impl = {
    .send = persist_source_send,
    .destroy = persist_source_destroy,
};

/**
 * 源客户端已经退出, 由合成器接管选区。
 * 放在 idle 里执行, 避免在 set_selection 派发过程中重入。
 */
static void handle_install(void *data)
{
    struct persist_selection *persist = data;
    struct persist_manager *manager = persist->manager;
    struct wlr_seat *wlr_seat = manager->seat->wlr_seat;

    persist->install = NULL;

    /* 这期间已经有新的持有者了 */
    if (wlr_seat->selection_source) {
        return;
    }

    /* 客户端退出时写端随之关闭, 收尾读一次, 把管道里剩下的数据取干净 */
    struct persist_entry *entry, *tmp;
    wl_list_for_each_safe(entry, tmp, &persist->entries, link) {
        persist_entry_drain(entry);
        if (!entry->complete || entry->failed || entry->size == 0) {
            persist_entry_destroy(entry);
        }
    }

    if (wl_list_empty(&persist->entries)) {
        wlr_data_source_destroy(&persist->base);
        return;
    }

    size_t count = 0;
    wl_list_for_each(entry, &persist->entries, link) {
        char **dst = wl_array_add(&persist->base.mime_types, sizeof(char *));
        if (!dst) {
            break;
        }
        *dst = strdup(entry->mime_type);
        if (!*dst) {
            persist->base.mime_types.size -= sizeof(char *);
            break;
        }
        count++;
    }

    if (count == 0) {
        wlr_data_source_destroy(&persist->base);
        return;
    }

    persist->owned = true;
    wlr_seat_set_selection(wlr_seat, &persist->base, wl_display_next_serial(manager->display));
    kywc_log(KYWC_DEBUG, "clipboard: kept %zu mime type(s) of %zu bytes after client exited", count,
             persist->total);
}

static void handle_set_selection(struct wl_listener *listener, void *data)
{
    struct persist_manager *manager = wl_container_of(listener, manager, set_selection);
    struct wlr_data_source *source = manager->seat->wlr_seat->selection_source;

    /* 合成器自己持有的选区, 不需要再缓存一次 */
    if (source && (source->impl == &persist_source_impl || selection_source_is_compositor(source))) {
        return;
    }

    if (!source) {
        /* 选区被清空, 通常意味着持有者退出了, 用缓存接管 */
        struct persist_selection *persist = manager->cache;
        if (persist && !persist->owned && !persist->install) {
            persist->install = wl_event_loop_add_idle(manager->loop, handle_install, persist);
        }
        return;
    }

    if (manager->cache) {
        wlr_data_source_destroy(&manager->cache->base);
    }
    manager->cache = persist_selection_create(manager, source);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct persist_manager *manager = wl_container_of(listener, manager, seat_destroy);

    if (manager->cache) {
        if (manager->cache->owned) {
            /* 已经安装的源归 wlr_seat 管, 这里只解除关联 */
            manager->cache->manager = NULL;
        } else {
            wlr_data_source_destroy(&manager->cache->base);
        }
    }

    wl_list_remove(&manager->set_selection.link);
    wl_list_remove(&manager->seat_destroy.link);
    free(manager);
}

void selection_persist_create(struct seat *seat)
{
    struct persist_manager *manager = calloc(1, sizeof(struct persist_manager));
    if (!manager) {
        return;
    }

    struct wl_display *display = seat->manager->server->display;
    manager->seat = seat;
    manager->display = display;
    manager->loop = wl_display_get_event_loop(display);

    manager->set_selection.notify = handle_set_selection;
    wl_signal_add(&seat->wlr_seat->events.set_selection, &manager->set_selection);
    manager->seat_destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &manager->seat_destroy);
}
