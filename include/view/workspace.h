// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _WORKSPACE_H_
#define _WORKSPACE_H_

#include "view.h"

#define MAX_WORKSPACES 15

struct workspace {
    const char *uuid;
    /* user readable descriptive name */
    const char *name;
    /* layers in workspacec, below, normal and above */
    struct view_layer layers[3];

    struct wl_list view_proxies;
    /* start from 0 */
    uint32_t position;
    bool has_custom_name;
    bool activated;

    struct {
        /* view is shown in this workspace */
        struct wl_signal view_enter;
        /* view is gone or unmapped */
        struct wl_signal view_leave;

        struct wl_signal name;
        struct wl_signal position;
        struct wl_signal activate;
        struct wl_signal destroy;
    } events;
};

struct view_proxy {
    /* insert to view view_proxies */
    struct wl_list view_link;
    /* insert to workspace view_proxies */
    struct wl_list workspace_link;

    struct view *view;
    struct workspace *workspace;
    /* used to mount view tree */
    struct ky_scene_tree *tree;
};

bool workspace_manager_create(struct view_manager *view_manager);

void workspace_manager_add_new_listener(struct wl_listener *listener);

void workspace_manager_set_rows(uint32_t rows);

uint32_t workspace_manager_get_rows(void);

struct workspace *workspace_manager_get_current(void);

uint32_t workspace_manager_get_count(void);

struct workspace *workspace_create(const char *name, uint32_t position);

void workspace_destroy(struct workspace *workspace);

void workspace_activate(struct workspace *workspace);

struct view_layer *workspace_layer(struct workspace *workspace, enum layer layer);

struct workspace *workspace_by_position(uint32_t position);

struct workspace *workspace_by_uuid(const char *uuid);

void workspace_set_position(struct workspace *workspace, uint32_t position);

#endif /* _WORKSPACE_H_ */
