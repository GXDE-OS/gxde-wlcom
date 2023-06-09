#ifndef _WORKSPACE_H_
#define _WORKSPACE_H_

#include "view.h"

struct workspace {
    /* user readable descriptive name */
    const char *name;
    /* layers in workspacec, below, nornal and above */
    struct view_layer layers[3];

    struct wl_list views;

    uint32_t position;
    bool activated;

    struct {
        // TODO: name and position
        struct wl_signal activate;
        struct wl_signal destroy;
    } events;
};

bool workspace_manager_create(struct view_manager *view_manager);

void workspace_manager_add_new_listener(struct wl_listener *listener);

void workspace_manager_set_rows(uint32_t rows);

uint32_t workspace_manager_get_rows(void);

struct workspace *workspace_manager_get_current(void);

struct workspace *workspace_create(const char *name, uint32_t position);

void workspace_destroy(struct workspace *workspace);

void workspace_activate(struct workspace *workspace);

struct view_layer *workspace_layer(struct workspace *workspace, enum layer layer);

#endif /* _WORKSPACE_H_ */
