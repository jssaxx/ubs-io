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

#include <functional>
#include <algorithm>
#include <regex>
#include <thread>
#include "mms_mem_allocator.h"

namespace ock {
namespace mms {

static constexpr uint8_t THREAD_CACHE_ALLOCATOR_NUM = 3;
static constexpr uint16_t FIXED_BLOCK_ORDER = UINT16_MAX;
static constexpr uint64_t FIXED_CACHE_BATCH_COUNT = CACHE_LIMIT_PER_THREAD / NO_2;
static constexpr uint16_t UINT64_BIT_NUM = 64;

struct AllocScratch {
    std::vector<BlockNode *> blocks;
    std::vector<uintptr_t> blockAddrs;
};

class BuddyOrderLockGuard {
public:
    BuddyOrderLockGuard(std::mutex locks[], uint16_t firstOrder, uint16_t lastOrder)
        : mLocks(locks), mFirstOrder(firstOrder), mLastOrder(lastOrder)
    {
        for (uint16_t order = mFirstOrder; order <= mLastOrder; ++order) {
            mLocks[order].lock();
        }
    }

    ~BuddyOrderLockGuard()
    {
        uint16_t order = mLastOrder;
        while (true) {
            mLocks[order].unlock();
            if (order == mFirstOrder) {
                break;
            }
            --order;
        }
    }

private:
    std::mutex *mLocks;
    uint16_t mFirstOrder;
    uint16_t mLastOrder;
};

static thread_local ThreadCache threadCache[THREAD_CACHE_ALLOCATOR_NUM] = {
    ThreadCache(MmsMemAllocator::Instance(MMAP_AREA_IOCTX)),
    ThreadCache(MmsMemAllocator::Instance(MMAP_AREA_INDEX)),
    ThreadCache(MmsMemAllocator::Instance(MMAP_AREA_VALUE))
};

static thread_local AllocScratch gAllocScratch[THREAD_CACHE_ALLOCATOR_NUM];

static inline AllocScratch &GetAllocScratch(MmapArea area)
{
    return gAllocScratch[static_cast<uint16_t>(area)];
}

static inline void ConvertBlockNodesToAddrs(const std::vector<BlockNode *> &blocks, std::vector<uintptr_t> &blockAddrs)
{
    blockAddrs.clear();
    blockAddrs.reserve(blocks.size());
    for (BlockNode *block : blocks) {
        blockAddrs.emplace_back(reinterpret_cast<uintptr_t>(block));
    }
}

static inline uint16_t CeilLog2(uint64_t value)
{
    if (value <= NO_1) {
        return 0;
    }
    return UINT64_BIT_NUM - static_cast<uint16_t>(__builtin_clzll(value - NO_1));
}

BResult MmsMemAllocator::InitMemNumaPool(uint16_t numaIndex)
{
    uint16_t index;

    NumaMemoryPool *pool = mNumaPool->CreatNumaMemPool(mNumaId[numaIndex]);
    if (UNLIKELY(pool == nullptr)) {
        MEM_LOG_ERROR("Create numa pool failed.");
        return MMS_ERR;
    }

    uint64_t offset = 0;
    for (index = 0; index < mBlockNum; index++) {
        uint64_t blockSize = mBlockSize[index] + sizeof(BlockHeader);
        uint64_t blockSizeRate = mNumaSize[numaIndex] * mBlockRate[index] / NO_10;
        uint64_t blockCount = blockSizeRate / blockSize;
        uint64_t current = mNumaAddress[numaIndex] + offset;
        for (uint64_t i = 0; i < blockCount; i++) {
            BlockHeader *header = reinterpret_cast<BlockHeader *>(current);
            header->numaId = mNumaId[numaIndex];
            header->order = FIXED_BLOCK_ORDER;
            header->blockSize = mBlockSize[index];
            BlockNode *block = reinterpret_cast<BlockNode *>(current + sizeof(BlockHeader));
            pool->AddOneBlock(index, block);
            current += blockSize;
        }
        offset += blockSizeRate;
        MEM_LOG_DEBUG("Init numa pool success, numa id:" << mNumaId[numaIndex] << ", block size:" <<
            blockSize << ", block count:" << blockCount);
    }
    return MMS_OK;
}

BResult MmsMemAllocator::InitBuddyNumaPool(uint16_t numaIndex)
{
    BuddyNumaMemoryPool *pool = mBuddyNumaPool->CreatNumaMemPool(mNumaId[numaIndex], mNumaAddress[numaIndex],
                                                                 mNumaSize[numaIndex], mBlockSize[NO_0]);
    if (UNLIKELY(pool == nullptr)) {
        MEM_LOG_ERROR("Create buddy numa pool failed.");
        return MMS_ERR;
    }
    MEM_LOG_DEBUG("Init buddy numa pool success, numa id:" << mNumaId[numaIndex] << ", base block size:"
                                                           << mBlockSize[NO_0] << ".");
    return MMS_OK;
}

void MmsMemAllocator::LoadMemAllocOptions(const MemAllocOptions &options)
{
    for (uint16_t index = 0; index < options.numaNum; index++) {
        mNumaId[index] = options.numaId[index];
        mNumaSize[index] = options.numaSize[index];
        mNumaAddress[index] = options.numaAddress[index];
    }
    mNumaNum = options.numaNum;
    mAllocMode = options.allocMode;

    for (uint16_t index = 0; index < options.blockNum; index++) {
        mBlockRate[index] = options.blockRate[index];
        mBlockSize[index] = options.blockSize[index];
    }
    mBlockNum = options.blockNum;
}

BResult MmsMemAllocator::CalculateBuddyMaxAllocSize()
{
    if (UNLIKELY(mBlockNum == 0 || mBlockSize[NO_0] == 0)) {
        MEM_LOG_ERROR("Invalid buddy base block size.");
        return MMS_INVALID_PARAM;
    }

    mBuddyUnitSize = static_cast<uint64_t>(mBlockSize[NO_0]) + sizeof(BlockHeader);
    mBuddyMaxAllocSize = 0;
    for (uint16_t index = 0; index < mNumaNum; ++index) {
        uint64_t unitCount = mNumaSize[index] / mBuddyUnitSize;
        if (unitCount == 0) {
            continue;
        }

        uint64_t orderUnits = NO_1;
        while ((orderUnits <= (UINT64_MAX >> NO_1)) && ((orderUnits << NO_1) <= unitCount)) {
            orderUnits <<= NO_1;
        }
        mBuddyMaxAllocSize = std::max(mBuddyMaxAllocSize, mBuddyUnitSize * orderUnits - sizeof(BlockHeader));
    }

    if (UNLIKELY(mBuddyMaxAllocSize == 0)) {
        MEM_LOG_ERROR("Invalid buddy memory size.");
        return MMS_INVALID_PARAM;
    }
    return MMS_OK;
}

BResult MmsMemAllocator::InitBuddyMemPools()
{
    BResult ret = CalculateBuddyMaxAllocSize();
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    mBuddyNumaPool = std::make_shared<BuddyNumaPoolManager>();
    for (uint16_t index = 0; index < mNumaNum; index++) {
        ret = InitBuddyNumaPool(index);
        if (UNLIKELY(ret != MMS_OK)) {
            MEM_LOG_ERROR("Init buddy numa pool failed, numa id:" << mNumaId[index] << ".");
            return ret;
        }
    }
    return MMS_OK;
}

BResult MmsMemAllocator::InitFixedMemPools()
{
    mNumaPool = std::make_shared<NumaPoolManager>();
    for (uint16_t index = 0; index < mNumaNum; index++) {
        auto ret = InitMemNumaPool(index);
        if (UNLIKELY(ret != MMS_OK)) {
            MEM_LOG_ERROR("Init numa pool failed, numa id:" << mNumaId[index] << ".");
            return ret;
        }
    }
    return MMS_OK;
}

BResult MmsMemAllocator::Initialize(MemAllocOptions &options)
{
    LoadMemAllocOptions(options);
    if (mAllocMode == MemAllocOptions::ALLOC_MODE_BUDDY) {
        return InitBuddyMemPools();
    }
    return InitFixedMemPools();
}

void MmsMemAllocator::Exit(void)
{
    return;
}

BResult MmsMemAllocator::BuddyAllocFromPool(uint16_t preferNumaId, uint16_t order, uint16_t &allocNumaId,
                                            uintptr_t &blockAddr)
{
    allocNumaId = preferNumaId;
    BResult ret = mBuddyNumaPool->AllocFromPool(preferNumaId, order, blockAddr);
    if (LIKELY(ret == MMS_OK)) {
        return MMS_OK;
    }

    return mBuddyNumaPool->AllocFromOtherPool(preferNumaId, order, allocNumaId, blockAddr);
}

BResult MmsMemAllocator::BuddyAllocBatchFromPool(uint16_t preferNumaId, uint16_t order, uintptr_t blockAddrs[],
                                                 uint64_t requestCount, uint64_t &actualCount)
{
    actualCount = 0;
    BResult ret = mBuddyNumaPool->AllocBatchFromPool(preferNumaId, order, blockAddrs, requestCount, actualCount);
    if (actualCount == requestCount) {
        return MMS_OK;
    }
    if (UNLIKELY(ret != MMS_OK && ret != MMS_ALLOC_FAIL && ret != MMS_NOT_EXISTS)) {
        return ret;
    }

    uint64_t otherCount = 0;
    ret = mBuddyNumaPool->AllocBatchFromOtherPool(preferNumaId, order, blockAddrs + actualCount,
                                                  requestCount - actualCount, otherCount);
    actualCount += otherCount;
    return actualCount > 0 ? MMS_OK : ret;
}

BResult MmsMemAllocator::GetBuddyAllocOrder(uint64_t size, uint16_t &order) const
{
    if (UNLIKELY(size > mBuddyMaxAllocSize)) {
        MEM_LOG_ERROR("Invalid buddy alloc size:" << size << ", max size:" << mBuddyMaxAllocSize << ".");
        return MMS_ALLOC_FAIL;
    }

    if (UNLIKELY(mBuddyUnitSize == 0 || size > UINT64_MAX - sizeof(BlockHeader))) {
        MEM_LOG_ERROR("Invalid buddy alloc size:" << size << ".");
        return MMS_ALLOC_FAIL;
    }

    const uint64_t totalSize = size + sizeof(BlockHeader);
    uint64_t unitCount = (totalSize - NO_1) / mBuddyUnitSize + NO_1;
    order = CeilLog2(unitCount);
    return MMS_OK;
}

BResult MmsMemAllocator::ReturnBatchBuddyBlocksToPool(const uintptr_t blockAddrs[], uint64_t count)
{
    if (count == 0) {
        return MMS_OK;
    }
    if (UNLIKELY(count > BUDDY_CACHE_LIMIT_PER_THREAD)) {
        return MMS_INVALID_PARAM;
    }

    uintptr_t numaBlocks[MAX_NUMAS_NUM][BUDDY_CACHE_LIMIT_PER_THREAD];
    uint64_t numaCounts[MAX_NUMAS_NUM] = {0};
    auto firstHeader = reinterpret_cast<BlockHeader*>(blockAddrs[NO_0] - sizeof(BlockHeader));
    uint16_t order = firstHeader->order;
    for (uint64_t index = 0; index < count; ++index) {
        auto header = reinterpret_cast<BlockHeader*>(blockAddrs[index] - sizeof(BlockHeader));
        uint16_t numaIndex = 0;
        while (numaIndex < mNumaNum && mNumaId[numaIndex] != header->numaId) {
            ++numaIndex;
        }
        if (UNLIKELY(numaIndex == mNumaNum || header->order != order)) {
            return MMS_INVALID_PARAM;
        }
        numaBlocks[numaIndex][numaCounts[numaIndex]++] = blockAddrs[index];
    }

    for (uint16_t numaIndex = 0; numaIndex < mNumaNum; ++numaIndex) {
        if (numaCounts[numaIndex] == 0) {
            continue;
        }
        BResult ret = mBuddyNumaPool->FreeBatchToPool(mNumaId[numaIndex], order, numaBlocks[numaIndex],
                                                      numaCounts[numaIndex]);
        if (UNLIKELY(ret != MMS_OK)) {
            return ret;
        }
    }
    return MMS_OK;
}

BResult MmsMemAllocator::ReturnBatchBlocksToPool(uint64_t blockIndex, const std::vector<uintptr_t> &blockAddrs)
{
    std::vector<BlockNode *> numaBlocks[MAX_NUMAS_NUM];
    for (uintptr_t blockAddr : blockAddrs) {
        auto header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
        uint16_t numaIndex = 0;
        while (numaIndex < mNumaNum && mNumaId[numaIndex] != header->numaId) {
            ++numaIndex;
        }
        if (UNLIKELY(numaIndex == mNumaNum)) {
            MEM_LOG_ERROR("Invalid block NUMA id:" << header->numaId << ".");
            return MMS_INVALID_PARAM;
        }
        numaBlocks[numaIndex].emplace_back(reinterpret_cast<BlockNode*>(blockAddr));
    }

    for (uint16_t numaIndex = 0; numaIndex < mNumaNum; ++numaIndex) {
        if (numaBlocks[numaIndex].empty()) {
            continue;
        }
        BResult ret = mNumaPool->AddBatchBlocksToPool(mNumaId[numaIndex], blockIndex, numaBlocks[numaIndex]);
        if (UNLIKELY(ret != MMS_OK)) {
            return ret;
        }
    }
    return MMS_OK;
}

BResult MmsMemAllocator::BuddyAllocFromThreadCacheMiss(uint16_t order, uint16_t &numaId, uintptr_t &blockAddr)
{
    uint16_t preferNumaId = numaId;
    uintptr_t blocks[BUDDY_CACHE_REFILL_COUNT];
    uint64_t count = 0;
    BResult ret = BuddyAllocBatchFromPool(numaId, order, blocks, BUDDY_CACHE_REFILL_COUNT, count);
    if (count > 0) {
        blockAddr = blocks[--count];
        auto header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
        numaId = header->numaId;
        ret = threadCache[mArea].AddBatchBuddyBlocksToCache(order, blocks, count);
        if (UNLIKELY(ret != MMS_OK)) {
            return ReturnBatchBuddyBlocksToPool(blocks, count);
        }
        threadCache[mArea].RecordAllocNumaId(preferNumaId, numaId);
        return MMS_OK;
    }

    ret = threadCache[mArea].FlushBuddyCaches();
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }
    ret = BuddyAllocFromPool(preferNumaId, order, numaId, blockAddr);
    if (LIKELY(ret == MMS_OK)) {
        threadCache[mArea].RecordAllocNumaId(preferNumaId, numaId);
    }
    return ret;
}

BResult MmsMemAllocator::BuddyAllocDirect(uint16_t order, uint16_t &numaId, uintptr_t &blockAddr)
{
    uint16_t preferNumaId = numaId;
    BResult ret = BuddyAllocFromPool(preferNumaId, order, numaId, blockAddr);
    if (UNLIKELY(ret != MMS_OK)) {
        ret = threadCache[mArea].FlushBuddyCaches();
        if (UNLIKELY(ret != MMS_OK)) {
            return ret;
        }
        ret = BuddyAllocFromPool(preferNumaId, order, numaId, blockAddr);
    }
    if (LIKELY(ret == MMS_OK)) {
        threadCache[mArea].RecordAllocNumaId(preferNumaId, numaId);
    }
    return ret;
}

BResult MmsMemAllocator::BuddyAlloc(uint64_t size, uint16_t &numaId, uintptr_t &blockAddr)
{
    if (UNLIKELY(mBuddyNumaPool == nullptr)) {
        return MMS_NOT_INITIALIZED;
    }

    numaId = threadCache[mArea].GetNumaId();
    uint16_t order = 0;
    BResult ret = GetBuddyAllocOrder(size, order);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    if (order <= BUDDY_THREAD_CACHE_MAX_ORDER &&
        threadCache[mArea].GetOneBuddyBlockFromCache(order, blockAddr) == MMS_OK) {
        auto header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
        numaId = header->numaId;
        return MMS_OK;
    }

    numaId = threadCache[mArea].GetAllocNumaId();
    ret = (order <= BUDDY_THREAD_CACHE_MAX_ORDER) ?
        BuddyAllocFromThreadCacheMiss(order, numaId, blockAddr) : BuddyAllocDirect(order, numaId, blockAddr);
    if (UNLIKELY(ret != MMS_OK)) {
        if (ret == MMS_ALLOC_FAIL) {
            MEM_LOG_ERROR("All buddy pools are empty, alloc failed, size:" << size << ", order:" << order << ".");
        }
        return ret;
    }
    return MMS_OK;
}

BResult MmsMemAllocator::MmsAlloc(uint64_t size, uint16_t &numaId, uintptr_t &blockAddr)
{
    if (mAllocMode == MemAllocOptions::ALLOC_MODE_BUDDY) {
        return BuddyAlloc(size, numaId, blockAddr);
    }

    uint32_t blockIndex;

    for (blockIndex = 0; blockIndex < mBlockNum; blockIndex++) {
        if (mBlockSize[blockIndex] >= size) {
            break;
        }
    }
    if (blockIndex == mBlockNum) {
        MEM_LOG_ERROR("Invalid size:" << size);
        return MMS_ERR;
    }

    numaId = threadCache[mArea].GetNumaId();

    BResult ret = threadCache[mArea].GetOneBlockFromCache(blockIndex, blockAddr);
    if (LIKELY((ret == MMS_OK))) {
        auto header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
        numaId = header->numaId;
        MEM_LOG_DEBUG("Thread cache hit, block size:" << mBlockSize[blockIndex] << ".");
        return MMS_OK;
    }

    numaId = threadCache[mArea].GetAllocNumaId();
    uint16_t preferNumaId = numaId;
    AllocScratch &scratch = GetAllocScratch(mArea);
    std::vector<BlockNode *> &blocks = scratch.blocks;
    blocks.reserve(FIXED_CACHE_BATCH_COUNT);
    ret = mNumaPool->GetBatchBlocksFromPool(numaId, blockIndex, blocks);
    if (UNLIKELY(ret != MMS_OK)) {
        ret = mNumaPool->GetBatchBlocksFromOtherPool(numaId, blockIndex, blocks);
        if (UNLIKELY(ret != MMS_OK)) {
            MEM_LOG_ERROR("All pools is empty, alloc block failed, size:" << mBlockSize[blockIndex] << ".");
            return MMS_ALLOC_FAIL;
        }
    }

    blockAddr = reinterpret_cast<uintptr_t>(blocks.back());
    auto header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
    uint16_t allocNumaId = header->numaId;
    blocks.pop_back();

    std::vector<uintptr_t> &blockAddrs = scratch.blockAddrs;
    ConvertBlockNodesToAddrs(blocks, blockAddrs);

    ret = threadCache[mArea].AddBatchBlocksToCache(blockIndex, blockAddrs);
    if (UNLIKELY(ret != MMS_OK)) {
        MEM_LOG_ERROR("Error thread cache.");
        return MMS_INNER_ERR;
    }

    threadCache[mArea].RecordAllocNumaId(preferNumaId, allocNumaId);
    numaId = allocNumaId;
    MEM_LOG_DEBUG("Mem pool hit, block size:" << mBlockSize[blockIndex] << ".");
    return MMS_OK;
}

BResult MmsMemAllocator::BuddyAllocPreferNuma(uint64_t size, uint16_t preferNumaId, uint16_t &numaId,
                                              uintptr_t &blockAddr)
{
    uint16_t order;
    BResult ret = GetBuddyAllocOrder(size, order);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    if (order <= BUDDY_THREAD_CACHE_MAX_ORDER &&
        threadCache[mArea].GetOneBuddyBlockFromCachePreferNuma(order, preferNumaId, blockAddr) == MMS_OK) {
        auto header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
        numaId = header->numaId;
        return MMS_OK;
    }
    if (order <= BUDDY_THREAD_CACHE_MAX_ORDER) {
        numaId = preferNumaId;
        return BuddyAllocFromThreadCacheMiss(order, numaId, blockAddr);
    }
    return BuddyAllocFromPool(preferNumaId, order, numaId, blockAddr);
}

BResult MmsMemAllocator::FixedAllocPreferNuma(uint64_t size, uint16_t preferNumaId, uint16_t &numaId,
                                              uintptr_t &blockAddr)
{
    uint32_t blockIndex;
    for (blockIndex = 0; blockIndex < mBlockNum; blockIndex++) {
        if (mBlockSize[blockIndex] >= size) {
            break;
        }
    }
    if (blockIndex == mBlockNum) {
        MEM_LOG_ERROR("Invalid size:" << size << ".");
        return MMS_ERR;
    }

    BResult ret = threadCache[mArea].GetOneBlockFromCachePreferNuma(blockIndex, preferNumaId, blockAddr);
    if (LIKELY(ret == MMS_OK)) {
        auto header = reinterpret_cast<BlockHeader *>(blockAddr - sizeof(BlockHeader));
        numaId = header->numaId;
        return MMS_OK;
    }

    AllocScratch &scratch = GetAllocScratch(mArea);
    std::vector<BlockNode *> &blocks = scratch.blocks;
    blocks.reserve(FIXED_CACHE_BATCH_COUNT);
    ret = mNumaPool->GetBatchBlocksFromPool(preferNumaId, blockIndex, blocks);
    if (UNLIKELY(ret != MMS_OK)) {
        ret = threadCache[mArea].GetOneBlockFromCache(blockIndex, blockAddr);
        if (LIKELY(ret == MMS_OK)) {
            auto header = reinterpret_cast<BlockHeader *>(blockAddr - sizeof(BlockHeader));
            numaId = header->numaId;
            return MMS_OK;
        }
        ret = mNumaPool->GetBatchBlocksFromOtherPool(preferNumaId, blockIndex, blocks);
        if (UNLIKELY(ret != MMS_OK)) {
            MEM_LOG_ERROR("All pools is empty, alloc block failed, size:" << mBlockSize[blockIndex] << ".");
            return MMS_ALLOC_FAIL;
        }
    }

    blockAddr = reinterpret_cast<uintptr_t>(blocks.back());
    auto header = reinterpret_cast<BlockHeader *>(blockAddr - sizeof(BlockHeader));
    numaId = header->numaId;
    blocks.pop_back();
    std::vector<uintptr_t> &restBlocks = scratch.blockAddrs;
    ConvertBlockNodesToAddrs(blocks, restBlocks);
    if (restBlocks.empty()) {
        return MMS_OK;
    }
    ret = threadCache[mArea].AddBatchBlocksToCache(blockIndex, restBlocks);
    if (UNLIKELY(ret != MMS_OK)) {
        ret = ReturnBatchBlocksToPool(blockIndex, restBlocks);
        if (UNLIKELY(ret != MMS_OK)) {
            MEM_LOG_ERROR("Return unused preferred blocks failed, ret:" << ret << ".");
            return ret;
        }
    }
    return MMS_OK;
}

BResult MmsMemAllocator::MmsAllocPreferNuma(uint64_t size, uint16_t preferNumaId, uint16_t &numaId,
                                            uintptr_t &blockAddr)
{
    UpdateNumaId(preferNumaId);
    if (mAllocMode == MemAllocOptions::ALLOC_MODE_BUDDY) {
        return BuddyAllocPreferNuma(size, preferNumaId, numaId, blockAddr);
    }
    return FixedAllocPreferNuma(size, preferNumaId, numaId, blockAddr);
}

BResult MmsMemAllocator::ReturnBuddyBlockToPool(uintptr_t blockAddr)
{
    auto header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
    return BuddyFree(blockAddr, header);
}

BResult MmsMemAllocator::BuddyFree(uintptr_t blockAddr, BlockHeader *header)
{
    if (UNLIKELY(mBuddyNumaPool == nullptr)) {
        return MMS_NOT_INITIALIZED;
    }

    BResult ret = mBuddyNumaPool->FreeToPool(header->numaId, blockAddr);
    if (UNLIKELY(ret != MMS_OK)) {
        MEM_LOG_ERROR("Return buddy block failed, numaId:" << header->numaId << ", ret:" << ret << ".");
    }
    return ret;
}

BResult MmsMemAllocator::BuddyFreeToThreadCache(uintptr_t blockAddr, BlockHeader *header)
{
    uint16_t order = header->order;
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER) {
        return BuddyFree(blockAddr, header);
    }

