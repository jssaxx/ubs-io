/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

/**
* @file    rb_tree.c
* @brief   红黑树实现文件
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

#include "rb_tree.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/**
  \brief 功能描述:  对node左旋转
  \verbatim
   目标用户: 用户
   使用约束: 内部函数
   升级影响: no
  \endverbatim

  \param[in]  node - 节点
  \param[in]  root - 根
  \retval 无
*/
static void RbRotateLeft(struct RbNode *node, struct RbRoot *root)
{
    struct RbNode *right = node->rbRight;
    struct RbNode *parent = RB_PARENT(node);

    if ((node->rbRight = right->rbLeft)) {
        RbSetParent(right->rbLeft, node);
    }
    right->rbLeft = node;

    RbSetParent(right, parent);

    if (parent) {
        if (node == parent->rbLeft) {
            parent->rbLeft = right;
        } else {
            parent->rbRight = right;
        }
    } else {
        root->rbNode = right;
    }
    RbSetParent(node, right);
}

/**
  \brief 功能描述:  对node右旋转
  \verbatim
   目标用户: 用户
   使用约束: 内部函数
   升级影响: no
  \endverbatim

  \param[in]  node - 节点
  \param[in]  root - 根
  \retval 无
*/
static void RbRotateRight(struct RbNode *node, struct RbRoot *root)
{
    struct RbNode *left = node->rbLeft;
    struct RbNode *parent = RB_PARENT(node);

    if ((node->rbLeft = left->rbRight)) {
        RbSetParent(left->rbRight, node);
    }
    left->rbRight = node;

    RbSetParent(left, parent);

    if (parent) {
        if (node == parent->rbRight) {
            parent->rbRight = left;
        } else {
            parent->rbLeft = left;
        }
    } else {
        root->rbNode = left;
    }
    RbSetParent(node, left);
}

/**
  \brief 功能描述:  将node插入红黑树,node默认为红色，并对违背红黑树性质的地方进行纠正
  \verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
  \endverbatim

  \param[in]  node - 节点
  \param[in]  root - 根
  \retval 无
*/
void RbInsertColor(struct RbNode *node, struct RbRoot *root)
{
    struct RbNode *parent = NULL, *gparent = NULL;

    while ((parent = RB_PARENT(node)) && RB_IS_RED(parent)) {
        gparent = RB_PARENT(parent);
        if (parent == gparent->rbLeft) {
            register struct RbNode *uncle = gparent->rbRight;
            if (uncle && RB_IS_RED(uncle)) {
                RB_SET_BLACK(uncle);
                RB_SET_BLACK(parent);
                RB_SET_RED(gparent);
                node = gparent;

                continue;
            }

            if (parent->rbRight == node) {
                register struct RbNode *tmp = NULL;
                RbRotateLeft(parent, root);
                tmp = parent;
                parent = node;
                node = tmp;
            }

            RB_SET_BLACK(parent);
            RB_SET_RED(gparent);
            RbRotateRight(gparent, root);
        } else {
            register struct RbNode *uncle = gparent->rbLeft;
            if (uncle && RB_IS_RED(uncle)) {
                RB_SET_BLACK(uncle);
                RB_SET_BLACK(parent);
                RB_SET_RED(gparent);
                node = gparent;
                continue;
            }

            if (parent->rbLeft == node) {
                register struct RbNode *tmp = NULL;
                RbRotateRight(parent, root);
                tmp = parent;
                parent = node;
                node = tmp;
            }

            RB_SET_BLACK(parent);
            RB_SET_RED(gparent);
            RbRotateLeft(gparent, root);
        }
    }

    RB_SET_BLACK(root->rbNode);
}


/**
  \brief 功能描述:  删除黑色节点后对违背红黑树性质的地方进行纠正，以恢复平衡
  \verbatim
   目标用户: 用户
   使用约束: 内部函数
   升级影响: no
  \endverbatim

  \param[in]  node - 节点
  \param[in]  parent - node父节点
  \param[in]  root - 根
  \retval 无
*/
static void RbEraseColor(struct RbNode *node, struct RbNode *parent, struct RbRoot *root)
{
    struct RbNode *other = NULL;
    while ((!node || RB_IS_BLACK(node)) && node != root->rbNode) {
        if (parent->rbLeft == node) {
            other = parent->rbRight;
            if (RB_IS_RED(other)) {
                RB_SET_BLACK(other);
                RB_SET_RED(parent);
                RbRotateLeft(parent, root);
                other = parent->rbRight;
            }
            if ((!other->rbLeft || RB_IS_BLACK(other->rbLeft)) && (!other->rbRight || RB_IS_BLACK(other->rbRight))) {
                RB_SET_RED(other);
                node = parent;
                parent = RB_PARENT(node);
            } else {
                if (!other->rbRight || RB_IS_BLACK(other->rbRight)) {
                    RB_SET_BLACK(other->rbLeft);
                    RB_SET_RED(other);
                    RbRotateRight(other, root);
                    other = parent->rbRight;
                }
                RbSetColor(other, RB_COLOR(parent));
                RB_SET_BLACK(parent);
                RB_SET_BLACK(other->rbRight);
                RbRotateLeft(parent, root);
                node = root->rbNode;
                break;
            }
        } else {
            other = parent->rbLeft;
            if (RB_IS_RED(other)) {
                RB_SET_BLACK(other);
                RB_SET_RED(parent);
                RbRotateRight(parent, root);
                other = parent->rbLeft;
            }
            if ((!other->rbLeft || RB_IS_BLACK(other->rbLeft)) && (!other->rbRight || RB_IS_BLACK(other->rbRight))) {
                RB_SET_RED(other);
                node = parent;
                parent = RB_PARENT(node);
            } else {
                if (!other->rbLeft || RB_IS_BLACK(other->rbLeft)) {
                    RB_SET_BLACK(other->rbRight);
                    RB_SET_RED(other);
                    RbRotateLeft(other, root);
                    other = parent->rbLeft;
                }
                RbSetColor(other, RB_COLOR(parent));
                RB_SET_BLACK(parent);
                RB_SET_BLACK(other->rbLeft);
                RbRotateRight(parent, root);
                node = root->rbNode;
                break;
            }
        }
    }
    if (node) {
        RB_SET_BLACK(node);
    }
}


