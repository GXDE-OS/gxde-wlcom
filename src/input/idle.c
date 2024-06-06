// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <kywc/log.h>

#include "ext-idle-notify-v1-protocol.h"
#include "idle-protocol.h"

#include "input/seat.h"
#include "input_p.h"
#include "server.h"

#define IDLE_NOTIFIER_VERSION 1
#define ORG_KDE_KWIN_IDLE_VERSION 1

struct idle_manager {
    struct wl_display *display;
    struct wl_global *kde_idle_global;
    struct wl_global *idle_notifier_global;

    struct wl_list idles;
    bool inhibited;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

enum idle_type {
    IDLE_FROM_KDE_IDLE,
    IDLE_FROM_IDLE_NOTIFIER,
    IDLE_FROM_SERVER,
};

struct idle {
    enum idle_type type;
    struct wl_list link;

    uint32_t timeout_ms;
    struct wl_event_source *timer;

    bool idle_state;
    bool support_inhibit;

    struct seat *seat;
    struct wl_listener seat_destroy;

    void (*idle_func)(struct idle *idle, void *data);
    void (*resume_func)(struct idle *idle, void *data);
    void (*destroy_func)(struct idle *idle, void *data);
    void *data; // resource
};

static struct idle_manager *idle_manager = NULL;

static void idle_set_idle(struct idle *idle, bool idle_state)
{
    if (idle->idle_state == idle_state) {
        return;
    }

    if (idle_state) {
        idle->idle_func(idle, idle->data);
    } else {
        idle->resume_func(idle, idle->data);
    }

    idle->idle_state = idle_state;
}

static void idle_notifier_resource_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static int idle_handle_timer(void *data)
{
    struct idle *idle = data;
    idle_set_idle(idle, true);
    return 0;
}

void idle_destroy(struct idle *idle)
{
    if (!idle) {
        return;
    }

    if (idle->destroy_func) {
        idle->destroy_func(idle, idle->data);
    }

    wl_list_remove(&idle->link);
    wl_list_remove(&idle->seat_destroy.link);
    if (idle->timer) {
        wl_event_source_remove(idle->timer);
    }
    if (idle->type != IDLE_FROM_SERVER) {
        wl_resource_set_user_data(idle->data, NULL);
    }

    free(idle);
}

static void idle_reset_timer(struct idle *idle)
{
    if (idle_manager->inhibited && idle->support_inhibit) {
        idle_set_idle(idle, false);
        if (idle->timer) {
            wl_event_source_timer_update(idle->timer, 0);
        }
        return;
    }

    if (idle->timer) {
        wl_event_source_timer_update(idle->timer, idle->timeout_ms);
    } else {
        idle_set_idle(idle, true);
    }
}

static void idle_handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct idle *idle = wl_container_of(listener, idle, seat_destroy);
    idle_destroy(idle);
}

static struct idle *idle_create(enum idle_type type, struct seat *seat, bool support_inhibit,
                                uint32_t timeout, void (*idle_func)(struct idle *idle, void *data),
                                void (*resume_func)(struct idle *idle, void *data),
                                void (*destroy_func)(struct idle *idle, void *data), void *data)
{
    struct idle *idle = calloc(1, sizeof(struct idle));
    if (!idle) {
        return NULL;
    }

    idle->type = type;
    idle->timeout_ms = timeout;
    idle->support_inhibit = support_inhibit;
    idle->seat = seat;

    idle->idle_func = idle_func;
    idle->resume_func = resume_func;
    idle->destroy_func = destroy_func;
    idle->data = data;

    if (timeout > 0) {
        struct wl_event_loop *loop = wl_display_get_event_loop(idle_manager->display);
        idle->timer = wl_event_loop_add_timer(loop, idle_handle_timer, idle);
        if (!idle->timer) {
            free(idle);
            return NULL;
        }
    }

    wl_list_insert(&idle_manager->idles, &idle->link);

    idle->seat_destroy.notify = idle_handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &idle->seat_destroy);

    return idle;
}

static void idle_notification_destroy(struct wl_resource *resource)
{
    struct idle *idle = wl_resource_get_user_data(resource);
    idle_destroy(idle);
}

static const struct ext_idle_notification_v1_interface notification_impl = {
    .destroy = idle_notifier_resource_destroy,
};

static void idle_notification_idle(struct idle *idle, void *data)
{
    struct wl_resource *resource = data;
    ext_idle_notification_v1_send_idled(resource);
}

static void idle_notification_resume(struct idle *idle, void *data)
{
    struct wl_resource *resource = data;
    ext_idle_notification_v1_send_resumed(resource);
}

static void idle_notifier_get_idle_notification(struct wl_client *client,
                                                struct wl_resource *notifier_resource, uint32_t id,
                                                uint32_t timeout, struct wl_resource *seat_resource)
{
    uint32_t version = wl_resource_get_version(notifier_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &ext_idle_notification_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &notification_impl, NULL, idle_notification_destroy);

    struct seat *seat = seat_from_resource(seat_resource);
    if (!seat) {
        return; // leave the resource inert
    }

    struct idle *idle =
        idle_create(IDLE_FROM_IDLE_NOTIFIER, seat, true, timeout, idle_notification_idle,
                    idle_notification_resume, NULL, resource);
    if (!idle) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_user_data(resource, idle);

    idle_reset_timer(idle);
}

static const struct ext_idle_notifier_v1_interface idle_notifier_impl = {
    .destroy = idle_notifier_resource_destroy,
    .get_idle_notification = idle_notifier_get_idle_notification,
};

