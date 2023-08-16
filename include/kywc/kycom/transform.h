// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYCOM_TRANSFORM_H
#define _KYCOM_TRANSFORM_H

#include "scene.h"

struct transform_manager_node;

struct kywc_transform_root_node {
    struct kywc_group_node group;
    struct transform_manager_node *transforms_manager;
    struct kywc_node *transformed_node;

    /*Read only*/
    int use_count;

    struct kywc_transform_root_node *parent;
    struct wl_listener parent_destroy;
};

struct kywc_transform_root_node *kywc_transform_root_create(struct kywc_node *transformed_node);

void kywc_transform_root_destory(struct kywc_transform_root_node *view_root);

struct kywc_group_node *kywc_node_transform_get(struct kywc_node *transformed_node,
                                                const char *name);

bool kywc_node_transform_add(struct kywc_node *transformed_node, struct kywc_group_node *transform,
                             int z_order, const char *name);

struct kywc_group_node *kywc_node_transform_remove(struct kywc_node *transformed_node,
                                                   const char *name);

#endif
