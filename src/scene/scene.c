#include "scene/scene.h"

#if HAVE_WLR_SCENE

struct ky_scene *ky_scene_from_node(struct ky_scene_node *node)
{
    struct ky_scene_tree *tree;
    if (node->type == WLR_SCENE_NODE_TREE) {
        tree = wl_container_of(node, tree, node);
    } else {
        tree = node->parent;
    }

    while (tree->node.parent != NULL) {
        tree = tree->node.parent;
    }
    return (struct ky_scene *)tree;
}

#endif