/**
  \brief 功能描述:  移除node节点并修正树
  \verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
  \endverbatim

  \param[in]  node - 节点
  \param[in]  root - 根
  \retval 无
*/
void RbErase(struct RbNode *node, struct RbRoot *root)
{
    struct RbNode *child = NULL, *parent = NULL;
    int color;

    if (!node->rbLeft) {
        child = node->rbRight;
    } else if (!node->rbRight) {
        child = node->rbLeft;
    } else {
        struct RbNode *old = node, *left;

        node = node->rbRight;
        while ((left = node->rbLeft) != NULL) {
            node = left;
        }

        if (RB_PARENT(old)) {
            if (RB_PARENT(old)->rbLeft == old) {
                RB_PARENT(old)->rbLeft = node;
            } else {
                RB_PARENT(old)->rbRight = node;
            }
        } else {
            root->rbNode = node;
        }

        child = node->rbRight;
        parent = RB_PARENT(node);
        color = RB_COLOR(node);

        if (parent == old) {
            parent = node;
        } else {
            if (child) {
                RbSetParent(child, parent);
            }
            parent->rbLeft = child;

            node->rbRight = old->rbRight;
            RbSetParent(old->rbRight, node);
        }

        node->rbParentColor = old->rbParentColor;
        node->rbLeft = old->rbLeft;
        RbSetParent(old->rbLeft, node);

        goto color;
    }

    parent = RB_PARENT(node);
    color = RB_COLOR(node);

    if (child) {
        RbSetParent(child, parent);
    }
    if (parent) {
        if (parent->rbLeft == node) {
            parent->rbLeft = child;
        } else {
            parent->rbRight = child;
        }
    } else {
        root->rbNode = child;
    }

color:
    if (color == RB_BLACK) {
        RbEraseColor(child, parent, root);
    }
}

/*
 * This function returns the first node (in sort order) of the tree.
 */
/**
  \brief 功能描述:  获取红黑树第一个节点
  \verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
  \endverbatim

  \param[in]  root - 树根

  \retval node，红黑树第一个节点
*/
struct RbNode *RbFirst(const struct RbRoot *root)
{
    struct RbNode *n;

    n = root->rbNode;
    if (!n) {
        return NULL;
    }
    while (n->rbLeft) {
        n = n->rbLeft;
    }
    return n;
}

/**
  \brief 功能描述:  获取当前节点的下一个节点
  \verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
  \endverbatim

  \param[in]  node - 当前节点

  \retval node，当前节点的下一个节点
*/
struct RbNode *RbNext(struct RbNode *node)
{
    struct RbNode *parent = NULL;

    if (RB_PARENT(node) == node) {
        return NULL;
    }

    /* If we have a right-hand child, go down and then left as far
       as we can. */
    if (node->rbRight) {
        node = node->rbRight;
        while (node->rbLeft) {
            node = node->rbLeft;
        }
        return (struct RbNode *)node;
    }

    /* No right-hand children.  Everything down and left is
       smaller than us, so any 'next' node must be in the general
       direction of our parent. Go up the tree; any time the
       ancestor is a right-hand child of its parent, keep going
       up. First time it's a left-hand child of its parent, said
       parent is our 'next' node. */
    while ((parent = RB_PARENT(node)) && node == parent->rbRight) {
        node = parent;
    }

    return parent;
}

/**
  \brief 功能描述:  获取当前节点的前一个节点
  \verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
  \endverbatim

  \param[in]  node - 当前节点

  \retval node，当前节点的前一个节点
*/
struct RbNode *RbPrev(struct RbNode *node)
{
    struct RbNode *parent = NULL;

    if (RB_PARENT(node) == node) {
        return NULL;
    }

    /* If we have a left-hand child, go down and then right as far
       as we can. */
    if (node->rbLeft) {
        node = node->rbLeft;
        while (node->rbRight) {
            node = node->rbRight;
        }
        return (struct RbNode *)node;
    }

    /* No left-hand children. Go up till we find an ancestor which
       is a right-hand child of its parent */
    while ((parent = RB_PARENT(node)) && node == parent->rbLeft) {
        node = parent;
    }

    return parent;
}

/**
  \brief 功能描述:  替换节点
  \verbatim
   目标用户: 用户
   使用约束: 无
   升级影响: no
  \endverbatim

  \param[in]  victim - 待替换节点
  \param[in]  new - 新节点
  \param[in]  root - 树根

  \retval 无
*/
void RbReplaceNode(struct RbNode *victim, struct RbNode *newNode, struct RbRoot *root)
{
    struct RbNode *parent = RB_PARENT(victim);

    /* Set the surrounding nodes to point to the replacement */
    if (parent) {
        if (victim == parent->rbLeft) {
            parent->rbLeft = newNode;
        } else {
            parent->rbRight = newNode;
        }
    } else {
        root->rbNode = newNode;
    }
    if (victim->rbLeft) {
        RbSetParent(victim->rbLeft, newNode);
    }
    if (victim->rbRight) {
        RbSetParent(victim->rbRight, newNode);
    }

    /* Copy the pointers/colour from the victim to the replacement */
    *newNode = *victim;
}


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif
