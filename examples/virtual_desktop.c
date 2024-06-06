// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plasma-virtual-desktop-client-protocol.h"

struct virtual_desktop_manager {
    struct org_kde_plasma_virtual_desktop_management *management;
    struct wl_list desktops;

    uint32_t rows;
    int32_t pending_done;
};

struct virtual_desktop {
    struct wl_list link;

    struct virtual_desktop_manager *manager;
    struct org_kde_plasma_virtual_desktop *desktop;

    char *name, *id;
    uint32_t position;
    bool activated;
};

static void desktop_id(void *data, struct org_kde_plasma_virtual_desktop *virtual_desktop,
                       const char *desktop_id)
{
    struct virtual_desktop *desktop = data;

    if (!strcmp(desktop->id, desktop_id)) {
        return;
    }

    free(desktop->id);
    desktop->id = strdup(desktop_id);
}

static void desktop_name(void *data, struct org_kde_plasma_virtual_desktop *virtual_desktop,
                         const char *name)
{
    struct virtual_desktop *desktop = data;

    if (desktop->name && !strcmp(desktop->name, name)) {
        return;
    }

    free(desktop->name);
    desktop->name = strdup(name);
}

static void desktop_activated(void *data, struct org_kde_plasma_virtual_desktop *virtual_desktop)
{
    struct virtual_desktop *desktop = data;

    desktop->activated = true;
}

static void desktop_deactivated(void *data, struct org_kde_plasma_virtual_desktop *virtual_desktop)
{
    struct virtual_desktop *desktop = data;

    desktop->activated = false;
}

static void desktop_done(void *data, struct org_kde_plasma_virtual_desktop *virtual_desktop)
{
    struct virtual_desktop *desktop = data;

    desktop->manager->pending_done--;
}

static void desktop_removed(void *data, struct org_kde_plasma_virtual_desktop *virtual_desktop)
{
    struct virtual_desktop *desktop = data;

    org_kde_plasma_virtual_desktop_destroy(desktop->desktop);
    wl_list_remove(&desktop->link);

    free(desktop->name);
    free(desktop->id);
    free(desktop);
}

static const struct org_kde_plasma_virtual_desktop_listener desktop_listener = {
    .desktop_id = desktop_id,
    .name = desktop_name,
    .activated = desktop_activated,
    .deactivated = desktop_deactivated,
    .done = desktop_done,
    .removed = desktop_removed,
};

static void management_desktop_created(void *data,
                                       struct org_kde_plasma_virtual_desktop_management *management,
                                       const char *desktop_id, uint32_t position)
{
    struct virtual_desktop *desktop = calloc(1, sizeof(struct virtual_desktop));
    if (!desktop) {
        return;
    }

    desktop->desktop =
        org_kde_plasma_virtual_desktop_management_get_virtual_desktop(management, desktop_id);

    struct virtual_desktop_manager *manager = data;
    desktop->id = strdup(desktop_id);
    desktop->position = position;
    desktop->manager = manager;
    wl_list_insert(&manager->desktops, &desktop->link);

    org_kde_plasma_virtual_desktop_add_listener(desktop->desktop, &desktop_listener, desktop);
    manager->pending_done++;
}

static void management_desktop_removed(void *data,
                                       struct org_kde_plasma_virtual_desktop_management *management,
                                       const char *desktop_id)
{
    // do nothing
}

static void management_done(void *data,
                            struct org_kde_plasma_virtual_desktop_management *management)
{
    struct virtual_desktop_manager *manager = data;

    manager->pending_done--;
}

static void management_rows(void *data,
                            struct org_kde_plasma_virtual_desktop_management *management,
                            uint32_t rows)
{
    struct virtual_desktop_manager *manager = data;

    manager->rows = rows;
}