    if (LIKELY(threadCache[mArea].AddOneBuddyBlockToCache(order, blockAddr) == MMS_OK)) {
        return MMS_OK;
    }

    uintptr_t blocks[BUDDY_CACHE_DRAIN_COUNT];
    uint64_t count = threadCache[mArea].GetBatchBuddyBlocksFromCachePreferNuma(order, header->numaId, blocks,
                                                                               BUDDY_CACHE_DRAIN_COUNT);
    BResult ret = ReturnBatchBuddyBlocksToPool(blocks, count);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    if (LIKELY(threadCache[mArea].AddOneBuddyBlockToCache(order, blockAddr) == MMS_OK)) {
        return MMS_OK;
    }

    return BuddyFree(blockAddr, header);
}

BResult MmsMemAllocator::MmsFree(uintptr_t blockAddr)
{
    BlockHeader *header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
    if (mAllocMode == MemAllocOptions::ALLOC_MODE_BUDDY) {
        return BuddyFreeToThreadCache(blockAddr, header);
    }

    uint64_t blockSize = header->blockSize;
    uint32_t blockIndex;

    for (blockIndex = 0; blockIndex < mBlockNum; blockIndex++) {
        if (mBlockSize[blockIndex] >= blockSize) {
            break;
        }
    }
    if (blockIndex == mBlockNum) {
        MEM_LOG_ERROR("Invalid size:" << blockSize);
        return MMS_ERR;
    }

    if (LIKELY(threadCache[mArea].AddOneBlockToCache(blockIndex, blockAddr) == MMS_OK)) {
        MEM_LOG_DEBUG("Return one block to thread cache, block size:" << blockSize << ".");
        return MMS_OK;
    }

    std::vector<uintptr_t> &blocks = GetAllocScratch(mArea).blockAddrs;
    threadCache[mArea].GetBatchBlocksFromCachePreferNuma(blockIndex, header->numaId, FIXED_CACHE_BATCH_COUNT, blocks);
    BResult ret = ReturnBatchBlocksToPool(blockIndex, blocks);
    if (UNLIKELY(ret != MMS_OK)) {
        MEM_LOG_ERROR("Add blocks to numa pool failed, ret:" << ret << ".");
        return ret;
    }

    MEM_LOG_DEBUG("Return one block to thread cache, block size:" << blockSize << ".");
    return threadCache[mArea].AddOneBlockToCache(blockIndex, blockAddr); // 重新归还块到线程的缓存里
}

