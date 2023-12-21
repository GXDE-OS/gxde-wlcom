// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "kywc/kycom/transform.h"

struct transform_item {
    const char *name;
    struct kywc_group_node *transform;
    int z_order;
    struct wl_list link;
};

struct transform_manager_node {
    struct kywc_group_node group;
    struct wl_list transforms; // transform_item
    kywc_node_destroy_interface group_destory;
};

static void transform_manager_node_release_all(struct transform_manager_node *tmn);

static struct kywc_transform_root_node *node_get_transform_root(struct kywc_node *node);

/*****************************************************************************/
static void transform_manager_node_destroy(struct kywc_node *node)
{
    struct transform_manager_node *tmn = wl_container_of(node, tmn, group);

    /* Release transform item. */
    transform_manager_node_release_all(tmn);
    tmn->group_destory(node);
}

static const char *transform_manager_node_name(void)
{
    return "transform_manager_node";
}

static void transform_manager_node_init(struct transform_manager_node *tmn)
{
    kywc_group_node_init(&tmn->group);
    tmn->group_destory = tmn->group.node.destroy;
    tmn->group.node.node_name = transform_manager_node_name;
    tmn->group.node.destroy = transform_manager_node_destroy;
    wl_list_init(&tmn->transforms);
}

static struct transform_item *transform_manager_get_transform(struct transform_manager_node *tmn,
                                                              const char *name)
{
    if (!tmn) {
        return NULL;
    }
    bool finded = false;
    struct transform_item *pos_item, *temp_item;
    wl_list_for_each_safe(pos_item, temp_item, &tmn->transforms, link) {
        if (!strcmp(name, pos_item->name)) {
            finded = true;
            break;
        }
    }
    if (finded) {
        return pos_item;
    }
    return NULL;
}

static struct transform_item *transform_manager_node_add(struct transform_manager_node *tmn,
                                                         struct transform_item *item)
{
    if (!tmn || !item) {
        return NULL;
    }
    if (wl_list_empty(&tmn->transforms) && item) {
        wl_list_insert(&tmn->transforms, &item->link);
        return NULL;
    }

    struct transform_item *same_name = transform_manager_get_transform(tmn, item->name);
    if (same_name) {
        return same_name;
    }
    /* Max z-order in the list head. */
    bool finded = false;
    struct transform_item *pos_item, *temp_item, *last_item = NULL;
    wl_list_for_each_safe(pos_item, temp_item, &tmn->transforms, link) {
        if (pos_item->z_order < item->z_order) {
            finded = true;
            break;
        }
        last_item = pos_item;
    }

    if (finded) {
        wl_list_insert(&pos_item->link, &item->link);
        return pos_item;
    } else {
        wl_list_insert(&tmn->transforms, &item->link);
        return last_item;
    }
}

/* Return transform node*/
static struct kywc_group_node *transform_manager_node_remove(struct transform_manager_node *tmn,
                                                             const char *name)
{
    struct kywc_group_node *transform = NULL;
    struct transform_item *removing_item = transform_manager_get_transform(tmn, name);
    if (removing_item) {
        wl_list_remove(&removing_item->link);
        transform = removing_item->transform;
        free(removing_item);
    }
    return transform;
}

static void transform_manager_node_release_all(struct transform_manager_node *tmn)
{
    if (wl_list_empty(&tmn->transforms)) {
        return;
    }
    /**
     * just free the item, don't care the transform node,
     * because transform node isn't create by transform manager.
     */
    struct transform_item *pos_item, *temp_item;
    wl_list_for_each_safe(pos_item, temp_item, &tmn->transforms, link) {
        wl_list_remove(&pos_item->link);
        free(pos_item);
    }
}

static struct transform_manager_node *transform_manager_node_create(struct kywc_group_node *parent)
{
    struct transform_manager_node *tmn = malloc(sizeof(struct transform_manager_node));
    if (!tmn) {
        return NULL;
    }
    transform_manager_node_init(tmn);
    kywc_group_node_add(&tmn->group, parent);
    return tmn;
}

static const char *transform_root_node_name(void)
{
    return "transform_root_node";
}

/*****************************************************************************/
static void transform_root_node_init(struct kywc_transform_root_node *transform_root,
                                     struct kywc_node *node, struct kywc_group_node *parent)
{
    kywc_group_node_init(&transform_root->group);
    transform_root->group.node.node_name = transform_root_node_name;

    kywc_group_node_add(&transform_root->group, parent);
    transform_root->transformed_node = node;
    transform_root->transforms_manager = transform_manager_node_create(&transform_root->group);
    transform_root->use_count = 1;
}

static struct kywc_transform_root_node *transform_root_node_create(struct kywc_node *node,
                                                                   struct kywc_group_node *parent)
{
    struct kywc_transform_root_node *transform_root =
        malloc(sizeof(struct kywc_transform_root_node));
    if (!transform_root) {
        return NULL;
    }

    transform_root_node_init(transform_root, node, parent);
    return transform_root;
}

/*****************************************************************************/
static void destroy_transform_root_from_node(struct kywc_node *transformed_node)
{
    struct kywc_transform_root_node *transform_root = node_get_transform_root(transformed_node);
    if (!transform_root) {
        return;
    }
    struct kywc_group_node *new_parent = transform_root->group.node.parent;

    /* Remove from transform node's child list. */
    wl_list_remove(&transformed_node->link);
    transformed_node->parent = new_parent;

    wl_list_insert(new_parent->children.prev, &transformed_node->link);
    transformed_node->proxy_node = NULL;

    /**
     * Remove transform root from scene tree.
     * Realase transform_manager_node in transform root node destroy.
     */
    kywc_node_destroy(&transform_root->group.node);
}

