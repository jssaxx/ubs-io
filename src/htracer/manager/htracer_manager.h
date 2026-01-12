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

#ifndef HTRACER_MANAGER_H
#define HTRACER_MANAGER_H

#include <cstdint>
#include <functional>
#include "common/htracer_info.h"

namespace ock {
namespace htracer {
constexpr int32_t MAX_SERVICE_NUM = 64;
constexpr int32_t MAX_INNER_ID_NUM = 1024;
constexpr uint32_t SHIFT_AMOUNT = 16;

class HtracerManager {
public:
    static inline HtracerInfo **GetTracePoints()
    {
        static HtracerInfo **tracePoints = CreateInstance();
        return tracePoints;
    }

    static inline HtracerInfo *GetPointPtr(int32_t tpId)
    {
        auto tracePoints = GetTracePoints();
        if (tracePoints == nullptr) {
            return nullptr;
        }
        uint32_t serviceId = GetServiceId(tpId);
        uint32_t innerId = GetInnerId(tpId);
        if (serviceId >= MAX_SERVICE_NUM || innerId >= MAX_INNER_ID_NUM) {
            return nullptr;
        }
        return &tracePoints[serviceId][innerId];
    }

    static inline void DelayBegin(int32_t tpId, std::string tpName)
    {
        auto ptr = GetPointPtr(tpId);
        if (ptr == nullptr) {
            return;
        }
        ptr->DelayBegin(tpName);
    }

    static inline void DelayEnd(int32_t tpId, const uint64_t &diff, int32_t retCode)
    {
        auto ptr = GetPointPtr(tpId);
        if (ptr == nullptr) {
            return;
        }
        ptr->DelayEnd(diff, retCode);
    }

    static inline bool IsEnable()
    {
        return mEnable;
    }

public:
    static bool mEnable;

private:
    static inline HtracerInfo **CreateInstance()
    {
        auto **instance = new (std::nothrow) HtracerInfo *[MAX_SERVICE_NUM];
        if (instance == nullptr) {
            return nullptr;
        } else {
            for (int i = 0; i < MAX_SERVICE_NUM; ++i) {
                instance[i] = nullptr;
            }
        }

        int ret = 0;
        uint16_t i = 0;
        for (i = 0; i < MAX_SERVICE_NUM; ++i) {
            instance[i] = new (std::nothrow) HtracerInfo[MAX_INNER_ID_NUM];
            if (instance[i] == nullptr) {
                ret = -1;
                break;
            }
        }

        if (ret != 0) {
            for (uint16_t j = 0; j < i; ++j) {
                delete instance[j];
            }
            delete[] instance;
            return nullptr;
        }
        return instance;
    }

    static inline uint32_t GetServiceId(uint32_t tpId)
    {
        return ((tpId >> SHIFT_AMOUNT) & 0xFFFF);
    }

    static inline uint32_t GetInnerId(uint32_t tpId)
    {
        return (tpId & 0xFFFF);
    }
};
}
}
#endif // HTRACER_MANAGER_H
