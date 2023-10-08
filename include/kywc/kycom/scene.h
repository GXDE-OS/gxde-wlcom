// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYCOM_SCENE_H_
#define _KYCOM_SCENE_H_

#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>

#include "opengl.h"
#include "target.h"

struct server;
struct kywc_node;
struct kywc_node_visitor;
struct kywc_texture_node;
struct kywc_effect_output;
struct kywc_scene_output_layout;

enum kywc_visitor_flag {
    VISITOR_LEAF = 0,
    /* Breadth first traversal */
    VISITOR_BFs = 1,
    /* Depth first traversal */
    VISITOR_DFs = 2,
};

typedef void (*kywc_node_generate_render_task_interface)(const struct kywc_node *node,
                                                         pixman_region32_t *damage,
                                                         struct kywc_render_target *target,
                                                         struct wl_list *render_tasks);

typedef void (*kywc_node_travel_interface)(struct kywc_node *node,
                                           struct kywc_node_visitor *visitor);

typedef void (*kywc_node_push_damage_interface)(struct kywc_node *node,
                                                const struct wlr_box *damage);

typedef void (*kywc_node_get_bounding_box_interface)(const struct kywc_node *node,
                                                     struct wlr_box *box);

typedef void (*kywc_node_destroy_interface)(struct kywc_node *node);

typedef void (*kywc_node_get_opaque_region)(const struct kywc_node *node,
                                            pixman_region32_t *opaque_region);

typedef const char *(*kywc_node_name)(void);

typedef bool (*kywc_texture_node_point_accepts_input_func)(struct kywc_texture_node *buffer, int sx,
                                                           int sy);

struct kywc_node {
    bool enabled;
    int x, y;
    struct wl_list link;
    struct kywc_group_node *parent;

    kywc_node_generate_render_task_interface generate_render_task;
    /* z_order + -> -*/
    kywc_node_travel_interface travel;
    kywc_node_push_damage_interface push_damage;
    /**
     * Bounding box with transform, (w,h) is geometry width,height.
     * Texture-local(surface-local) coordinates.
     * Rect and texture (x,y) = 0.
     */
    kywc_node_get_bounding_box_interface get_bounding_box;
    /**
     * Texture-local(surface-local) coordinates.
     * Rect and texture left_top point(x,y) = 0.
     */
    kywc_node_get_opaque_region get_opaque_region;
    /* Private method, call it by kywc_node_destroy. */
    kywc_node_destroy_interface destroy;
    kywc_node_name node_name;
    void (*visiting_callback)(struct kywc_node *node, struct kywc_node_visitor *visitor);

    void *data;
    /**
     * Read only, inited when the node create in scene.
     * Node in scene who proxy crurrent node.
     * Just using when operation related to node's parent.
     */
    void *proxy_node;
    struct {
        struct wl_signal destroy;
    } events;

    struct wlr_addon_set addons;
};

struct kywc_group_node {
    struct kywc_node node;
    struct wl_list children;
};

struct kywc_root {
    struct kywc_group_node group;
    struct wl_list outputs; // kywc_scene_output.link

    struct wlr_presentation *presentation;

    struct wl_listener presentation_destroy;

    kywc_node_destroy_interface group_destroy;
};

struct kywc_scene_output_sample_event {
    struct kywc_scene_output *output;
    bool direct_scanout;
};

struct kywc_scene_output {
    struct wl_list link;
    int x, y;

    struct kywc_effect_output *effect_output;
    struct wlr_damage_ring damage_ring;
    struct wlr_output *output;
    struct kywc_root *scene;
    struct wlr_addon addon;

    struct {
        struct wl_signal destroy;
    } events;

    uint8_t index;
    bool prev_scanout;

    struct wl_listener output_commit;
    struct wl_listener output_damage;
    struct wl_listener output_needs_frame;

    struct wl_list render_task_list;
};

struct kywc_rect_node {
    struct kywc_node node;

    float color[4];
    int width, height;
};

