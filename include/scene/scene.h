// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCENE_H_
#define _SCENE_H_

#include <pixman.h>
#include <time.h>

#include <wlr/types/wlr_damage_ring.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

struct wlr_buffer;
struct wlr_output;
struct wlr_output_layout;
struct wlr_output_layout_output;

struct ky_scene_node;
struct ky_scene_tree;
struct ky_scene_rect;
struct ky_scene_buffer;

struct ky_scene_output;
struct ky_scene_output_layout;
struct ky_scene_output_state_options;

struct ky_scene_render_target;

typedef void (*ky_scene_node_destroy_func_t)(struct ky_scene_node *node);

typedef struct ky_scene_node *(*ky_scene_node_accpet_input_func_t)(struct ky_scene_node *node,
                                                                   int lx, int ly, double px,
                                                                   double py, double *rx,
                                                                   double *ry);

typedef void (*ky_scene_node_update_outputs_func_t)(struct ky_scene_node *node, int lx, int ly,
                                                    struct wl_list *outputs,
                                                    struct ky_scene_output *ignore,
                                                    struct ky_scene_output *force);

typedef void (*ky_scene_node_collect_damage_func_t)(struct ky_scene_node *node, int lx, int ly,
                                                    bool parent_enabled, uint32_t damage_type,
                                                    pixman_region32_t *damage,
                                                    pixman_region32_t *invisible,
                                                    pixman_region32_t *affected);

typedef void (*ky_scene_node_get_bounding_box_func_t)(struct ky_scene_node *node,
                                                      struct wlr_box *box);

typedef void (*ky_scene_node_push_damage_func_t)(struct ky_scene_node *node,
                                                 struct ky_scene_node *damage_node,
                                                 uint32_t damage_type, pixman_region32_t *damage);

typedef void (*ky_scene_node_render_func_t)(struct ky_scene_node *node, int lx, int ly,
                                            struct ky_scene_render_target *target);

struct ky_scene_node_interface {
    /**
     * Check the node is accepted input event in the box.
     */
    ky_scene_node_accpet_input_func_t accpet_input;
    /**
     * Update node output state.
     */
    ky_scene_node_update_outputs_func_t update_outputs;
    /**
     * Collect all nodes damage region.
     */
    ky_scene_node_collect_damage_func_t collect_damage;
    /**
     * Generate a rendering instance for the node and
     * start the rendering instance generation function for the child nodes.
     */
    ky_scene_node_render_func_t render;
    /**
     * Get node bounding box.
     */
    ky_scene_node_get_bounding_box_func_t get_bounding_box;
    /**
     * Push damgae to output.
     */
    ky_scene_node_push_damage_func_t push_damage;
    /**
     * Private method, call it by ky_scene_node_destroy.
     */
    ky_scene_node_destroy_func_t destroy;
};

/* scene node props */
enum ky_scene_node_prop {
    KY_SCENE_NODE_INVALID = 0,
    // bit 0 ... 2, node type [1, 7]
    KY_SCENE_NODE_TREE = 1 << 0,
    KY_SCENE_NODE_RECT,
    KY_SCENE_NODE_BUFFER,

    // bit 3 ... 6, node role: root, layer, workspace, toplevel, xwayland, popup, ssd,  ...
    KY_SCENE_NODE_ROOT = 1 << 3,
    KY_SCENE_NODE_TOPLEVEL,
    KY_SCENE_NODE_SUBSURFACE,
    KY_SCENE_NODE_POPUP,

    // bit 7, node from: external(client)
    KY_SCENE_NODE_EXTERNAL = 1 << 7,
};

enum ky_scene_damage_type {
    KY_SCENE_DAMAGE_NONE = 0,
    /* the damage will not affect the visible region of the node */
    KY_SCENE_DAMAGE_HARMLESS = 1 << 0,
    /* the damage will affect the visible region of the node */
    KY_SCENE_DAMAGE_HARMFUL = 1 << 1,
};

enum ky_scene_round_corner {
    KY_SCENE_ROUND_CORNER_RB, // right-bottom
    KY_SCENE_ROUND_CORNER_RT, // right-top
    KY_SCENE_ROUND_CORNER_LB, // left-bottom
    KY_SCENE_ROUND_CORNER_LT, // left-top
};

