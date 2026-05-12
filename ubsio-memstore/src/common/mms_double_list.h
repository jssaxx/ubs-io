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

#ifndef MMSCORE_MMS_DOUBLE_LIST_H
#define MMSCORE_MMS_DOUBLE_LIST_H

#include <cstdlib>
#include "mms_def.h"

namespace ock {
namespace mms {
template <typename T> class MmsDoubleList {
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

#endif // MMSCORE_MMS_DOUBLE_LIST_H

