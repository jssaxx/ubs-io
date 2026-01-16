/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 * This file contains  Red-black tree algorithm implementation from NGINX
 * which is licensed under 2-clause BSD-like license
 * please see https://github.com/nginx/nginx/blob/master/LICENSE for details.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <stdint.h>

#ifndef _NGX_RBTREE_H_INCLUDED_
#define _NGX_RBTREE_H_INCLUDED_

typedef struct ngx_rbtree_node_s ngx_rbtree_node_t;

struct ngx_rbtree_node_s {
    ngx_rbtree_node_t *left;
    ngx_rbtree_node_t *right;
    ngx_rbtree_node_t *parent;
    uint8_t color;
    uint8_t data;
};

typedef struct ngx_rbtree_s ngx_rbtree_t;

typedef int (*ngx_rbtree_insert_pt)(ngx_rbtree_node_t *root, ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel,
                                    void *context);

struct ngx_rbtree_s {
    ngx_rbtree_node_t *root;
    ngx_rbtree_node_t *sentinel;
    ngx_rbtree_insert_pt insert;
};

#define ngx_rbtree_init(tree, s, i) \
    ngx_rbtree_sentinel_init(s);    \
    (tree)->root = s;               \
    (tree)->sentinel = s;           \
    (tree)->insert = i

#define ngx_rb_entry(ptr, type, member) CONTAINER_OF(ptr, type, member)

void ngx_rbtree_insert(ngx_rbtree_t *tree, ngx_rbtree_node_t *node, void *context);
void ngx_rbtree_delete(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);
ngx_rbtree_node_t *ngx_rbtree_next(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);
void ngx_rbtree_replace(ngx_rbtree_t *tree, ngx_rbtree_node_t *target, ngx_rbtree_node_t *source);

#define ngx_rbt_red(node) ((node)->color = 1)
#define ngx_rbt_black(node) ((node)->color = 0)
#define ngx_rbt_is_red(node) ((node)->color)
#define ngx_rbt_is_black(node) (!ngx_rbt_is_red(node))
#define ngx_rbt_copy_color(n1, n2) (n1->color = n2->color)

/* a sentinel must be black */

#define ngx_rbtree_sentinel_init(node) ngx_rbt_black(node)

static inline ngx_rbtree_node_t *ngx_rbtree_min(ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    while (node->left != sentinel) {
        node = node->left;
    }

    return node;
}

#endif /* _NGX_RBTREE_H_INCLUDED_ */
