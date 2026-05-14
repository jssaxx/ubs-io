/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "art_range.h"
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#define ART_MAX_BYTE (255)
#define ART_MIN_BYTE (0)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define EXT_IS_LEAF(x) (((uintptr_t)(x) & 1))
#define EXT_LEAF_RAW(x) ((art_leaf *)((void *)((uintptr_t)(x) & ~(uintptr_t)1)))

typedef struct {
    const unsigned char *k_start;
    int l_start;
    const unsigned char *k_end;
    int l_end;
    art_callback cb;
    void *data;
} range_query_context;

static art_leaf *ext_minimum(const art_node *n)
{
    while (n && !EXT_IS_LEAF(n)) {
        switch (n->type) {
            case NODE4:
                n = ((const art_node4 *)n)->children[0];
                break;
            case NODE16:
                n = ((const art_node16 *)n)->children[0];
                break;
            case NODE48: {
                int idx = 0;
                const art_node48 *p48 = (const art_node48 *)n;
                while (!p48->keys[idx]) {
                    idx++;
                }
                n = p48->children[p48->keys[idx] - 1];
                break;
            }
            case NODE256: {
                int idx = 0;
                const art_node256 *p256 = (const art_node256 *)n;
                while (!p256->children[idx]) {
                    idx++;
                }
                n = p256->children[idx];
                break;
            }
            default:
                return NULL;
        }
    }
    return n ? EXT_LEAF_RAW(n) : NULL;
}

static inline int leaf_cmp(const art_leaf *l, const unsigned char *key, int key_len, int depth)
{
    int min_len = MIN((int)l->key_len, key_len);
    if (min_len > depth) {
        int cmp = memcmp(l->key + depth, key + depth, min_len - depth);
        if (cmp != 0) {
            return cmp;
        }
    }
    return (int)l->key_len - key_len;
}

static int ext_iterate_all(art_node *n, art_callback cb, void *data);

static int iterate_node4(art_node4 *p, art_callback cb, void *data)
{
    int res = 0;
    for (int i = 0; i < p->n.num_children; i++) {
        if ((res = ext_iterate_all(p->children[i], cb, data))) {
            return res;
        }
    }
    return 0;
}

static int iterate_node16(art_node16 *p, art_callback cb, void *data)
{
    int res = 0;
    for (int i = 0; i < p->n.num_children; i++) {
        if ((res = ext_iterate_all(p->children[i], cb, data))) {
            return res;
        }
    }
    return 0;
}

static int iterate_node48(art_node48 *p, art_callback cb, void *data)
{
    int res = 0;
    for (int i = 0; i <= ART_MAX_BYTE; i++) {
        int idx = p->keys[i];
        if (idx) {
            if ((res = ext_iterate_all(p->children[idx - 1], cb, data))) {
                return res;
            }
        }
    }
    return 0;
}

static int iterate_node256(art_node256 *p, art_callback cb, void *data)
{
    int res = 0;
    for (int i = 0; i <= ART_MAX_BYTE; i++) {
        if (p->children[i]) {
            if ((res = ext_iterate_all(p->children[i], cb, data))) {
                return res;
            }
        }
    }
    return 0;
}

static int ext_iterate_all(art_node *n, art_callback cb, void *data)
{
    if (!n) {
        return 0;
    }

    if (EXT_IS_LEAF(n)) {
        art_leaf *l = EXT_LEAF_RAW(n);
        return cb(data, (const unsigned char *)l->key, l->key_len, l->value);
    }

    switch (n->type) {
        case NODE4: {
            return iterate_node4((art_node4 *)n, cb, data);
        }
        case NODE16: {
            return iterate_node16((art_node16 *)n, cb, data);
        }
        case NODE48: {
            return iterate_node48((art_node48 *)n, cb, data);
        }
        case NODE256: {
            return iterate_node256((art_node256 *)n, cb, data);
        }
    }
    return 0;
}

static int check_leaf_match(art_leaf *l, int depth, bool b_start, bool b_end, const range_query_context *ctx)
{
    if (b_start && leaf_cmp(l, ctx->k_start, ctx->l_start, depth) < 0) {
        return 0;
    }
    if (b_end && leaf_cmp(l, ctx->k_end, ctx->l_end, depth) > 0) {
        return 0;
    }
    return ctx->cb(ctx->data, (const unsigned char *)l->key, l->key_len, l->value);
}

static bool check_short_prefix(art_node *n, int *depth, bool *b_start, bool *b_end, const range_query_context *ctx)
{
    int check_len = MIN((int)n->partial_len, (int)MAX_PREFIX_LEN);
    for (int i = 0; i < check_len; i++) {
        int d = *depth + i;
        if (*b_start) {
            unsigned char cs = (d < ctx->l_start) ? ctx->k_start[d] : ART_MIN_BYTE;
            if (n->partial[i] < cs) {
                return false;
            }
            if (n->partial[i] > cs) {
                *b_start = false;
            }
        }
        if (*b_end) {
            unsigned char ce = (d < ctx->l_end) ? ctx->k_end[d] : ART_MIN_BYTE;
            if (n->partial[i] > ce) {
                return false;
            }
            if (n->partial[i] < ce) {
                *b_end = false;
            }
        }
    }
    return true;
}

static bool check_long_prefix(art_node *n, int *depth, bool *b_start, bool *b_end, const range_query_context *ctx)
{
    art_leaf *l = ext_minimum(n);
    for (uint32_t i = MAX_PREFIX_LEN; i < n->partial_len; i++) {
        int d = *depth + i;
        unsigned char c = l->key[d];
        if (*b_start) {
            unsigned char cs = (d < ctx->l_start) ? ctx->k_start[d] : ART_MIN_BYTE;
            if (c < cs) {
                return false;
            }
            if (c > cs) {
                *b_start = false;
            }
        }
        if (*b_end) {
            unsigned char ce = (d < ctx->l_end) ? ctx->k_end[d] : ART_MIN_BYTE;
            if (c > ce) {
                return false;
            }
            if (c < ce) {
                *b_end = false;
            }
        }
        if (!*b_start && !*b_end) {
            break;
        }
    }
    return true;
}

