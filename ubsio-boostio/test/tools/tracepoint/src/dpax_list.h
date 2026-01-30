/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef BIO_DPAX_LIST_H
#define BIO_DPAX_LIST_H

#include <unistd.h>
#include <errno.h>
#include <stddef.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#ifndef LIST_POISON1
#define LIST_POISON1  ((void *) 0x00100100)
#endif

#ifndef LIST_POISON2
#define LIST_POISON2  ((void *) 0x00200200)
#endif

#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member) \
        ((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#endif

struct ListHead {
    struct ListHead *next, *prev;
};

typedef struct ListHead ListHeadT;

#define DPAX_LIST_HEAD_INIT(name) { &(name), &(name) }

#define DPAX_LIST_HEAD(name) \
        struct ListHead name = DPAX_LIST_HEAD_INIT(name)

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
    struct ListHead name = LIST_HEAD_INIT(name)

#define DPAX_INIT_LIST_NODE(ptr)   { (ptr)->next = (struct ListHead *)LIST_POISON1; (ptr)->prev = (struct ListHead *)LIST_POISON2; }

#define IS_LIST_NODE_INIT(ptr)  ((LIST_POISON1 == (void*)((ptr)->next)) && (LIST_POISON2 == (void*)((ptr)->prev)))

#define DPAX_LIST_ENTRY(_ptr, _type, _memb)    \
                CONTAINER_OF(_ptr, _type, _memb)

#define DPAX_LIST_FOR_EACH(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define DPAX_LIST_FOR_DEL_EACH(pos, head) \
    for (pos = (head)->next; pos != (head); \
            pos = (head)->next)

#define INIT_LIST_HEAD(ptr) { \
        (ptr)->next = (ptr); (ptr)->prev = (ptr); \
    }

static inline void DpaxInitListHead(struct ListHead *list)
{
    list->next = list;
    list->prev = list;
}

static inline void ListAdd(struct ListHead *newnode, struct ListHead *prev, struct ListHead *next)
{
	next->prev = newnode;
	newnode->next = next;
	newnode->prev = prev;
	prev->next = newnode;
}

static inline void DpaxListAdd(struct ListHead *newnode, struct ListHead *head)
{
    ListAdd(newnode, head, head->next);
}

static inline void DpaxListAddTail(struct ListHead *newnode, struct ListHead *head)
{
    ListAdd(newnode, head->prev, head);
}

static inline void DpaxListInsert(struct ListHead *new_node, struct ListHead* prev_node, struct ListHead *next_node)
{
    ListAdd(new_node, prev_node, next_node);
}

static inline void ListDel(struct ListHead * prev, struct ListHead *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void DpaxListDel(struct ListHead *entry)
{
    ListDel(entry->prev, entry->next);
    DPAX_INIT_LIST_NODE(entry);
}

static inline int ListEmpty(const struct ListHead *head)
{
	return head->next == head;
}

static inline ListHeadT* DpaxListGetFirst(ListHeadT *head)
{
    return ListEmpty(head) ? NULL : head->next;
}

static inline ListHeadT* DpaxListGetTail(ListHeadT *head)
{
    return  (head->prev == head) ? NULL : head->prev;
}

static inline ListHeadT* DpaxListDelFirst(ListHeadT *head)
{
    ListHeadT* ret = NULL;

    ret = head->next;
    if (ret == head)
    {
        return NULL;
    }
    else
    {
        DpaxListDel(ret);
    }

    return ret;
}

static inline void DpaxListDelInit(struct ListHead *entry)
{
    ListDel(entry->prev, entry->next);
    DpaxInitListHead(entry);
}

static inline ListHeadT* DpaxListDelTail(ListHeadT *head)
{
    ListHeadT* ret;

    ret = head->prev;
    if (ret == head)
    {
        return NULL;
    }
    else
    {
        DpaxListDel(ret);
    }

    return ret;
}

#define DPAX_LIST_FOR_DEL_ALL(pos, type, listHead, name)  \
do {                                                      \
    DPAX_LIST_FOR_DEL_EACH(pos, listHead) {               \
        name = DPAX_LIST_ENTRY(pos, type, listNode);      \
        DpaxListDel(pos);                                 \
        free(name);                                       \
    }                                                     \
} while(0)

static inline void DpaxListMove(struct ListHead *list, struct ListHead *head)
{
    DpaxListDel(list);
    DpaxListAdd(list, head);
}

static inline void DpaxListMoveTail(struct ListHead *list, struct ListHead *head)
{
    DpaxListDel(list);
    DpaxListAddTail(list, head);
}

static inline int DpaxListEmpty(const struct ListHead *head)
{
    return head->next == head;
}

static inline int DpaxListEmptyCareful(const struct ListHead *head)
{
	struct ListHead *next = head->next;
	return (next == head) && (next == head->prev);
}

static inline int DpaxListIsLast(const struct ListHead *list, const struct ListHead *head)
{
    return list->next == head;
}

static inline void ListSplice(const struct ListHead *list, struct ListHead *prev, struct ListHead *next)
{
    struct ListHead *head = list->next;
    struct ListHead *tail = list->prev;

    head->prev = prev;
    prev->next = head;

    tail->next = next;
    next->prev = tail;
}

static inline void DpaxListSplice(const struct ListHead *list, struct ListHead *head)
{
    if (!DpaxListEmpty(list))
        ListSplice(list, head, head->next);
}

static inline void DpaxListSpliceInit(struct ListHead *list, struct ListHead *head)
{
     if (!DpaxListEmpty(list)) {
         ListSplice(list, head, head->next);
         INIT_LIST_HEAD(list);
     }
}

static inline void DpaxListSpliceTail(struct ListHead *list, struct ListHead *head)
{
    if (!DpaxListEmpty(list))
        ListSplice(list, head->prev, head);
}

#define INIT_LIST_NODE(ptr)   { (ptr)->next = (struct ListHead *)LIST_POISON1; (ptr)->prev = (struct ListHead *)LIST_POISON2; }

#define DPAX_LIST_FOR_EACH_PREV(pos, head) \
            for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define DPAX_LIST_FOR_EACH_PREV_SAFE(pos, p, head)\
                        for(pos = (head)->prev, p = pos->prev; pos != (head);\
                            pos = p, p = pos->prev)

static inline void DpaxListForEachPrevSafe(struct ListHead *list, struct ListHead *head)
{
    if (!ListEmpty(list)) {
        ListSplice(list, head->prev, head);
        INIT_LIST_HEAD(list);
    }
}

static inline void DpaxListReplace(ListHeadT *old_node, ListHeadT *new_node)
{
    new_node->next = old_node->next;
    new_node->next->prev = new_node;
    new_node->prev = old_node->prev;
    new_node->prev->next = new_node;
    DpaxInitListHead(old_node);
}

static inline int DpaxListCheckInQueue(ListHeadT *node)
{
    if ((node->next == node) && (node->prev == node))
    {
        return 0;
    }

    return 1;
}

static inline void DpaxListMerge(ListHeadT *first_list, ListHeadT *second_list)
{
    if (DpaxListEmpty(second_list))
    {
        return;
    }

    if (DpaxListEmpty(first_list))
    {
        DpaxListReplace(second_list, first_list);
        return;
    }

    ListHeadT *first_list_end = first_list->prev;
    ListHeadT *second_list_begin = second_list->next;
    ListHeadT *second_list_end = second_list->prev;

    first_list_end->next = second_list_begin;
    second_list_begin->prev = first_list_end;
    second_list_end->next = first_list;
    first_list->prev = second_list_end;

    DpaxInitListHead(second_list);
}

static inline int DpaxListNodeIsTail(ListHeadT *node, ListHeadT *head)
{
    if (node == head->prev)
    {
        return 1;
    }

    return 0;
}

static inline int DpaxListNodeIsFirst(ListHeadT *node, ListHeadT *head)
{
    if (node == head->next)
    {
        return 1;
    }

    return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif

#endif /* __cplusplus */

#endif // BIO_DPAX_LIST_H