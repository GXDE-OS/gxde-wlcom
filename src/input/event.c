#include <stdlib.h>

#include "input/event.h"

static void input_event_node_destroy(struct wl_listener *listener, void *data)
{
    struct input_event_node *inode = wl_container_of(listener, inode, node_destroy);
    wl_list_remove(&inode->node_destroy.link);
    free(inode);
}

struct input_event_node *input_event_node_create(struct ky_scene_node *node,
                                                 const struct input_event_node_impl *impl,
                                                 input_event_node_get_root get_root,
                                                 input_event_node_get_toplevel get_toplevel,
                                                 void *data)
{
    struct input_event_node *inode = calloc(1, sizeof(struct input_event_node));
    if (!inode) {
        return NULL;
    }

    inode->data = data;
    inode->impl = impl;
    inode->get_root = get_root;
    inode->get_toplevel = get_toplevel;

    ky_scene_node_set_user_data(node, inode);
    inode->node_destroy.notify = input_event_node_destroy;
    ky_scene_node_add_destroy_listener(node, &inode->node_destroy);

    return inode;
}

static const struct input_event_node_impl dumb_impl = {
    .click = NULL,
    .hover = NULL,
    .leave = NULL,
};

static struct ky_scene_node *dumb_get_root(void *data)
{
    return data;
}

struct input_event_node *input_event_dumb_node_create(struct ky_scene_node *node,
                                                      struct ky_scene_node *root)
{
    return input_event_node_create(node, &dumb_impl, dumb_get_root, NULL, root);
}

struct input_event_node *input_event_node_from_node(struct ky_scene_node *node)
{
    struct ky_scene_node *n = node;

    struct ky_scene_tree *parent;
    while (n && !ky_scene_node_get_user_data(n)) {
        parent = ky_scene_node_get_parent(n);
        n = parent ? ky_scene_node_from_tree(parent) : NULL;
    }

    return n ? ky_scene_node_get_user_data(n) : NULL;
}

struct ky_scene_node *input_event_node_root(struct input_event_node *event_node)
{
    if (!event_node || !event_node->get_root) {
        return NULL;
    }

    return event_node->get_root(event_node->data);
}

struct wlr_surface *input_event_node_toplevel(struct input_event_node *event_node)
{
    if (!event_node || !event_node->get_toplevel) {
        return NULL;
    }

    return event_node->get_toplevel(event_node->data);
}