static bool process_prefix(art_node *n, int *depth, bool *b_start, bool *b_end, const range_query_context *ctx)
{
    if (!check_short_prefix(n, depth, b_start, b_end, ctx)) {
        return false;
    }
    if (n->partial_len > MAX_PREFIX_LEN && (*b_start || *b_end)) {
        if (!check_long_prefix(n, depth, b_start, b_end, ctx)) {
            return false;
        }
    }
    *depth += (int)n->partial_len;
    return true;
}

static int ext_recursive_range(art_node *n, int depth, bool b_start, bool b_end, const range_query_context *ctx);

static inline unsigned char get_min_c(int depth, bool b_start, const range_query_context *ctx)
{
    return b_start ? ((depth < ctx->l_start) ? ctx->k_start[depth] : ART_MIN_BYTE) : ART_MIN_BYTE;
}

static inline unsigned char get_max_c(int depth, bool b_end, const range_query_context *ctx)
{
    return b_end ? ((depth < ctx->l_end) ? ctx->k_end[depth] : ART_MIN_BYTE) : ART_MAX_BYTE;
}

static int search_node4(art_node4 *p, int depth, bool b_start, bool b_end, const range_query_context *ctx)
{
    int res = 0;
    unsigned char min_c = get_min_c(depth, b_start, ctx);
    unsigned char max_c = get_max_c(depth, b_end, ctx);

    for (int i = 0; i < p->n.num_children; i++) {
        unsigned char c = p->keys[i];
        if (c < min_c) {
            continue;
        }
        if (c > max_c) {
            break;
        }
        if ((res =
                 ext_recursive_range(p->children[i], depth + 1, b_start && (c == min_c), b_end && (c == max_c), ctx))) {
            return res;
        }
    }
    return 0;
}

static int search_node16(art_node16 *p, int depth, bool b_start, bool b_end, const range_query_context *ctx)
{
    int res = 0;
    unsigned char min_c = get_min_c(depth, b_start, ctx);
    unsigned char max_c = get_max_c(depth, b_end, ctx);

    for (int i = 0; i < p->n.num_children; i++) {
        unsigned char c = p->keys[i];
        if (c < min_c) {
            continue;
        }
        if (c > max_c) {
            break;
        }
        if ((res =
                 ext_recursive_range(p->children[i], depth + 1, b_start && (c == min_c), b_end && (c == max_c), ctx))) {
            return res;
        }
    }
    return 0;
}

static int search_node48(art_node48 *p, int depth, bool b_start, bool b_end, const range_query_context *ctx)
{
    int res = 0;
    unsigned char min_c = get_min_c(depth, b_start, ctx);
    unsigned char max_c = get_max_c(depth, b_end, ctx);

    for (int i = min_c; i <= max_c; i++) {
        int idx = p->keys[i];
        if (idx) {
            if ((res = ext_recursive_range(p->children[idx - 1], depth + 1, b_start && (i == min_c),
                                           b_end && (i == max_c), ctx))) {
                return res;
            }
        }
    }
    return 0;
}

static int search_node256(art_node256 *p, int depth, bool b_start, bool b_end, const range_query_context *ctx)
{
    int res = 0;
    unsigned char min_c = get_min_c(depth, b_start, ctx);
    unsigned char max_c = get_max_c(depth, b_end, ctx);

    for (int i = min_c; i <= max_c; i++) {
        if (p->children[i]) {
            if ((res = ext_recursive_range(p->children[i], depth + 1, b_start && (i == min_c), b_end && (i == max_c),
                                           ctx))) {
                return res;
            }
        }
    }
    return 0;
}

static int search_children(art_node *n, int depth, bool b_start, bool b_end, const range_query_context *ctx)
{
    switch (n->type) {
        case NODE4: {
            return search_node4((art_node4 *)n, depth, b_start, b_end, ctx);
        }
        case NODE16: {
            return search_node16((art_node16 *)n, depth, b_start, b_end, ctx);
        }
        case NODE48: {
            return search_node48((art_node48 *)n, depth, b_start, b_end, ctx);
        }
        case NODE256: {
            return search_node256((art_node256 *)n, depth, b_start, b_end, ctx);
        }
    }
    return 0;
}

static int ext_recursive_range(art_node *n, int depth, bool b_start, bool b_end, const range_query_context *ctx)
{
    if (!n) {
        return 0;
    }

    if (!b_start && !b_end) {
        return ext_iterate_all(n, ctx->cb, ctx->data);
    }

    if (EXT_IS_LEAF(n)) {
        return check_leaf_match(EXT_LEAF_RAW(n), depth, b_start, b_end, ctx);
    }

    if (n->partial_len) {
        if (!process_prefix(n, &depth, &b_start, &b_end, ctx)) {
            return 0;
        }
    }

    if (!b_start && !b_end) {
        return ext_iterate_all(n, ctx->cb, ctx->data);
    }

    return search_children(n, depth, b_start, b_end, ctx);
}

int art_search_range_external(art_tree *t, const art_range_bound *start_bound, const art_range_bound *end_bound,
                              art_callback cb, void *data)
{
    if (!t || !t->root || !cb || !data) {
        return 0;
    }

    range_query_context ctx = {start_bound->key, start_bound->len, end_bound->key, end_bound->len, cb, data};
    return ext_recursive_range(t->root, 0, true, true, &ctx);
}