BResult BuddyNumaMemoryPool::Start(uint16_t numaId, uint64_t address, uint64_t size, uint64_t baseBlockSize)
{
    if (UNLIKELY(baseBlockSize < sizeof(BuddyBlockNode) || size <= sizeof(BlockHeader))) {
        return MMS_INVALID_PARAM;
    }

    mNumaId = numaId;
    mBaseAddr = address;
    mUnitSize = baseBlockSize + sizeof(BlockHeader);
    mUnitCount = size / mUnitSize;
    if (UNLIKELY(mUnitCount == 0)) {
        return MMS_INVALID_PARAM;
    }
    mOrders.reset(new(std::nothrow) std::atomic<uint16_t>[mUnitCount]);
    mFreeStates.reset(new(std::nothrow) std::atomic<uint8_t>[mUnitCount]);
    if (UNLIKELY(mOrders == nullptr || mFreeStates == nullptr)) {
        return MMS_ALLOC_FAIL;
    }
    for (uint64_t index = 0; index < mUnitCount; ++index) {
        mOrders[index].store(0, std::memory_order_relaxed);
        mFreeStates[index].store(0, std::memory_order_relaxed);
    }

    mMaxOrder = 0;
    uint64_t units = mUnitCount;
    while (units > NO_1) {
        units >>= NO_1;
        ++mMaxOrder;
    }

    uint64_t unitIndex = 0;
    uint64_t remainUnits = mUnitCount;
    while (remainUnits > 0) {
        uint16_t order = 0;
        uint64_t orderUnits = NO_1;
        while ((order < mMaxOrder) && ((orderUnits << NO_1) <= remainUnits) &&
               ((unitIndex & ((orderUnits << NO_1) - NO_1)) == 0)) {
            orderUnits <<= NO_1;
            ++order;
        }
        AddFreeChunk(ChunkAddr(unitIndex), order);
        unitIndex += orderUnits;
        remainUnits -= orderUnits;
    }
    return MMS_OK;
}

