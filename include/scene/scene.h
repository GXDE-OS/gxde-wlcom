// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCENE_H_
#define _SCENE_H_

#define SCENE_API static __attribute__((unused)) inline

#if HAVE_WLR_SCENE // redefine all wlr-scene api

#include <wlr/types/wlr_scene.h>

// clang-format off

#define ky_scene wlr_scene
#define ky_scene_node wlr_scene_node
#define ky_scene_tree wlr_scene_tree
#define ky_scene_rect wlr_scene_rect
#define ky_scene_buffer wlr_scene_buffer
#define ky_scene_output wlr_scene_output
#define ky_scene_output_state_options wlr_scene_output_state_options
#define ky_scene_output_sample_event wlr_scene_output_sample_event
#define ky_scene_output_layout wlr_scene_output_layout 

#define ky_scene_node_destroy wlr_scene_node_destroy
#define ky_scene_node_set_enabled wlr_scene_node_set_enabled
#define ky_scene_node_set_position wlr_scene_node_set_position
#define ky_scene_node_place_above wlr_scene_node_place_above
#define ky_scene_node_place_below wlr_scene_node_place_below
#define ky_scene_node_raise_to_top wlr_scene_node_raise_to_top
#define ky_scene_node_lower_to_bottom wlr_scene_node_lower_to_bottom
#define ky_scene_node_reparent wlr_scene_node_reparent
#define ky_scene_node_coords wlr_scene_node_coords
#define ky_scene_node_at wlr_scene_node_at
#define ky_scene_tree_create wlr_scene_tree_create
#define ky_scene_rect_create wlr_scene_rect_create
#define ky_scene_rect_set_size wlr_scene_rect_set_size
#define ky_scene_rect_set_color wlr_scene_rect_set_color
#define ky_scene_rect_from_node wlr_scene_rect_from_node
#define ky_scene_buffer_create wlr_scene_buffer_create
#define ky_scene_buffer_set_buffer wlr_scene_buffer_set_buffer
#define ky_scene_buffer_set_buffer_with_damage wlr_scene_buffer_set_buffer_with_damage
#define ky_scene_buffer_set_opaque_region wlr_scene_buffer_set_opaque_region
#define ky_scene_buffer_set_source_box wlr_scene_buffer_set_source_box
#define ky_scene_buffer_set_dest_size wlr_scene_buffer_set_dest_size
#define ky_scene_buffer_set_transform wlr_scene_buffer_set_transform
#define ky_scene_attach_output_layout wlr_scene_attach_output_layout
#define ky_scene_get_scene_output wlr_scene_get_scene_output
#define ky_scene_output_commit wlr_scene_output_commit
#define ky_scene_output_send_frame_done wlr_scene_output_send_frame_done
#define ky_scene_set_presentation wlr_scene_set_presentation
#define ky_scene_buffer_point_accepts_input_func_t wlr_scene_buffer_point_accepts_input_func_t
#define ky_scene_output_create wlr_scene_output_create
#define ky_scene_output_layout_add_output wlr_scene_output_layout_add_output
#define ky_scene_node_set_bypassed wlr_scene_node_set_bypassed
#define ky_scene_output_destroy wlr_scene_output_destroy

struct ky_scene *ky_scene_from_node(struct ky_scene_node *node);
SCENE_API struct ky_scene_buffer *ky_scene_buffer_from_node(struct ky_scene_node *node) { return node->type == WLR_SCENE_NODE_BUFFER ? wlr_scene_buffer_from_node(node) : NULL; };
SCENE_API struct ky_scene *ky_scene_create(void) { return wlr_scene_create(); } 
SCENE_API struct wlr_presentation *ky_scene_get_presentation(struct ky_scene *scene) { return scene->presentation; }
SCENE_API void ky_scene_destroy(struct ky_scene *scene) { scene ? ky_scene_node_destroy(&scene->tree.node) : true; }
SCENE_API struct ky_scene_node *ky_scene_node_from_scene(struct ky_scene *scene) { return &scene->tree.node; }
SCENE_API void *ky_scene_node_get_user_data(struct ky_scene_node *node) { return node->data; };
SCENE_API void ky_scene_node_set_user_data(struct ky_scene_node *node, void *user_data) { node->data = user_data; };
SCENE_API void ky_scene_node_get_position(struct ky_scene_node *node, int *x, int *y) { *x = node->x; *y = node->y; }
SCENE_API void ky_scene_node_add_destroy_listener(struct ky_scene_node *node, struct wl_listener *listener) { wl_signal_add(&node->events.destroy, listener); }
SCENE_API struct wlr_addon_set *ky_scene_node_get_addon_set(struct ky_scene_node *node) { return &node->addons; }
SCENE_API struct ky_scene_tree *ky_scene_create_tree(struct ky_scene *scene) { return ky_scene_tree_create(&scene->tree); }
SCENE_API struct ky_scene_tree *ky_scene_node_get_parent(struct ky_scene_node *node) { return node->parent; }
SCENE_API struct ky_scene_node *ky_scene_node_from_tree(struct ky_scene_tree *tree) { return &tree->node; }
SCENE_API struct ky_scene_node *ky_scene_node_from_buffer(struct ky_scene_buffer *buffer) { return &buffer->node; }
SCENE_API struct ky_scene_node *ky_scene_node_from_rect(struct ky_scene_rect *rect) { return &rect->node; }
SCENE_API struct wlr_output *ky_scene_output_get_output(struct ky_scene_output *output) { return output->output; }
SCENE_API struct ky_scene_output *ky_scene_buffer_get_primary_output(struct ky_scene_buffer *scene_buffer) { return scene_buffer->primary_output; }
SCENE_API struct wlr_buffer *ky_scene_buffer_get_buffer(struct ky_scene_buffer *scene_buffer) { return scene_buffer->buffer; }
SCENE_API void ky_scene_buffer_set_point_accepts_input(struct ky_scene_buffer *scene_buffer, ky_scene_buffer_point_accepts_input_func_t point_accepts_input) { scene_buffer->point_accepts_input = point_accepts_input; }
SCENE_API void ky_scene_buffer_add_outputs_update_listener(struct ky_scene_buffer *scene_buffer, struct wl_listener *listener) { wl_signal_add(&scene_buffer->events.outputs_update, listener); }
SCENE_API void ky_scene_buffer_add_output_enter_listener(struct ky_scene_buffer *scene_buffer, struct wl_listener *listener) { wl_signal_add(&scene_buffer->events.output_enter, listener); }
SCENE_API void ky_scene_buffer_add_output_leave_listener(struct ky_scene_buffer *scene_buffer, struct wl_listener *listener) { wl_signal_add(&scene_buffer->events.output_leave, listener); }
SCENE_API void ky_scene_buffer_add_output_sample_listener(struct ky_scene_buffer *scene_buffer, struct wl_listener *listener) { wl_signal_add(&scene_buffer->events.output_sample, listener); }
SCENE_API struct ky_scene_output *ky_scene_output_sample_event_output(const struct ky_scene_output_sample_event *event) { return event->output; }
SCENE_API bool ky_scene_output_sample_event_direct_scanout(const struct ky_scene_output_sample_event *event) { return event->direct_scanout; }
SCENE_API void ky_scene_buffer_add_frame_done_listener(struct ky_scene_buffer *scene_buffer, struct wl_listener *listener) { wl_signal_add(&scene_buffer->events.frame_done, listener); }

// clang-format on

#else

// clang-format off

#endif

// clang-format on

#endif /* _SCENE_H_ */
