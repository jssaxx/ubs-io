/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef STANDALONE_SLOT_LEASE_H
#define STANDALONE_SLOT_LEASE_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "bio_err.h"

namespace ock {
namespace bio {

class StandaloneSlotLease {
public:
    StandaloneSlotLease() = default;
    ~StandaloneSlotLease();

    StandaloneSlotLease(const StandaloneSlotLease &) = delete;
    StandaloneSlotLease &operator=(const StandaloneSlotLease &) = delete;

    BResult Acquire(uint32_t slotCount, uint32_t &slotIndex);
    BResult PublishReady();
    void Release();

    bool IsAcquired() const noexcept
    {
        return mFd >= 0;
    }

    uint32_t SlotIndex() const noexcept
    {
        return mSlotIndex;
    }

private:
    BResult AcquireOnce(uint32_t slotCount, uint32_t &slotIndex, bool &retryStale);
    static BResult BuildShmName(std::string &shmName);
#ifdef DEBUG_UT
    static void SetShmNameForTest(const std::string &shmName);
#endif
    void ResetLocalState();

private:
    int32_t mFd{ -1 };
    uint32_t mSlotIndex{ UINT32_MAX };
    int32_t mPid{ -1 };
    uint64_t mPidStartTime{ 0 };
    bool mInitializer{ false };
    std::string mShmName;
};

}
}

#endif // STANDALONE_SLOT_LEASE_H