void BuddyNumaMemoryPool::AddFreeChunk(uint64_t chunkAddr, uint16_t order)
{
    std::lock_guard<std::mutex> lock(mFreeLocks[order]);
    AddFreeChunkUnlocked(chunkAddr, order);
}

void BuddyNumaMemoryPool::AddFreeChunkUnlocked(uint64_t chunkAddr, uint16_t order)
{
    uint64_t unitIndex = UnitIndex(chunkAddr);
    auto header = reinterpret_cast<BlockHeader *>(chunkAddr);
    header->numaId = mNumaId;
    header->order = order;
    header->blockSize = PayloadSize(order);
    auto block = reinterpret_cast<BuddyBlockNode *>(chunkAddr + sizeof(BlockHeader));

    block->prev = nullptr;
    block->next = mFreeHeads[order];
    if (mFreeHeads[order] != nullptr) {
        mFreeHeads[order]->prev = block;
    }
    mOrders[unitIndex].store(order, std::memory_order_release);
    mFreeStates[unitIndex].store(NO_1, std::memory_order_release);
    mFreeHeads[order] = block;
}

BuddyBlockNode *BuddyNumaMemoryPool::PopFreeChunk(uint16_t order)
{
    std::lock_guard<std::mutex> lock(mFreeLocks[order]);
    return PopFreeChunkUnlocked(order);
}