static struct kywc_transform_root_node *create_transform_root_on_node(struct kywc_node *node)
{
    struct kywc_transform_root_node *transform_root =
        transform_root_node_create(node, node->parent);
    if (!transform_root) {
        return NULL;
    }

    struct kywc_group_node *new_parent = &transform_root->transforms_manager->group;
    /* Ensure that a node cannot become its own ancestor */
    for (struct kywc_group_node *ancestor = new_parent; ancestor != NULL;
         ancestor = ancestor->node.parent) {
        assert(&ancestor->node != node);
    }
    wl_list_remove(&node->link);
    node->parent = new_parent;
    wl_list_insert(new_parent->children.prev, &node->link);

    node->proxy_node = transform_root;
    return transform_root;
}

/* Paired with kywc_transform_root_destory*/
struct kywc_transform_root_node *kywc_transform_root_create(struct kywc_node *node)
{
    if (!node || !node->parent) {
        return NULL;
    }
    if (node->proxy_node) {
        struct kywc_transform_root_node *transform_root = node_get_transform_root(node);
        if (!transform_root) {
            wlr_log(WLR_ERROR, "The node' proxy_node maybe override %p", node->proxy_node);
            return NULL;
        }
        transform_root->use_count++;
        return transform_root;
    }
    return create_transform_root_on_node(node);
}

/**
 * After remove transform node, or view destroy.
 * Paired with kywc_transform_root_create
 */
void kywc_transform_root_destory(struct kywc_transform_root_node *transform_root)
{
    transform_root->use_count--;

    if (transform_root->use_count > 0) {
        return;
    }

    destroy_transform_root_from_node(transform_root->transformed_node);
}

/*****************************************************************************/
static void transform_item_init(struct transform_item *item, struct kywc_group_node *transform,
                                int z_order, const char *name)
{
    item->name = name;
    item->transform = transform;
    item->z_order = z_order;
}

static struct transform_item *transform_item_create(struct kywc_group_node *transform, int z_order,
                                                    const char *name)
{
    struct transform_item *item = malloc(sizeof(*item));
    if (!item) {
        return NULL;
    }
    transform_item_init(item, transform, z_order, name);
    return item;
}

static struct kywc_transform_root_node *node_get_transform_root(struct kywc_node *node)
{
    if (!node) {
        return NULL;
    }
    struct kywc_transform_root_node *transform_root =
        wl_container_of(node->proxy_node, transform_root, group);
    return transform_root;
}

static struct transform_manager_node *node_get_transform_manager(struct kywc_node *node)
{
    struct kywc_transform_root_node *transform_root = node_get_transform_root(node);
    if (transform_root) {
        return transform_root->transforms_manager;
    }

    return NULL;
}

static void group_node_set_children(struct kywc_group_node *group, struct wl_list *children)
{
    if (!group) {
        return;
    }
    struct kywc_node *node, *tempnode;
    wl_list_for_each_safe(node, tempnode, &group->children, link) {
        wl_list_remove(&node->link);
    }

    wl_list_for_each_safe(node, tempnode, children, link) {
        wl_list_remove(&node->link);
        node->parent = group;
        wl_list_insert(&group->children, &node->link);
    }
}

struct kywc_group_node *kywc_node_transform_get(struct kywc_node *transformed_node,
                                                const char *name)
{
    struct transform_manager_node *tmn = node_get_transform_manager(transformed_node);
    struct transform_item *same_name = transform_manager_get_transform(tmn, name);
    if (same_name) {
        return same_name->transform;
    }
    return NULL;
}

bool kywc_node_transform_add(struct kywc_node *node, struct kywc_group_node *transform, int z_order,
                             const char *name)
{
    struct kywc_transform_root_node *transform_root = node_get_transform_root(node);
    if (!transform_root) {
        return false;
    }

    struct transform_item *item = transform_item_create(transform, z_order, name);
    if (!item) {
        return false;
    }
    struct wl_list children;
    wl_list_init(&children);
    wl_list_insert(&children, &item->transform->node.link);
    struct transform_item *pre_item =
        transform_manager_node_add(transform_root->transforms_manager, item);
    if (!pre_item) {
        /* manager_node child -> transform child -> view_node */
        kywc_node_reparent_ex(node, item->transform);
        group_node_set_children(&transform_root->transforms_manager->group, &children);
    } else {
        if (strcmp(pre_item->name, name) == 0) {
            return false;
        }
        // pre_item child -> transform child ->pre_item old child
        pre_item->transform->node.push_damage(&pre_item->transform->node, NULL);
        group_node_set_children(item->transform, &pre_item->transform->children);
        group_node_set_children(pre_item->transform, &children);
        // item->transform->node.push_damage(&item->transform->node, NULL);
    }
    return true;
}

struct kywc_group_node *kywc_node_transform_remove(struct kywc_node *transformed_node,
                                                   const char *name)
{
    struct transform_manager_node *tmn = node_get_transform_manager(transformed_node);

    if (!tmn) {
        return NULL;
    }
    struct kywc_group_node *node = transform_manager_node_remove(tmn, name);

    if (node) {
        /*remove from scene*/
        struct kywc_node *child_node, *tempnode;
        wl_list_for_each_safe(child_node, tempnode, &node->children, link) {
            kywc_node_reparent_ex(child_node, node->node.parent);
        }
        wl_list_remove(&node->node.link);
        wl_list_init(&node->node.link);
        node->node.parent = NULL;
    }
    return node;
}