struct kywc_texture_node {
    struct kywc_node node;
    /* May be NULL */
    struct wlr_buffer *buffer;
    struct kywc_gl_texture *texture;
    int geometry_width, geometry_height;

    kywc_node_destroy_interface base_node_destroy; // kywc_node
    kywc_texture_node_point_accepts_input_func point_accepts_input;

    struct {
        struct wl_signal outputs_update; // wl_array: struct kywc_scene_output*
        struct wl_signal output_enter;   // struct kywc_scene_output
        struct wl_signal output_leave;   // struct kywc_scene_output
        struct wl_signal output_present; // struct kywc_scene_output
        struct wl_signal output_sample;  // kywc_scene_output_sample_event
        struct wl_signal frame_done;     // struct timespec
    } events;

    /**
     * The output that the largest area of this buffer is displayed on.
     * This may be NULL if the buffer is not currently displayed on any
     * outputs. This is the output that should be used for frame callbacks,
     * presentation feedback, etc.
     */
    struct kywc_scene_output *primary_output;

    uint64_t active_outputs;
    struct wlr_fbox src_box;

    enum wl_output_transform transform;
    /* Texture-local(surface-local) coordinates */
    pixman_region32_t opaque_region;
    /* Maybe be empty*/
    pixman_region32_t corner_region;
};

struct kywc_node_visitor_interface {
    void (*visit_node)(struct kywc_node_visitor *visitor, struct kywc_node *node);
    void (*visit_group)(struct kywc_node_visitor *visitor, struct kywc_group_node *group);
    void (*visit_texture)(struct kywc_node_visitor *visitor, struct kywc_texture_node *surface);
    void (*visit_rect)(struct kywc_node_visitor *visitor, struct kywc_rect_node *rect);
};

struct kywc_node_visitor {
    /* abort travel */
    bool travel_break; 
    enum kywc_visitor_flag flag;
    /* Current node ly, ly in scene. */
    int lx, ly;

    const struct kywc_node_visitor_interface *impl;
    /* skip child node */
    bool skip_current_node;
};

struct kywc_root *kywc_root_create_with_server(struct server *kywc_server);

void kywc_root_set_presentation(struct kywc_root *scene, struct wlr_presentation *presentation);

void kywc_node_destroy(struct kywc_node *node);

void kywc_node_set_enabled(struct kywc_node *node, bool enabled);

/* bypassed: Visitor decision */
void kywc_node_set_bypassed(struct kywc_node *node, bool bypassed);

void kywc_node_set_position(struct kywc_node *node, int x, int y);

void kywc_node_place_above(struct kywc_node *node, struct kywc_node *sibling);

void kywc_node_place_below(struct kywc_node *node, struct kywc_node *sibling);

void kywc_node_raise_to_top(struct kywc_node *node);

void kywc_node_lower_to_bottom(struct kywc_node *node);

void kywc_node_reparent_ex(struct kywc_node *node, struct kywc_group_node *new_parent);

void kywc_node_reparent(struct kywc_node *node, struct kywc_group_node *new_parent);

struct kywc_group_node *kywc_node_get_parent(struct kywc_node *node);

bool kywc_node_coords(struct kywc_node *node, int *lx, int *ly);

struct kywc_gl_texture *kywc_node_generate_texture(struct kywc_node *source_node, 
                                                   struct kywc_render_target *target, float scale);

/**
 * No buffer node.
 * Buffer nodes in the kylin's scene are replaced by texture node.
 */

struct kywc_node *kywc_node_at(struct kywc_node *node, double lx, double ly, double *nx,
                               double *ny);

/* Doesn't add damage region*/
void kywc_node_init(struct kywc_node *node);

/* Doesn't add damage region*/
void kywc_node_add(struct kywc_node *node, struct kywc_group_node *parent);

void kywc_scene_node_render(struct kywc_node *node, struct kywc_render_target *target,
                            pixman_region32_t *damage);

/*************************************************************************/
struct kywc_group_node *kywc_group_node_from_node(const struct kywc_node *node);