BuddyBlockNode *BuddyNumaMemoryPool::PopFreeChunkUnlocked(uint16_t order)
{
    BuddyBlockNode *block = mFreeHeads[order];
    if (block == nullptr) {
        return nullptr;
    }

    mFreeHeads[order] = block->next;
    if (mFreeHeads[order] != nullptr) {
        mFreeHeads[order]->prev = nullptr;
    }
    block->next = nullptr;
    block->prev = nullptr;

    uint64_t chunkAddr = reinterpret_cast<uint64_t>(block) - sizeof(BlockHeader);
    uint64_t unitIndex = UnitIndex(chunkAddr);
    mFreeStates[unitIndex].store(0, std::memory_order_release);
    return block;
}

bool BuddyNumaMemoryPool::RemoveFreeChunk(uint64_t unitIndex, uint16_t order)
{
    std::lock_guard<std::mutex> lock(mFreeLocks[order]);
    return RemoveFreeChunkUnlocked(unitIndex, order);
}

bool BuddyNumaMemoryPool::RemoveFreeChunkUnlocked(uint64_t unitIndex, uint16_t order)
{
    if (mFreeStates[unitIndex].load(std::memory_order_acquire) == 0 ||
        mOrders[unitIndex].load(std::memory_order_acquire) != order) {
        return false;
    }

    uint64_t chunkAddr = ChunkAddr(unitIndex);
    auto block = reinterpret_cast<BuddyBlockNode *>(chunkAddr + sizeof(BlockHeader));
    if (block->prev != nullptr) {
        block->prev->next = block->next;
    } else {
        mFreeHeads[order] = block->next;
    }
    if (block->next != nullptr) {
        block->next->prev = block->prev;
    }
    block->prev = nullptr;
    block->next = nullptr;
    mFreeStates[unitIndex].store(0, std::memory_order_release);
    return true;
}

BResult BuddyNumaMemoryPool::AllocUnlocked(uint16_t order, uintptr_t &blockAddr)
{
    uint16_t curOrder = order;
    BuddyBlockNode *block = nullptr;
    for (; curOrder <= mMaxOrder; ++curOrder) {
        block = PopFreeChunkUnlocked(curOrder);
        if (block != nullptr) {
            break;
        }
    }
    if (block == nullptr) {
        return MMS_ALLOC_FAIL;
    }

    uint64_t chunkAddr = reinterpret_cast<uint64_t>(block) - sizeof(BlockHeader);
    while (curOrder > order) {
        --curOrder;
        uint64_t buddyAddr = chunkAddr + mUnitSize * OrderUnits(curOrder);
        AddFreeChunkUnlocked(buddyAddr, curOrder);
    }

    auto header = reinterpret_cast<BlockHeader *>(chunkAddr);
    header->numaId = mNumaId;
    header->order = order;
    header->blockSize = PayloadSize(order);
    blockAddr = chunkAddr + sizeof(BlockHeader);
    return MMS_OK;
}

BResult BuddyNumaMemoryPool::Alloc(uint16_t order, uintptr_t &blockAddr)
{
    if (UNLIKELY(order > mMaxOrder || order > BUDDY_MAX_ORDER)) {
        return MMS_ALLOC_FAIL;
    }

    BuddyOrderLockGuard lock(mFreeLocks, order, mMaxOrder);
    return AllocUnlocked(order, blockAddr);
}

BResult BuddyNumaMemoryPool::AllocBatch(uint16_t order, uintptr_t blockAddrs[], uint64_t requestCount,
                                        uint64_t &actualCount)
{
    actualCount = 0;
    if (UNLIKELY(blockAddrs == nullptr || requestCount == 0)) {
        return MMS_INVALID_PARAM;
    }
    if (UNLIKELY(order > mMaxOrder || order > BUDDY_MAX_ORDER)) {
        return MMS_ALLOC_FAIL;
    }

    BuddyOrderLockGuard lock(mFreeLocks, order, mMaxOrder);
    while (actualCount < requestCount) {
        BResult ret = AllocUnlocked(order, blockAddrs[actualCount]);
        if (ret != MMS_OK) {
            return actualCount > 0 ? MMS_OK : ret;
        }
        ++actualCount;
    }
    return MMS_OK;
}

BResult BuddyNumaMemoryPool::FreeUnlocked(uintptr_t blockAddr)
{
    uint64_t chunkAddr = blockAddr - sizeof(BlockHeader);
    auto header = reinterpret_cast<BlockHeader *>(chunkAddr);
    uint16_t order = header->order;
    uint64_t unitIndex = UnitIndex(chunkAddr);
    while (order < mMaxOrder) {
        uint64_t orderUnits = OrderUnits(order);
        uint64_t buddyUnitIndex = unitIndex ^ orderUnits;
        if (buddyUnitIndex + orderUnits > mUnitCount) {
            break;
        }

        if (!RemoveFreeChunkUnlocked(buddyUnitIndex, order)) {
            break;
        }

        unitIndex = std::min(unitIndex, buddyUnitIndex);
        chunkAddr = ChunkAddr(unitIndex);
        ++order;
    }

    AddFreeChunkUnlocked(chunkAddr, order);
    return MMS_OK;
}

BResult BuddyNumaMemoryPool::Free(uintptr_t blockAddr)
{
    uint64_t chunkAddr = blockAddr - sizeof(BlockHeader);
    auto header = reinterpret_cast<BlockHeader *>(chunkAddr);
    uint16_t order = header->order;
    if (UNLIKELY(order > mMaxOrder || order > BUDDY_MAX_ORDER)) {
        return MMS_ERR;
    }

    BuddyOrderLockGuard lock(mFreeLocks, order, mMaxOrder);
    return FreeUnlocked(blockAddr);
}

BResult BuddyNumaMemoryPool::FreeBatch(uint16_t order, const uintptr_t blockAddrs[], uint64_t count)
{
    if (UNLIKELY(order > mMaxOrder || order > BUDDY_MAX_ORDER || blockAddrs == nullptr || count == 0)) {
        return MMS_INVALID_PARAM;
    }
    for (uint64_t index = 0; index < count; ++index) {
        auto header = reinterpret_cast<BlockHeader *>(blockAddrs[index] - sizeof(BlockHeader));
        if (UNLIKELY(header->order != order || header->numaId != mNumaId)) {
            return MMS_INVALID_PARAM;
        }
    }

    BuddyOrderLockGuard lock(mFreeLocks, order, mMaxOrder);
    for (uint64_t index = 0; index < count; ++index) {
        BResult ret = FreeUnlocked(blockAddrs[index]);
        if (UNLIKELY(ret != MMS_OK)) {
            return ret;
        }
    }
    return MMS_OK;
}

BuddyNumaMemoryPool *BuddyNumaPoolManager::CreatNumaMemPool(uint16_t numaId, uint64_t address, uint64_t size,
                                                            uint64_t baseBlockSize)
{
    WriteLocker<ReadWriteLock> lock(&mLock);
    if (mNumaPools.find(numaId) != mNumaPools.end()) {
        MEM_LOG_ERROR("Buddy numa memory pool is exist, numa id:" << numaId << ".");
        return nullptr;
    }

    BuddyNumaMemoryPool *pool = new(std::nothrow) BuddyNumaMemoryPool();
    if (UNLIKELY(pool == nullptr)) {
        MEM_LOG_ERROR("Alloc memory failed.");
        return nullptr;
    }

    BResult ret = pool->Start(numaId, address, size, baseBlockSize);
    if (UNLIKELY(ret != MMS_OK)) {
        delete pool;
        MEM_LOG_ERROR("Start buddy numa pool failed, numa id:" << numaId << ", ret:" << ret << ".");
        return nullptr;
    }

    mNumaPools.insert({numaId, pool});
    return pool;
}

