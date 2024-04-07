/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_DOUBLE_LIST_H
#define BOOSTIO_BIO_DOUBLE_LIST_H

#include "bio_def.h"
#include <cstdlib>

namespace ock {
namespace bio {
template <typename T> class BioDoubleList {
public:
    void Initialize(uint8_t lane)
    {
        this->lane = lane;
    }

    bool InsertAt(T position, T node)
    {
        if (UNLIKELY(node == nullptr)) {
            return false;
        }

        if (position == nullptr) {
            if (LIKELY(IsEmpty())) {
                head = node;
                tail = node;
                return true;
            } else {
                return false;
            }
        }

        if (position == head) {
            node->next[lane] = head;
            head->prev[lane] = node;
            head = node;
            head->prev[lane] = nullptr;
            return true;
        }

        auto prev = position->prev[lane];
        prev->next[lane] = node;
        node->prev[lane] = prev;
        node->next[lane] = position;
        position->prev[lane] = node;

        return true;
    }

    bool Remove(T node)
    {
        if (UNLIKELY(node == nullptr)) {
            return false;
        }
        if (UNLIKELY(IsEmpty())) {
            return false;
        }

        if (node == head && node == tail) {
            head = nullptr;
            tail = nullptr;
        } else if (node == head) {
            head = node->next[lane];
            head->prev[lane] = nullptr;
        } else if (node == tail) {
            tail = node->prev[lane];
            tail->next[lane] = nullptr;
            node->prev[lane] = nullptr;
        } else {
            auto prev = node->prev[lane];
            auto next = node->next[lane];
            prev->next[lane] = next;
            next->prev[lane] = prev;
        }
        node->prev[lane] = nullptr;
        node->next[lane] = nullptr;
        return true;
    }

    bool PushBack(T node)
    {
        if (tail == nullptr) {
            return InsertAt(nullptr, node);
        }
        if (UNLIKELY(node == nullptr)) {
            return false;
        }
        tail->next[lane] = node;
        node->prev[lane] = tail;
        tail = node;
        tail->next[lane] = nullptr;
        return true;
    }

    bool PushFront(T node)
    {
        return InsertAt(head, node, lane);
    }

    bool PopBack()
    {
        return Remove(tail);
    }

    bool PopFront()
    {
        return Remove(head);
    }

    bool IsEmpty()
    {
        return head == nullptr;
    }

    T Begin()
    {
        return head;
    }

    T End()
    {
        return tail;
    }

private:
    uint8_t lane = 0;
    T head = nullptr;
    T tail = nullptr;
};
}
}

#endif // BOOSTIO_BIO_DOUBLE_LIST_H