struct kywc_group_node *kywc_group_node_create(struct kywc_group_node *parent);

/* Doesn't add damage region*/
void kywc_group_node_init(struct kywc_group_node *group);

/* Doesn't add damage region*/
void kywc_group_node_add(struct kywc_group_node *group, struct kywc_group_node *parent);

void kywc_group_node_children_generate_render(const struct kywc_group_node *group,
                                              pixman_region32_t *damage,
                                              struct kywc_render_target *target,
                                              struct wl_list *render_tasks);

kywc_node_generate_render_task_interface
kywc_group_node_set_generate_func(struct kywc_group_node *group,
                                  kywc_node_generate_render_task_interface generate_func);

kywc_node_travel_interface kywc_group_node_set_travel_func(struct kywc_group_node *group,
                                                           kywc_node_travel_interface travel_func);

void kywc_scene_output_destroy(struct kywc_scene_output *scene_output);

struct kywc_scene_output *kywc_scene_output_create(struct kywc_root *scene,
                                                   struct wlr_output *output);

void kywc_scene_output_layout_add_output(struct kywc_scene_output_layout *sol,
                                         struct wlr_output_layout_output *loa,
                                         struct kywc_scene_output *so);

struct kywc_scene_output_layout *
kywc_scene_attach_output_layout(struct kywc_root *scene, struct wlr_output_layout *output_layout);

struct kywc_scene_output *kywc_scene_get_scene_output(struct kywc_root *scene,
                                                      struct wlr_output *output);

void kywc_scene_output_set_position(struct kywc_scene_output *scene_output, int lx, int ly);

void kywc_scene_output_send_frame_done(struct kywc_scene_output *scene_output,
                                       struct timespec *now);

bool kywc_scene_output_commit(struct kywc_scene_output *scene_output,
                              struct kywc_effect_output *output_manager);

struct kywc_root *kywc_node_get_root(struct kywc_node *node);

/*************************************************************************/
struct kywc_render_instance {
    const struct kywc_render_instance_interface *impl;
    const char *name;

    struct wl_listener destroy_listener;

    struct {
        struct wl_signal destroy;
    } event;
};

struct kywc_render_instance_interface {
    void (*render)(struct kywc_render_instance *instance, struct kywc_render_target *target,
                   pixman_region32_t *damage);
    void (*compute_visible)(struct kywc_render_instance *instance);
    void (*try_direct_scanout)(struct wlr_output *output);
};

struct kywc_render_task {
    pixman_region32_t damage;
    struct kywc_render_target *target;
    struct kywc_render_instance *instance;
    struct wl_list dependences; /* dependences task */
    struct wl_list link;

    struct wl_listener destroy_listener;

    struct {
        struct wl_signal destroy;
    } event;
};

void kywc_render_instance_init(struct kywc_render_instance *instance,
                               const struct kywc_render_instance_interface *impl,
                               wl_notify_func_t destroy);

void kywc_render_instance_handle_destroy(struct wl_listener *listener, void *data);

struct kywc_render_task *kywc_render_task_create(struct kywc_render_instance *instance,
                                                 const pixman_region32_t *damage,
                                                 const struct kywc_render_target *target);

void kywc_render_task_init(struct kywc_render_task *task, struct kywc_render_instance *instance,
                           const pixman_region32_t *damage,
                           const struct kywc_render_target *target);

void kywc_render_task_release(struct kywc_render_task *task);

/*************************************************************************/
void kywc_box_scale_xy(const struct wlr_box *src, struct wlr_box *dst, float scale_x,
                       float scale_y);

void kywc_region_adjust(pixman_region32_t *dst, const pixman_region32_t *src, int top, int left,
                        int bottom, int right);

void kywc_region_scale_xy(pixman_region32_t *dst, const pixman_region32_t *src, float scale_x,
                          float scale_y);

void kywc_region_scale(pixman_region32_t *dst, const pixman_region32_t *src, float scale);

#endif