BResult BuddyNumaPoolManager::AllocFromPool(uint16_t numaId, uint16_t order, uintptr_t &blockAddr)
{
    ReadLocker<ReadWriteLock> lock(&mLock);
    auto it = mNumaPools.find(numaId);
    if (it == mNumaPools.end()) {
        return MMS_NOT_EXISTS;
    }
    return it->second->Alloc(order, blockAddr);
}

BResult BuddyNumaPoolManager::AllocFromOtherPool(uint16_t numaId, uint16_t order, uint16_t &allocNumaId,
                                                 uintptr_t &blockAddr)
{
    ReadLocker<ReadWriteLock> lock(&mLock);
    for (auto &item : mNumaPools) {
        if (item.first == numaId) {
            continue;
        }
        BResult ret = item.second->Alloc(order, blockAddr);
        if (ret == MMS_OK) {
            allocNumaId = item.first;
            return MMS_OK;
        }
    }
    return MMS_ALLOC_FAIL;
}

BResult BuddyNumaPoolManager::AllocBatchFromPool(uint16_t numaId, uint16_t order, uintptr_t blockAddrs[],
                                                 uint64_t requestCount, uint64_t &actualCount)
{
    ReadLocker<ReadWriteLock> lock(&mLock);
    auto it = mNumaPools.find(numaId);
    if (it == mNumaPools.end()) {
        actualCount = 0;
        return MMS_NOT_EXISTS;
    }
    return it->second->AllocBatch(order, blockAddrs, requestCount, actualCount);
}

BResult BuddyNumaPoolManager::AllocBatchFromOtherPool(uint16_t numaId, uint16_t order, uintptr_t blockAddrs[],
                                                      uint64_t requestCount, uint64_t &actualCount)
{
    ReadLocker<ReadWriteLock> lock(&mLock);
    actualCount = 0;
    for (auto &item : mNumaPools) {
        if (actualCount == requestCount) {
            break;
        }
        if (item.first == numaId) {
            continue;
        }
        uint64_t poolCount = 0;
        BResult ret = item.second->AllocBatch(order, blockAddrs + actualCount, requestCount - actualCount, poolCount);
        actualCount += poolCount;
        if (UNLIKELY(ret != MMS_OK && ret != MMS_ALLOC_FAIL)) {
            return ret;
        }
    }
    return actualCount > 0 ? MMS_OK : MMS_ALLOC_FAIL;
}

BResult BuddyNumaPoolManager::FreeToPool(uint16_t numaId, uintptr_t blockAddr)
{
    ReadLocker<ReadWriteLock> lock(&mLock);
    auto it = mNumaPools.find(numaId);
    if (it == mNumaPools.end()) {
        return MMS_NOT_EXISTS;
    }
    return it->second->Free(blockAddr);
}

BResult BuddyNumaPoolManager::FreeBatchToPool(uint16_t numaId, uint16_t order, const uintptr_t blockAddrs[],
                                              uint64_t count)
{
    ReadLocker<ReadWriteLock> lock(&mLock);
    auto it = mNumaPools.find(numaId);
    if (it == mNumaPools.end()) {
        return MMS_NOT_EXISTS;
    }
    return it->second->FreeBatch(order, blockAddrs, count);
}

NumaMemoryPool *NumaPoolManager::CreatNumaMemPool(uint16_t numaId)
{
    WriteLocker<ReadWriteLock> lock(&mLock);
    if (mNumaPools.find(numaId) != mNumaPools.end()) {
        MEM_LOG_ERROR("Numa memory pool is exist, numa id:" << numaId << ".");
        return nullptr; // 不支持重复、覆盖初始化
    }

    NumaMemoryPool *pool = new(std::nothrow) NumaMemoryPool();
    if (pool == nullptr) {
        MEM_LOG_ERROR("Alloc memory failed.");
        return nullptr;
    }

    mNumaPools.insert({numaId, pool});
    MEM_LOG_DEBUG("Add one numa pool, numa id:" << numaId << ".");
    return pool;
}

BResult NumaPoolManager::GetBatchBlocksFromPool(uint16_t numaId, uint64_t blockIndex, std::vector<BlockNode *> &blocks)
{
    mLock.LockRead();
    NumaMemoryPool* pool = GetCurrentNumaPool(numaId);
    if (pool == nullptr) {
        MEM_LOG_ERROR("Numa pool is not exist, numa id:" << numaId << ", cpu id:" << sched_getcpu() << ".");
        mLock.UnLock();
        return MMS_NOT_EXISTS;
    }

    pool->GetBatchBlocks(blockIndex, FIXED_CACHE_BATCH_COUNT, blocks);
    if (blocks.empty()) {
        MEM_LOG_DEBUG("Numa pool is empty, numa id:" << numaId << ".");
        mLock.UnLock();
        return MMS_NOT_EXISTS;
    }
    mLock.UnLock();
    return MMS_OK;
}

BResult NumaPoolManager::GetBatchBlocksFromOtherPool(uint16_t numaId, uint64_t blockIndex,
                                                     std::vector<BlockNode *> &blocks)
{
    ReadLocker<ReadWriteLock> lock(&mLock);
    NumaMemoryPool *pool = nullptr;
    for (auto &item: mNumaPools) {
        if (item.first == numaId) {
            continue;
        }
        pool = item.second;
        pool->GetBatchBlocks(blockIndex, FIXED_CACHE_BATCH_COUNT, blocks);
        if (!blocks.empty()) {
            return MMS_OK;
        }
        MEM_LOG_DEBUG("Numa pool is empty, numa id:" << item.first << ".");
        continue;
    }

    return MMS_ALLOC_FAIL;
}

BResult NumaPoolManager::AddBatchBlocksToPool(uint16_t numaId, uint64_t blockIndex, std::vector<BlockNode *> &blocks)
{
    mLock.LockRead();
    NumaMemoryPool *pool = GetCurrentNumaPool(numaId);
    if (pool == nullptr) {
        MEM_LOG_ERROR("Numa pool is not exist, numa id:" << numaId << ", cpu id:" << sched_getcpu() << ".");
        mLock.UnLock();
        return MMS_NOT_EXISTS;
    }

    pool->AddBatchBlocks(blockIndex, blocks);
    mLock.UnLock();
    return MMS_OK;
}

BResult NumaPoolManager::AddOneBlocksToPool(uint16_t numaId, uint64_t blockIndex, BlockNode *block)
{
    mLock.LockRead();
    NumaMemoryPool *pool = GetCurrentNumaPool(numaId);
    if (UNLIKELY(pool == nullptr)) {
        MEM_LOG_ERROR("Numa pool is not exist, numa id:" << numaId << ", cpu id:" << sched_getcpu() << ".");
        mLock.UnLock();
        return MMS_NOT_EXISTS;
    }

    pool->AddOneBlock(blockIndex, block);
    mLock.UnLock();
    return MMS_OK;
}

uint16_t ThreadCache::GetAllocNumaId()
{
    if (mNumaId == mHomeNumaId) {
        return mNumaId;
    }

    ++mNumaReprobeCount;
    if (mNumaReprobeCount < NUMA_POOL_REPROBE_INTERVAL) {
        return mNumaId;
    }

    mNumaReprobeCount = 0;
    return mHomeNumaId;
}