struct ky_scene_node {
    struct ky_scene_tree *parent;
    struct wl_list link;

    union {
        struct {
            uint8_t type : 3;
            uint8_t role : 4;
            uint8_t from : 1;
        };
        uint8_t prop;
    };

    bool enabled, bypassed;
    int x, y;
    /* round corner radius */
    int radius[4];

    /* node damage type after last collect_damage */
    uint32_t damage_type;
    /* enabled state after last collect_damage */
    bool last_enabled;
    pixman_region32_t visible_region;
    /* region that can accept input */
    pixman_region32_t input_region;
    /* reigon that can render */
    pixman_region32_t clip_region;

    /* impl.xxx MUST not be NULL */
    struct ky_scene_node_interface impl;

    struct {
        struct wl_signal destroy;
    } events;

    struct wlr_addon_set addons;

    void *data;
};

struct ky_scene_tree {
    struct ky_scene_node node;
    struct wl_list children;
};

struct ky_scene {
    struct ky_scene_tree tree;
    ky_scene_node_destroy_func_t tree_destroy;

    struct wl_list outputs;

    /* damage regon after collect_damage based in node's visible region */
    pixman_region32_t collected_damage;
    /* invisible region after collect_damage */
    pixman_region32_t collected_invisible;
    /* damage region pushed by nodes */
    pixman_region32_t pushed_damage;

    // May be NULL
    struct wlr_presentation *presentation;
    struct wl_listener presentation_destroy;
    // struct wlr_linux_dmabuf_v1 *linux_dmabuf_v1;
    // struct wl_listener linux_dmabuf_v1_destroy;
};

struct ky_scene_rect {
    struct ky_scene_node node;
    ky_scene_node_destroy_func_t node_destroy;

    int width, height;
    float color[4];
};

typedef bool (*ky_scene_buffer_point_accepts_input_func_t)(struct ky_scene_buffer *buffer,
                                                           double *sx, double *sy);

struct ky_scene_outputs_update_event {
    struct ky_scene_output **active;
    size_t size;
};

struct ky_scene_output_sample_event {
    struct ky_scene_output *output;
    bool direct_scanout;
};

struct ky_scene_buffer {
    struct ky_scene_node node;
    ky_scene_node_destroy_func_t node_destroy;

    /* May be NULL */
    struct wlr_buffer *buffer;
    /* May be NULL */
    struct wlr_texture *texture;

    struct wlr_fbox src_box;
    int dst_width, dst_height;
    enum wl_output_transform transform;

    float opacity;
    pixman_region32_t opaque_region;

    /**
     * The output that the largest area of this buffer is displayed on.
     * This may be NULL if the buffer is not currently displayed on any
     * outputs. This is the output that should be used for frame callbacks,
     * presentation feedback, etc.
     */
    struct ky_scene_output *primary_output;
    uint64_t active_outputs;

    ky_scene_buffer_point_accepts_input_func_t point_accepts_input;

    struct {
        struct wl_signal outputs_update; // ky_scene_outputs_update_event
        struct wl_signal output_enter;   // struct ky_scene_output
        struct wl_signal output_leave;   // struct ky_scene_output
        struct wl_signal output_sample;  // ky_scene_output_sample_event
        struct wl_signal frame_done;     // struct timespec
    } events;
};

struct ky_scene_output {
    struct wlr_output *output;
    struct wl_list link;

    struct ky_scene *scene;
    struct wlr_addon addon;

    struct wlr_damage_ring damage_ring;

    int x, y;

    struct {
        struct wl_signal destroy;
    } events;

    uint8_t index;
    bool prev_scanout;

    struct wl_listener output_commit;
    struct wl_listener output_damage;
    struct wl_listener output_needs_frame;
};

void ky_scene_node_destroy(struct ky_scene_node *node);

struct ky_scene *ky_scene_create(void);

struct ky_scene *ky_scene_from_node(struct ky_scene_node *node);

struct ky_scene_tree *ky_scene_tree_create(struct ky_scene_tree *parent);