static void idle_notifier_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &ext_idle_notifier_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &idle_notifier_impl, NULL, NULL);
}

static void idle_timeout_destroy(struct wl_resource *resource)
{
    struct idle *idle = wl_resource_get_user_data(resource);
    idle_destroy(idle);
}

static void idle_timeout_release(struct wl_client *client, struct wl_resource *resource)
{
    idle_timeout_destroy(resource);
}

static void simulate_activity(struct wl_client *client, struct wl_resource *resource)
{
    struct idle *idle = wl_resource_get_user_data(resource);
    if (idle->idle_state) {
        idle->idle_state = false;
        org_kde_kwin_idle_timeout_send_resumed(idle->data);
    }

    if (idle->timer) {
        wl_event_source_timer_update(idle->timer, idle->timeout_ms);
    } else {
        idle_set_idle(idle, true);
    }
}

static const struct org_kde_kwin_idle_timeout_interface idle_timeout_impl = {
    .release = idle_timeout_release,
    .simulate_user_activity = simulate_activity,
};

static void kde_idle_idle(struct idle *idle, void *data)
{
    struct wl_resource *resource = data;
    org_kde_kwin_idle_timeout_send_idle(resource);
}

static void kde_idle_resume(struct idle *idle, void *data)
{
    struct wl_resource *resource = data;
    org_kde_kwin_idle_timeout_send_resumed(resource);
}

static void kde_idle_get_idle_timeout(struct wl_client *client, struct wl_resource *idle_resource,
                                      uint32_t id, struct wl_resource *seat_resource,
                                      uint32_t timeout)
{
    uint32_t version = wl_resource_get_version(idle_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_idle_timeout_interface, version, id);
    if (!resource) {
        wl_resource_post_no_memory(idle_resource);
        return;
    }

    wl_resource_set_implementation(resource, &idle_timeout_impl, NULL, idle_timeout_destroy);

    struct seat *seat = seat_from_resource(seat_resource);
    if (!seat) {
        return;
    }

    struct idle *idle = idle_create(IDLE_FROM_KDE_IDLE, seat, true, timeout, kde_idle_idle,
                                    kde_idle_resume, NULL, resource);
    if (!idle) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_user_data(resource, idle);

    idle_reset_timer(idle);
}

static const struct org_kde_kwin_idle_interface kde_idle_impl = {
    .get_idle_timeout = kde_idle_get_idle_timeout,
};

static void kde_idle_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_idle_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kde_idle_impl, NULL, NULL);
}

static void handle_display_destory(struct wl_listener *listener, void *data)
{
    wl_list_remove(&idle_manager->display_destroy.link);

    if (idle_manager->idle_notifier_global) {
        wl_global_destroy(idle_manager->idle_notifier_global);
    }
    if (idle_manager->kde_idle_global) {
        wl_global_destroy(idle_manager->kde_idle_global);
    }
}

static void handle_server_destory(struct wl_listener *listener, void *data)
{
    wl_list_remove(&idle_manager->server_destroy.link);
    free(idle_manager);
}

bool idle_manager_create(struct server *server)
{
    idle_manager = calloc(1, sizeof(struct idle_manager));
    if (!idle_manager) {
        return false;
    }

    idle_manager->display_destroy.notify = handle_display_destory;
    wl_display_add_destroy_listener(server->display, &idle_manager->display_destroy);
    idle_manager->server_destroy.notify = handle_server_destory;
    server_add_destroy_listener(server, &idle_manager->server_destroy);

    idle_manager->idle_notifier_global =
        wl_global_create(server->display, &ext_idle_notifier_v1_interface, IDLE_NOTIFIER_VERSION,
                         idle_manager, idle_notifier_bind);
    if (!idle_manager->idle_notifier_global) {
        kywc_log(KYWC_WARN, "%s global create failed", ext_idle_notifier_v1_interface.name);
    }

    idle_manager->idle_notifier_global =
        wl_global_create(server->display, &org_kde_kwin_idle_interface, ORG_KDE_KWIN_IDLE_VERSION,
                         idle_manager, kde_idle_bind);
    if (!idle_manager->idle_notifier_global) {
        kywc_log(KYWC_WARN, "%s global create failed", org_kde_kwin_idle_interface.name);
    }

    idle_manager->display = server->display;
    wl_list_init(&idle_manager->idles);

    return true;
}

void idle_manager_set_inhibited(bool inhibited)
{
    if (idle_manager->inhibited == inhibited) {
        return;
    }
    idle_manager->inhibited = inhibited;

    struct idle *idle;
    wl_list_for_each(idle, &idle_manager->idles, link) {
        if (idle->support_inhibit) {
            idle_reset_timer(idle);
        }
    }
}

void idle_manager_notify_activity(struct seat *seat)
{
    struct idle *idle;
    wl_list_for_each(idle, &idle_manager->idles, link) {
        if (idle->support_inhibit && idle_manager->inhibited) {
            continue;
        }

        if (idle->seat == seat) {
            idle_set_idle(idle, false);
            idle_reset_timer(idle);
        }
    }
}

struct idle *idle_manager_add_idle(struct seat *seat, bool support_inhibit, uint32_t timeout,
                                   void (*idle_func)(struct idle *idle, void *data),
                                   void (*resume_func)(struct idle *idle, void *data),
                                   void (*destroy_func)(struct idle *idle, void *data), void *data)
{
    struct idle *idle = idle_create(IDLE_FROM_SERVER, seat, support_inhibit, timeout, idle_func,
                                    resume_func, destroy_func, data);
    if (!idle) {
        return NULL;
    }

    idle_reset_timer(idle);
    return idle;
}