void ThreadCache::RecordAllocNumaId(uint16_t preferNumaId, uint16_t allocNumaId)
{
    if (mNumaId != mHomeNumaId && preferNumaId == mHomeNumaId) {
        if (allocNumaId == mHomeNumaId) {
            MEM_LOG_DEBUG("Restore preferred NUMA pool, old numa id:" << mNumaId << ", new numa id:" << mHomeNumaId
                                                                      << ".");
            mNumaId = mHomeNumaId;
            mNumaFallbackCount = 0;
            mNumaFallbackId = INVALID_NUMA_POOL_ID;
        }
        return;
    }

    if (allocNumaId == mNumaId) {
        mNumaFallbackCount = 0;
        mNumaFallbackId = INVALID_NUMA_POOL_ID;
        return;
    }

    if (mNumaFallbackId != allocNumaId) {
        mNumaFallbackId = allocNumaId;
        mNumaFallbackCount = 0;
    }
    ++mNumaFallbackCount;
    if (mNumaFallbackCount < NUMA_POOL_SWITCH_THRESHOLD) {
        return;
    }

    MEM_LOG_DEBUG("Switch preferred NUMA pool, old numa id:" << mNumaId << ", new numa id:" << allocNumaId << ".");
    mNumaId = allocNumaId;
    mNumaFallbackCount = 0;
    mNumaFallbackId = INVALID_NUMA_POOL_ID;
    mNumaReprobeCount = 0;
}

ThreadCache::~ThreadCache()
{
    BResult ret;

    try {
        if (mMemAllocator != nullptr) {
            mMemAllocator->RemoveThreadCacheMap(std::this_thread::get_id());
        } else {
            return;
        }
        if (mMemAllocator->IsBuddyMode()) {
            ret = FlushBuddyCaches();
            if (UNLIKELY(ret != MMS_OK)) {
                MEM_LOG_ERROR("Flush buddy thread cache failed, ret:" << ret << ".");
            }
            return;
        }

        for (uint16_t numaId = 0; numaId < MAX_NUMAS_NUM; ++numaId) {
            for (int index = 0; index < MAX_BLOCK_NUM; ++index) {
                if (mMemCaches[numaId][index].empty()) {
                    continue;
                }

                ret = mMemAllocator->ReturnBatchBlocksToPool(index, mMemCaches[numaId][index]);
                if (UNLIKELY(ret != MMS_OK)) {
                    MEM_LOG_ERROR("Add blocks to numa memory pool failed, ret:" << ret << ".");
                }
            }
        }
    } catch (const std::exception &ex) {
        MEM_LOG_ERROR("Exception caught in thread cache destructor: " << ex.what());
    }
}

BResult ThreadCache::PopBlock(std::vector<uintptr_t> &blocks, uintptr_t &blockAddr) const
{
    if (blocks.empty()) {
        return MMS_ALLOC_FAIL;
    }

    blockAddr = blocks.back();
    blocks.pop_back();
    return MMS_OK;
}

uint16_t ThreadCache::GetCacheNumaId(uint16_t numaId) const
{
    if (numaId < MAX_NUMAS_NUM) {
        return numaId;
    }
    return mNumaId;
}

BResult ThreadCache::GetOneBlockFromCache(uint64_t blockIndex, uintptr_t &blockAddr)
{
    if (PopBlock(mMemCaches[GetCacheNumaId(mNumaId)][blockIndex], blockAddr) == MMS_OK) {
        return MMS_OK;
    }

    for (uint16_t numaId = 0; numaId < MAX_NUMAS_NUM; ++numaId) {
        if (numaId == mNumaId) {
            continue;
        }
        if (PopBlock(mMemCaches[numaId][blockIndex], blockAddr) == MMS_OK) {
            return MMS_OK;
        }
    }
    return MMS_ALLOC_FAIL;
}

BResult ThreadCache::GetOneBlockFromCachePreferNuma(uint64_t blockIndex, uint16_t preferNumaId, uintptr_t &blockAddr)
{
    return PopBlock(mMemCaches[GetCacheNumaId(preferNumaId)][blockIndex], blockAddr);
}

BResult ThreadCache::AddOneBlockToCache(uint64_t blockIndex, uintptr_t blockAddr)
{
    auto header = reinterpret_cast<BlockHeader *>(blockAddr - sizeof(BlockHeader));
    auto &blocks = mMemCaches[GetCacheNumaId(header->numaId)][blockIndex];
    if (blocks.size() == CACHE_LIMIT_PER_THREAD) {
        MEM_LOG_DEBUG("Thread cache is full, thread id:" << std::this_thread::get_id() << ".");
        return MMS_INNER_RETRY;
    }

    blocks.emplace_back(blockAddr);
    return MMS_OK;
}

BResult ThreadCache::AddBatchBlocksToCache(uint64_t blockIndex, std::vector <uintptr_t> &blockAddrs)
{
    uint16_t numaCounts[MAX_NUMAS_NUM] = {0};
    for (uintptr_t blockAddr : blockAddrs) {
        auto header = reinterpret_cast<BlockHeader *>(blockAddr - sizeof(BlockHeader));
        ++numaCounts[GetCacheNumaId(header->numaId)];
    }

    for (uint16_t numaId = 0; numaId < MAX_NUMAS_NUM; ++numaId) {
        if (numaCounts[numaId] == 0) {
            continue;
        }
        if (mMemCaches[numaId][blockIndex].size() + numaCounts[numaId] >= CACHE_LIMIT_PER_THREAD) {
            MEM_LOG_ERROR("Thread cache is full, thread id:" << std::this_thread::get_id() << ".");
            return MMS_INNER_RETRY;
        }
    }

    for (uintptr_t blockAddr : blockAddrs) {
        auto header = reinterpret_cast<BlockHeader *>(blockAddr - sizeof(BlockHeader));
        mMemCaches[GetCacheNumaId(header->numaId)][blockIndex].emplace_back(blockAddr);
    }
    return MMS_OK;
}

BResult ThreadCache::GetOneBuddyBlockFromCache(uint16_t order, uintptr_t &blockAddr)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER) {
        return MMS_ALLOC_FAIL;
    }

    if (PopBlock(mBuddyCaches[GetCacheNumaId(mNumaId)][order], blockAddr) == MMS_OK) {
        return MMS_OK;
    }

    for (uint16_t numaId = 0; numaId < MAX_NUMAS_NUM; ++numaId) {
        if (numaId == mNumaId) {
            continue;
        }
        if (PopBlock(mBuddyCaches[numaId][order], blockAddr) == MMS_OK) {
            return MMS_OK;
        }
    }
    return MMS_ALLOC_FAIL;
}

BResult ThreadCache::GetOneBuddyBlockFromCachePreferNuma(uint16_t order, uint16_t preferNumaId, uintptr_t &blockAddr)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER) {
        return MMS_ALLOC_FAIL;
    }

    return PopBlock(mBuddyCaches[GetCacheNumaId(preferNumaId)][order], blockAddr);
}

BResult ThreadCache::AddOneBuddyBlockToCache(uint16_t order, uintptr_t blockAddr)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER) {
        return MMS_INVALID_PARAM;
    }
    auto header = reinterpret_cast<BlockHeader *>(blockAddr - sizeof(BlockHeader));
    auto &blocks = mBuddyCaches[GetCacheNumaId(header->numaId)][order];
    if (blocks.size() >= BUDDY_CACHE_LIMIT_PER_THREAD) {
        return MMS_INNER_RETRY;
    }

    blocks.emplace_back(blockAddr);
    return MMS_OK;
}

