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

#ifndef DLIST_H
#define DLIST_H

#include <stddef.h>

#ifdef __GNUC__
#define PREFETCH(a) __builtin_prefetch((a), 0, 1)
#else
#define PREFETCH(a) ((void)a)
#endif

struct DListHead {
    struct DListHead *next, *prev; /* *< 前驱后驱指针  */
};

typedef struct DListHead DList;

#define D_LIST_HEAD_INIT(name) \
    {                          \
        &(name), &(name)       \
    }

#define D_LIST_HEAD(name) DList name = D_LIST_HEAD_INIT(name)

#define D_INIT_LIST_HEAD(ptr) \
    do {                      \
        (ptr)->next = (ptr);  \
        (ptr)->prev = (ptr);  \
    } while (0)

#if defined(__cplusplus)
extern "C" {
#endif

static inline void BaseListAdd(DList *newe, DList *prev, DList *next)
{
    next->prev = newe;
    newe->next = next;
    newe->prev = prev;
    prev->next = newe;
}

static inline void DListAdd(DList *newe, DList *head)
{
    BaseListAdd(newe, head, head->next);
}

static inline void DListAddTail(DList *newe, DList *head)
{
    BaseListAdd(newe, head->prev, head);
}

static inline void BaseListDel(DList *prev, DList *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void DListDel(DList *entry)
{
    BaseListDel(entry->prev, entry->next);
}

#define D_LIST_EMPTY(head) ((head)->next == (head))

#define D_LIST_ENTRY(ptr, type, member) ((type *)((char *)(ptr) - (char *)(&((type *)0)->member)))

#define D_LIST_FOR_DEL_EACH(pos, head) for ((pos) = (head)->next; (pos) != (head); (pos) = (head)->next)

#define D_LIST_FOR_EACH(pos, head) \
    for ((pos) = (head)->next, PREFETCH((pos)->next); (pos) != (head); (pos) = (pos)->next, PREFETCH((pos)->next))

#define D_LIST_FOR_EACH_SAFE(pos, n, head) \
    for ((pos) = (head)->next, (n) = (pos)->next; (pos) != (head); (pos) = (n), (n) = (pos)->next)

static inline void BaseListSplice(DList *list, DList *head)
{
    DList *first = list->next;
    DList *last = list->prev;
    DList *at = head->next;

    first->prev = head;
    head->next = first;

    last->next = at;
    at->prev = last;
}

static inline void DListSpliceInit(DList *list, DList *head)
{
    if (D_LIST_EMPTY(list) == 0) {
        BaseListSplice(list, head);
        D_INIT_LIST_HEAD(list);
    }
}

#if defined(__cplusplus)
}
#endif

#endif

