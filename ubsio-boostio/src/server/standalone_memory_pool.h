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

#ifndef STANDALONE_MEMORY_POOL_H
#define STANDALONE_MEMORY_POOL_H

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "bio_err.h"
#include "bio_ref.h"
#include "bio_types.h"
#include "net_block_pool.h"

namespace ock {
namespace bio {
class StandaloneMemoryPool;
using StandaloneMemoryPoolPtr = Ref<StandaloneMemoryPool>;

class StandaloneMemoryPool {
public:
    StandaloneMemoryPool() = default;
    ~StandaloneMemoryPool();

    StandaloneMemoryPool(const StandaloneMemoryPool &) = delete;
    StandaloneMemoryPool(StandaloneMemoryPool &&) = delete;
    StandaloneMemoryPool &operator = (const StandaloneMemoryPool &) = delete;
    StandaloneMemoryPool &operator = (StandaloneMemoryPool &&) = delete;

    BResult Start(uint64_t blockSize, uint64_t poolSize);
    void Stop();

    BResult Alloc(uint64_t size, uint64_t *address);
    void Free(uint64_t address);

    inline uint64_t GetUsedSize() const
    {
        return mUsedBlock.load() * mBlockSize;
    }

    inline uint64_t GetBlockSize() const
    {
        return mBlockSize;
    }

    inline uint64_t GetPoolSize() const
    {
        return mPoolSize;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    bool IsValidBlockAddress(uint64_t address) const;

private:
    std::atomic<bool> mIsStarted{ false };
    std::atomic<uint64_t> mUsedBlock{ 0 };
    NetBlockPool mBlockPool{};

    uint64_t mBlockSize = 0;
    uint64_t mBlockCount = 0;
    uint64_t mPoolSize = 0;
    uint64_t mBaseAddress = 0;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}

#endif // STANDALONE_MEMORY_POOL_H