BResult ThreadCache::AddBatchBuddyBlocksToCache(uint16_t order, const uintptr_t blockAddrs[], uint64_t count)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER) {
        return MMS_INVALID_PARAM;
    }
    uint64_t numaCounts[MAX_NUMAS_NUM] = {0};
    for (uint64_t index = 0; index < count; ++index) {
        auto header = reinterpret_cast<BlockHeader *>(blockAddrs[index] - sizeof(BlockHeader));
        ++numaCounts[GetCacheNumaId(header->numaId)];
    }

    for (uint16_t numaId = 0; numaId < MAX_NUMAS_NUM; ++numaId) {
        if (numaCounts[numaId] == 0) {
            continue;
        }
        if (mBuddyCaches[numaId][order].size() + numaCounts[numaId] > BUDDY_CACHE_LIMIT_PER_THREAD) {
            return MMS_INNER_RETRY;
        }
    }

    for (uint64_t index = 0; index < count; ++index) {
        auto header = reinterpret_cast<BlockHeader *>(blockAddrs[index] - sizeof(BlockHeader));
        mBuddyCaches[GetCacheNumaId(header->numaId)][order].emplace_back(blockAddrs[index]);
    }
    return MMS_OK;
}

uint64_t ThreadCache::GetBatchBuddyBlocksFromCache(uint16_t order, uintptr_t blockAddrs[], uint64_t count)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER) {
        return 0;
    }

    if (!mBuddyCaches[GetCacheNumaId(mNumaId)][order].empty()) {
        return GetBatchBuddyBlocksFromCachePreferNuma(order, mNumaId, blockAddrs, count);
    }

    for (uint16_t numaId = 0; numaId < MAX_NUMAS_NUM; ++numaId) {
        if (numaId == mNumaId || mBuddyCaches[numaId][order].empty()) {
            continue;
        }
        return GetBatchBuddyBlocksFromCachePreferNuma(order, numaId, blockAddrs, count);
    }
    return 0;
}

uint64_t ThreadCache::GetBatchBuddyBlocksFromCachePreferNuma(uint16_t order, uint16_t preferNumaId,
                                                             uintptr_t blockAddrs[], uint64_t count)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER) {
        return 0;
    }

    auto &blocks = mBuddyCaches[GetCacheNumaId(preferNumaId)][order];
    if (blocks.empty()) {
        return 0;
    }

    uint64_t realCount = std::min<uint64_t>(count, blocks.size());
    auto begin = blocks.end() - static_cast<std::vector<uintptr_t>::difference_type>(realCount);
    std::copy(begin, blocks.end(), blockAddrs);
    blocks.erase(begin, blocks.end());
    return realCount;
}

BResult ThreadCache::FlushBuddyCaches()
{
    if (mMemAllocator == nullptr) {
        return MMS_OK;
    }

    uintptr_t blockAddrs[BUDDY_CACHE_LIMIT_PER_THREAD];
    for (uint16_t order = 0; order <= BUDDY_THREAD_CACHE_MAX_ORDER; ++order) {
        for (uint16_t numaId = 0; numaId < MAX_NUMAS_NUM; ++numaId) {
            while (!mBuddyCaches[numaId][order].empty()) {
                uint64_t count = GetBatchBuddyBlocksFromCachePreferNuma(order, numaId, blockAddrs,
                                                                        BUDDY_CACHE_LIMIT_PER_THREAD);
                BResult ret = mMemAllocator->ReturnBatchBuddyBlocksToPool(blockAddrs, count);
                if (UNLIKELY(ret != MMS_OK)) {
                    MEM_LOG_ERROR("Return buddy cache blocks failed, ret:" << ret << ".");
                    return ret;
                }
            }
        }
    }
    return MMS_OK;
}

std::vector<uintptr_t> ThreadCache::GetBatchBlocksFromCache(uint64_t blockIndex, uint64_t count)
{
    if (!mMemCaches[GetCacheNumaId(mNumaId)][blockIndex].empty()) {
        return GetBatchBlocksFromCachePreferNuma(blockIndex, mNumaId, count);
    }

    for (uint16_t numaId = 0; numaId < MAX_NUMAS_NUM; ++numaId) {
        if (numaId == mNumaId || mMemCaches[numaId][blockIndex].empty()) {
            continue;
        }
        return GetBatchBlocksFromCachePreferNuma(blockIndex, numaId, count);
    }
    return {};
}

std::vector<uintptr_t> ThreadCache::GetBatchBlocksFromCachePreferNuma(uint64_t blockIndex, uint16_t preferNumaId,
                                                                      uint64_t count)
{
    auto &blocks = mMemCaches[GetCacheNumaId(preferNumaId)][blockIndex];
    if (blocks.empty()) {
        return {};
    }

    if (count >= blocks.size()) {
        std::vector <uintptr_t> res(blocks.begin(), blocks.end());
        blocks.clear();
        return res;
    }

    std::vector <uintptr_t> res(blocks.end() - count, blocks.end());
    blocks.erase(blocks.end() - count, blocks.end());
    return res;
}

void ThreadCache::GetBatchBlocksFromCachePreferNuma(uint64_t blockIndex, uint16_t preferNumaId, uint64_t count,
                                                    std::vector<uintptr_t> &blockAddrs)
{
    blockAddrs.clear();
    auto &blocks = mMemCaches[GetCacheNumaId(preferNumaId)][blockIndex];
    if (blocks.empty()) {
        return;
    }

    blockAddrs.reserve(std::min<uint64_t>(count, blocks.size()));
    if (count >= blocks.size()) {
        blockAddrs.insert(blockAddrs.end(), blocks.begin(), blocks.end());
        blocks.clear();
        return;
    }

    auto begin = blocks.end() - static_cast<std::vector<uintptr_t>::difference_type>(count);
    blockAddrs.insert(blockAddrs.end(), begin, blocks.end());
    blocks.erase(begin, blocks.end());
}


void MmsMemAllocator::PutThreadCacheMap(std::thread::id key, ThreadCache *value)
{
    mLock.LockWrite();
    mThreadCacheMap.emplace(key, value);
    mLock.UnLock();
}

void MmsMemAllocator::RemoveThreadCacheMap(std::thread::id key)
{
    mLock.LockWrite();
    mThreadCacheMap.erase(key);
    mLock.UnLock();
}

uint16_t MmsMemAllocator::GetNumaId() const
{
    return threadCache[mArea].GetNumaId();
}

BResult MmsMemAllocator::Reset()
{
    BResult ret = MMS_OK;
    mLock.LockWrite();
    for (const auto &item: mThreadCacheMap) {
        ThreadCache *cache = item.second;
        if (cache == nullptr) {
            MEM_LOG_WARN("thread cache is nullptr!");
            continue;
        }
        cache->ClearCache();
    }
    if (mAllocMode == MemAllocOptions::ALLOC_MODE_BUDDY) {
        if (UNLIKELY(mBuddyNumaPool == nullptr)) {
            mLock.UnLock();
            return MMS_NOT_INITIALIZED;
        }
        ret = mBuddyNumaPool->Reset();
    } else {
        if (UNLIKELY(mNumaPool == nullptr)) {
            mLock.UnLock();
            return MMS_NOT_INITIALIZED;
        }
        ret = mNumaPool->Reset();
    }
    if (ret != MMS_OK) {
        MEM_LOG_ERROR("NumaPoolManager reset failed, ret:" << ret << ".");
    }
    mLock.UnLock();
    return ret;
}
}
}
