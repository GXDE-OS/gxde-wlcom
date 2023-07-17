#ifndef _SCENE_SURFACE_H_
#define _SCENE_SURFACE_H_

#include "scene.h"

struct ky_scene_surface {
    struct ky_scene_buffer *buffer;
    struct wlr_surface *surface;

    // private state

    struct wlr_addon addon;
    struct wlr_addon node_addon;

    struct wl_listener outputs_update;
    struct wl_listener output_enter;
    struct wl_listener output_leave;
    struct wl_listener output_sample;
    struct wl_listener frame_done;
    struct wl_listener surface_destroy;
    struct wl_listener surface_commit;
};

struct ky_scene_tree *ky_scene_subsurface_tree_create(struct ky_scene_tree *parent,
                                                      struct wlr_surface *surface);

struct ky_scene_surface *ky_scene_surface_create(struct ky_scene_tree *parent,
                                                 struct wlr_surface *wlr_surface);

struct ky_scene_surface *ky_scene_surface_try_from_buffer(struct ky_scene_buffer *scene_buffer);

struct wlr_surface *wlr_surface_try_from_node(struct ky_scene_node *node);

struct ky_scene_buffer *wlr_scene_buffer_try_from_surface(struct wlr_surface *wlr_surface);

#endif /* _SCENE_SURFACE_H_ */