struct ky_scene_tree *ky_scene_tree_from_node(struct ky_scene_node *node);

void ky_scene_node_set_enabled(struct ky_scene_node *node, bool enabled);

void ky_scene_node_set_bypassed(struct ky_scene_node *node, bool bypassed);

void ky_scene_node_set_position(struct ky_scene_node *node, int x, int y);

void ky_scene_node_place_above(struct ky_scene_node *node, struct ky_scene_node *sibling);

void ky_scene_node_place_below(struct ky_scene_node *node, struct ky_scene_node *sibling);

void ky_scene_node_raise_to_top(struct ky_scene_node *node);

void ky_scene_node_lower_to_bottom(struct ky_scene_node *node);

void ky_scene_node_reparent(struct ky_scene_node *node, struct ky_scene_tree *new_parent);

bool ky_scene_node_coords(struct ky_scene_node *node, int *lx_ptr, int *ly_ptr);

struct ky_scene_node *ky_scene_node_at(struct ky_scene_node *node, double lx, double ly, double *nx,
                                       double *ny);

void ky_scene_node_set_input_region(struct ky_scene_node *node, const pixman_region32_t *region);

void ky_scene_node_set_clip_region(struct ky_scene_node *node, const pixman_region32_t *region);

void ky_scene_node_set_radius(struct ky_scene_node *node, const int radius[static 4]);

// TODO: is removed in wlroots
struct wlr_presentation;
void ky_scene_set_presentation(struct ky_scene *scene, struct wlr_presentation *presentation);

/**
 * scene rect
 */
struct ky_scene_rect *ky_scene_rect_create(struct ky_scene_tree *parent, int width, int height,
                                           const float color[static 4]);

void ky_scene_rect_set_size(struct ky_scene_rect *rect, int width, int height);

void ky_scene_rect_set_color(struct ky_scene_rect *rect, const float color[static 4]);

struct ky_scene_rect *ky_scene_rect_from_node(struct ky_scene_node *node);

/**
 * scene buffer
 */
struct ky_scene_buffer *ky_scene_buffer_create(struct ky_scene_tree *parent,
                                               struct wlr_buffer *buffer);

struct ky_scene_buffer *ky_scene_buffer_from_node(struct ky_scene_node *node);

void ky_scene_buffer_set_buffer(struct ky_scene_buffer *scene_buffer, struct wlr_buffer *buffer);

void ky_scene_buffer_set_buffer_with_damage(struct ky_scene_buffer *scene_buffer,
                                            struct wlr_buffer *buffer,
                                            const pixman_region32_t *region);

void ky_scene_buffer_set_opacity(struct ky_scene_buffer *scene_buffer, float opacity);

void ky_scene_buffer_set_opaque_region(struct ky_scene_buffer *scene_buffer,
                                       const pixman_region32_t *region);

void ky_scene_buffer_set_source_box(struct ky_scene_buffer *scene_buffer,
                                    const struct wlr_fbox *box);

void ky_scene_buffer_set_dest_size(struct ky_scene_buffer *scene_buffer, int width, int height);

void ky_scene_buffer_set_transform(struct ky_scene_buffer *scene_buffer,
                                   enum wl_output_transform transform);

/**
 * scene output
 */
struct ky_scene_output *ky_scene_get_scene_output(struct ky_scene *scene,
                                                  struct wlr_output *output);

struct ky_scene_output_layout *
ky_scene_attach_output_layout(struct ky_scene *scene, struct wlr_output_layout *output_layout);

bool ky_scene_output_commit(struct ky_scene_output *scene_output,
                            const struct ky_scene_output_state_options *options);

void ky_scene_output_send_frame_done(struct ky_scene_output *scene_output, struct timespec *now);

struct ky_scene_output *ky_scene_output_create(struct ky_scene *scene, struct wlr_output *output);

void ky_scene_output_layout_add_output(struct ky_scene_output_layout *sol,
                                       struct wlr_output_layout_output *lo,
                                       struct ky_scene_output *so);

void ky_scene_output_destroy(struct ky_scene_output *scene_output);

#endif