static const struct org_kde_plasma_virtual_desktop_management_listener management_listener = {
    .desktop_created = management_desktop_created,
    .desktop_removed = management_desktop_removed,
    .done = management_done,
    .rows = management_rows,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version)
{
    struct virtual_desktop_manager *manager = data;

    int version_to_bind;

    if (!strcmp(interface, org_kde_plasma_virtual_desktop_management_interface.name)) {
        version_to_bind = version <= 2 ? version : 2;
        manager->management = wl_registry_bind(
            registry, name, &org_kde_plasma_virtual_desktop_management_interface, version_to_bind);

        org_kde_plasma_virtual_desktop_management_add_listener(manager->management,
                                                               &management_listener, manager);
        manager->pending_done++;
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    // do nothing
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void print_workspaces_info(struct virtual_desktop_manager *manager)
{
    printf("%d worskpaces in %d rows\n\n", wl_list_length(&manager->desktops), manager->rows);

    struct virtual_desktop *desktop;
    wl_list_for_each_reverse(desktop, &manager->desktops, link) {
        printf("workspace \"%s\"\n", desktop->id);
        printf("  name: %s\n", desktop->name);
        printf("  position: %d\n", desktop->position);
        printf("  activated: %s\n\n", desktop->activated ? "true" : "false");
    }
}

static const struct option long_options[] = {
    { "help", no_argument, 0, 'h' },
    { "monitor", no_argument, 0, 0 },
    { "create", required_argument, 0, 0 },
    { "desktop", required_argument, 0, 0 },
    { "activate", no_argument, 0, 0 },
    { "remove", no_argument, 0, 0 },
    { 0 },
};

static const char usage[] = "usage: virtual-desktop [option]\n\n"
                            " --help             Show help message and quit.\n"
                            " --monitor          Enter monitor mode\n"
                            " --create <name>    Create virtual desktop with name\n"
                            " --desktop <id>     control virtual desktop with unique id\n"
                            "   --activate       activate this virtual desktop\n"
                            "   --remove         remove this virtual desktop\n";

int main(int argc, char *argv[])
{
    struct virtual_desktop_manager manager = { 0 };
    wl_list_init(&manager.desktops);

    struct wl_display *display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "failed to connect to display\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &manager);
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (!manager.management) {
        fprintf(stderr, "compositor doesn't support kde-plasma-virtual-desktop\n");
        return EXIT_FAILURE;
    }

    while (manager.pending_done > 0) {
        if (wl_display_dispatch(display) < 0) {
            fprintf(stderr, "wl_display_dispatch failed\n");
            return EXIT_FAILURE;
        }
    }

    struct virtual_desktop *current_desktop = NULL;
    bool monitor = false, has_request = false;

    while (1) {
        int option_index = -1;
        int c = getopt_long(argc, argv, "h", long_options, &option_index);
        if (c < 0) {
            break;
        } else if (c == '?') {
            goto done;
        } else if (c == 'h') {
            fprintf(stderr, "%s", usage);
            goto done;
        }

        const char *name = long_options[option_index].name;
        const char *value = optarg;
        if (strcmp(name, "desktop") == 0) {
            bool found = false;
            wl_list_for_each(current_desktop, &manager.desktops, link) {
                if (strcmp(current_desktop->id, value) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "unknown desktop %s\n", value);
                goto done;
            }
        } else if (strcmp(name, "create") == 0) {
            org_kde_plasma_virtual_desktop_management_request_create_virtual_desktop(
                manager.management, value, 0);
            has_request = true;
        } else if (strcmp(name, "monitor") == 0) {
            monitor = true;
        } else { // output sub-option
            if (current_desktop == NULL) {
                fprintf(stderr, "no --desktop specified before --%s\n", name);
                goto done;
            }

            if (strcmp(name, "activate") == 0) {
                if (!current_desktop->activated) {
                    org_kde_plasma_virtual_desktop_request_activate(current_desktop->desktop);
                    has_request = true;
                }
            } else if (strcmp(name, "remove") == 0) {
                org_kde_plasma_virtual_desktop_management_request_remove_virtual_desktop(
                    manager.management, current_desktop->id);
                has_request = true;
            } else {
                fprintf(stderr, "invalid option: %s\n", name);
                goto done;
            }
        }
    }

    if (has_request) {
        /* dispatch events if any */
        wl_display_prepare_read(display);
        wl_display_read_events(display);
        wl_display_dispatch_pending(display);

        wl_display_flush(display);
        wl_display_roundtrip(display);
    } else {
        print_workspaces_info(&manager);
    }

    while (monitor && wl_display_dispatch(display) != -1) {
        // This space intentionally left blank
    }

    struct virtual_desktop *desktop, *tmp;
done:
    wl_list_for_each_safe(desktop, tmp, &manager.desktops, link) {
        desktop_removed(desktop, desktop->desktop);
    }
    org_kde_plasma_virtual_desktop_management_destroy(manager.management);

    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);

    return 0;
}
