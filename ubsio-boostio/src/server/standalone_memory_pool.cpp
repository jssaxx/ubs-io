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

#include "standalone_memory_pool.h"

#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include "bio_def.h"
#include "bio_log.h"

namespace ock {
namespace bio {
namespace {
void TouchMemoryPages(void *address, uint64_t size)
{
    long sysPageSize = sysconf(_SC_PAGESIZE);
    uint64_t pageSize = sysPageSize > 0 ? static_cast<uint64_t>(sysPageSize) : NO_4096;
    volatile char *pages = reinterpret_cast<volatile char *>(address);
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        pages[offset] = 0;
    }
}
}

StandaloneMemoryPool::~StandaloneMemoryPool()
{
    Stop();
}

BResult StandaloneMemoryPool::Start(uint64_t blockSize, uint64_t poolSize)
{
    if (mIsStarted.load()) {
        return BIO_OK;
    }
    if (UNLIKELY(blockSize == 0 || blockSize < sizeof(uintptr_t))) {
        LOG_ERROR("Invalid standalone memory block size:" << blockSize << ".");
        return BIO_INVALID_PARAM;
    }

    mBlockSize = blockSize;
    mBlockCount = poolSize / blockSize;
    mPoolSize = mBlockCount * blockSize;
    if (mPoolSize == 0) {
        LOG_WARN("Standalone memory pool size is 0, memory cache allocation will fail.");
        mBlockPool.Start(0, mBlockSize, 0);
        mIsStarted.store(true);
        return BIO_OK;
    }

    void *address = mmap(nullptr, mPoolSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (UNLIKELY(address == MAP_FAILED)) {
        LOG_ERROR("Mmap standalone memory pool failed, size:" << mPoolSize << ", errno:" << errno <<
            ", reason:" << strerror(errno) << ".");
        mBlockSize = 0;
        mBlockCount = 0;
        mPoolSize = 0;
        return BIO_ALLOC_FAIL;
    }

    mBaseAddress = reinterpret_cast<uint64_t>(address);
    TouchMemoryPages(address, mPoolSize);
    auto ret = mBlockPool.Start(static_cast<uintptr_t>(mBaseAddress), mBlockSize, mBlockCount);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Start standalone memory block pool failed, result:" << ret << ".");
        int32_t munmapRet = munmap(reinterpret_cast<void *>(mBaseAddress), mPoolSize);
        if (UNLIKELY(munmapRet != 0)) {
            LOG_ERROR("Munmap standalone memory pool failed, address:" << mBaseAddress << ", size:" << mPoolSize <<
                ", errno:" << errno << ", reason:" << strerror(errno) << ".");
        }
        mBlockSize = 0;
        mBlockCount = 0;
        mPoolSize = 0;
        mBaseAddress = 0;
        return ret;
    }
    mIsStarted.store(true);
    LOG_INFO("Standalone memory pool start success, blockSize:" << mBlockSize << ", blockCount:" << mBlockCount <<
        ", poolSize:" << mPoolSize << ".");
    return BIO_OK;
}

void StandaloneMemoryPool::Stop()
{
    bool expected = true;
    if (!mIsStarted.compare_exchange_strong(expected, false)) {
        return;
    }

    if (mBaseAddress != 0 && mPoolSize != 0) {
        int32_t ret = munmap(reinterpret_cast<void *>(mBaseAddress), mPoolSize);
        if (UNLIKELY(ret != 0)) {
            LOG_ERROR("Munmap standalone memory pool failed, address:" << mBaseAddress << ", size:" << mPoolSize <<
                ", errno:" << errno << ", reason:" << strerror(errno) << ".");
        }
    }
    mBlockPool.Stop();
    mUsedBlock.store(0);
    mBlockSize = 0;
    mBlockCount = 0;
    mPoolSize = 0;
    mBaseAddress = 0;
}

BResult StandaloneMemoryPool::Alloc(uint64_t size, uint64_t *address)
{
    if (UNLIKELY(address == nullptr)) {
        return BIO_INVALID_PARAM;
    }
    *address = 0;
    if (UNLIKELY(!mIsStarted.load())) {
        LOG_ERROR("Standalone memory pool not ready.");
        return BIO_NOT_READY;
    }
    if (UNLIKELY(size == 0 || size > mBlockSize)) {
        LOG_ERROR("Invalid standalone memory alloc size:" << size << ", blockSize:" << mBlockSize << ".");
        return BIO_ALLOC_FAIL;
    }

    uintptr_t blockAddress = 0;
    auto ret = mBlockPool.AllocOne(blockAddress);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }
    mUsedBlock.fetch_add(NO_U64_1);
    *address = static_cast<uint64_t>(blockAddress);
    return BIO_OK;
}

void StandaloneMemoryPool::Free(uint64_t address)
{
    if (address == 0) {
        return;
    }
    if (UNLIKELY(!mIsStarted.load())) {
        LOG_ERROR("Standalone memory pool not ready.");
        return;
    }
    if (UNLIKELY(!IsValidBlockAddress(address))) {
        LOG_ERROR("Invalid standalone memory free address:" << address << ", base:" << mBaseAddress <<
            ", blockSize:" << mBlockSize << ", poolSize:" << mPoolSize << ".");
        return;
    }

    mBlockPool.ReleaseOne(static_cast<uintptr_t>(address));
    mUsedBlock.fetch_sub(NO_U64_1);
}

bool StandaloneMemoryPool::IsValidBlockAddress(uint64_t address) const
{
    if (mBaseAddress == 0 || mPoolSize == 0 || address < mBaseAddress || address >= mBaseAddress + mPoolSize) {
        return false;
    }
    return ((address - mBaseAddress) % mBlockSize) == 0;
}
}
}
