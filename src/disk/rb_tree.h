/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

/**
* @file    rb_tree.h
* @brief   红黑树头文件
* @verbatim
  功能描述：红黑树
  目标用户：NA
  使用约束：NA
  升级影响: no
@endverbatim
*
* @author
* @version  v1.0.0
* @see      无
* @date     2021-05-20
*/

#ifndef RB_TREE_H
#define RB_TREE_H

#include <stddef.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif
/**
 * 红黑树节点颜色 红
 */
#define RB_RED 0
/**
 * 红黑树节点颜色 黑
 */
#define RB_BLACK 1

/**
 * 红黑树节点结构体
 */
struct RbNode {
    unsigned long rbParentColor;
    struct RbNode *rbRight;
    struct RbNode *rbLeft;
} __attribute__((aligned(sizeof(long))));
/* The alignment might seem pointless, but allegedly CRIS needs it */
/**
 * 红黑树根结构体
 */
struct RbRoot {
    struct RbNode *rbNode;
};

/**
 * 获取父节点
 */
#define RB_PARENT(r) ((struct RbNode *)((r)->rbParentColor & ~3))
/**
 * 获取节点颜色
 */
#define RB_COLOR(r) ((r)->rbParentColor & 1)
/**
 * 获取节点是否为红
 */
#define RB_IS_RED(r) (!RB_COLOR(r))
/**
 * 获取节点是否为黑
 */
#define RB_IS_BLACK(r) RB_COLOR(r)
/**
 * 设置节点为红色
 */
#define RB_SET_RED(r)             \
    do {                          \
        (r)->rbParentColor &= ~1; \
    } while (0)
/**
 * 设置节点为黑色
 */
#define RB_SET_BLACK(r)          \
    do {                         \
        (r)->rbParentColor |= 1; \
    } while (0)

/**
* @brief 功能描述:  设置节点的父节点
* @verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
@endverbatim

* @param[in]  node - 节点
* @param[in]  parent - 父节点
* @retval 无
*/
static inline void RbSetParent(struct RbNode *rb, struct RbNode *parent)
{
    rb->rbParentColor = (rb->rbParentColor & 3UL) | (unsigned long)(void *)parent;
}
/**
* @brief 功能描述:  设置节点的颜色
* @verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
@endverbatim

* @param[in]  node - 节点
* @param[in]  color - 颜色
* @retval 无
*/
static inline void RbSetColor(struct RbNode *rb, unsigned long color)
{
    rb->rbParentColor = (rb->rbParentColor & ~1) | color;
}
/**
 * 定义红黑树(根节点)
 */
#define RB_ROOT     \
    (struct RbRoot) \
    {               \
        NULL,       \
    }
/**
 * 根据节点获取节点所在结构体
 */
#define RB_ENTRY(ptr, type, member) CONTAINER_OF(ptr, type, member)

/**
 * 判断红黑树是否为空
 */
#define RB_EMPTY_ROOT(root) ((root)->rbNode == NULL)
/**
 * 判断节点是否无父节点
 */
#define RB_EMPTY_NODE(node) (RB_PARENT((node)) == (node))
/**
 * 清除节点的父节点
 */
#define RB_CLEAR_NODE(node) (RbSetParent(node, node))

/**
* @brief 功能描述:  将node插入红黑树,node默认为红色，并对违背红黑树性质的地方进行纠正
* @verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
@endverbatim

* @param[in]  node - 节点
* @param[in]  root - 根
* @retval 无
*/
extern void RbInsertColor(struct RbNode *node, struct RbRoot *root);
/**
* @brief 功能描述:  移除node节点并修正树
* @verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
@endverbatim

* @param[in]  node - 节点
* @param[in]  root - 根
* @retval 无
*/
extern void RbErase(struct RbNode *node, struct RbRoot *root);

/* Find logical next and previous nodes in a tree */
/**
* @brief 功能描述:  获取当前节点的下一个节点
* @verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
@endverbatim

* @param[in]  node - 当前节点
* @retval node，当前节点的下一个节点
*/
extern struct RbNode *RbNext(struct RbNode *node);
/**
* @brief 功能描述:  获取当前节点的前一个节点
* @verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
@endverbatim

* @param[in]  node - 当前节点
* @retval node，当前节点的前一个节点
*/
extern struct RbNode *RbPrev(struct RbNode *node);
/**
* @brief 功能描述:  获取红黑树第一个节点
* @verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
@endverbatim

* @param[in]  root - 树根
* @retval node，红黑树第一个节点
*/
extern struct RbNode *RbFirst(const struct RbRoot *root);

/* Fast replacement of a single node without remove/rebalance/add/rebalance */
/**
* @brief 功能描述:  替换节点
* @verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
@endverbatim

* @param[in]  victim - 待替换节点
* @param[in]  new_node - 新节点
* @param[in]  root - 树根
* @retval 无
*/
extern void RbReplaceNode(struct RbNode *victim, struct RbNode *newNode, struct RbRoot *root);

/**
* @brief 功能描述:  将节点link到新节点
* @verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
@endverbatim

* @param[in]  node - 待link节点
* @param[in]  parent - 父节点
* @param[in]  rb_link - link节点
* @retval 无
*/
static inline void RbLinkNode(struct RbNode *node, struct RbNode *parent, struct RbNode **rbLink)
{
    node->rbParentColor = (unsigned long)(void *)parent;
    node->rbLeft = node->rbRight = NULL;

    *rbLink = node;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* RB_TREE_H */