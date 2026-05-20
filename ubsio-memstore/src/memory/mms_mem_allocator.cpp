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

static thread_local ThreadCache threadCache[THREAD_CACHE_ALLOCATOR_NUM] = {
    ThreadCache(MmsMemAllocator::Instance(MMAP_AREA_IOCTX)),
    ThreadCache(MmsMemAllocator::Instance(MMAP_AREA_INDEX)),
    ThreadCache(MmsMemAllocator::Instance(MMAP_AREA_VALUE))
};

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

    const uint64_t unitSize = static_cast<uint64_t>(mBlockSize[NO_0]) + sizeof(BlockHeader);
    mBuddyMaxAllocSize = 0;
    for (uint16_t index = 0; index < mNumaNum; ++index) {
        uint64_t unitCount = mNumaSize[index] / unitSize;
        if (unitCount == 0) {
            continue;
        }

        uint64_t orderUnits = NO_1;
        while ((orderUnits <= (UINT64_MAX >> NO_1)) && ((orderUnits << NO_1) <= unitCount)) {
            orderUnits <<= NO_1;
        }
        mBuddyMaxAllocSize = std::max(mBuddyMaxAllocSize, unitSize * orderUnits - sizeof(BlockHeader));
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

BResult MmsMemAllocator::GetBuddyAllocOrder(uint64_t size, uint16_t &order) const
{
    if (UNLIKELY(size > mBuddyMaxAllocSize)) {
        MEM_LOG_ERROR("Invalid buddy alloc size:" << size << ", max size:" << mBuddyMaxAllocSize << ".");
        return MMS_ALLOC_FAIL;
    }

    const uint64_t totalSize = size + sizeof(BlockHeader);
    const uint64_t unitSize = static_cast<uint64_t>(mBlockSize[NO_0]) + sizeof(BlockHeader);
    uint64_t unitCount = (totalSize - NO_1) / unitSize + NO_1;
    order = 0;
    uint64_t roundedUnits = NO_1;
    while (roundedUnits < unitCount) {
        if (UNLIKELY(order == BUDDY_MAX_ORDER)) {
            MEM_LOG_ERROR("Invalid buddy alloc size:" << size << ".");
            return MMS_ALLOC_FAIL;
        }
        roundedUnits <<= NO_1;
        ++order;
    }
    return MMS_OK;
}

BResult MmsMemAllocator::ReturnBatchBuddyBlocksToPool(const uintptr_t blockAddrs[], uint64_t count)
{
    BResult freeRet = MMS_OK;
    for (uint64_t index = 0; index < count; ++index) {
        BResult ret = ReturnBuddyBlockToPool(blockAddrs[index]);
        if (UNLIKELY(ret != MMS_OK)) {
            freeRet = ret;
        }
    }
    return freeRet;
}

BResult MmsMemAllocator::BuddyAllocFromThreadCacheMiss(uint16_t order, uint16_t &numaId, uintptr_t &blockAddr)
{
    uintptr_t blocks[BUDDY_CACHE_REFILL_COUNT];
    uint64_t count = 0;
    uint16_t allocNumaId = numaId;
    BResult ret = MMS_ALLOC_FAIL;
    for (; count < BUDDY_CACHE_REFILL_COUNT; ++count) {
        ret = BuddyAllocFromPool(numaId, order, allocNumaId, blocks[count]);
        if (ret != MMS_OK) {
            break;
        }
    }

    if (count > 0) {
        blockAddr = blocks[--count];
        auto header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
        numaId = header->numaId;
        ret = threadCache[mArea].AddBatchBuddyBlocksToCache(order, blocks, count);
        if (UNLIKELY(ret != MMS_OK)) {
            return ReturnBatchBuddyBlocksToPool(blocks, count);
        }
        return MMS_OK;
    }

    ret = threadCache[mArea].FlushBuddyCaches();
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }
    return BuddyAllocFromPool(numaId, order, numaId, blockAddr);
}

BResult MmsMemAllocator::BuddyAllocDirect(uint16_t order, uint16_t &numaId, uintptr_t &blockAddr)
{
    BResult ret = BuddyAllocFromPool(numaId, order, numaId, blockAddr);
    if (UNLIKELY(ret != MMS_OK)) {
        ret = threadCache[mArea].FlushBuddyCaches();
        if (UNLIKELY(ret != MMS_OK)) {
            return ret;
        }
        ret = BuddyAllocFromPool(numaId, order, numaId, blockAddr);
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
        MEM_LOG_DEBUG("Thread cache hit, block size:" << mBlockSize[blockIndex] << ".");
        return MMS_OK;
    }

    std::vector <BlockNode *> blocks;
    blocks.reserve(CACHE_LIMIT_PER_THREAD / NO_2);
    ret = mNumaPool->GetBatchBlocksFromPool(numaId, blockIndex, blocks);
    if (UNLIKELY(ret != MMS_OK)) {
        ret= mNumaPool->GetBatchBlocksFromOtherPool(numaId, blockIndex, blocks);
        if (UNLIKELY(ret != MMS_OK)) {
            MEM_LOG_ERROR("All pools is empty, alloc block failed, size:" << mBlockSize[blockIndex] << ".");
            return MMS_ALLOC_FAIL;
        }
    }

    blockAddr = reinterpret_cast<uintptr_t>(blocks.back());
    blocks.pop_back();

    std::vector <uintptr_t> tmpVec;
    tmpVec.reserve(blocks.size());
    std::transform(blocks.begin(), blocks.end(), std::back_inserter(tmpVec),
                   [](BlockNode *ptr) -> uintptr_t {
                       return reinterpret_cast<uintptr_t>(ptr);
                   });

    ret = threadCache[mArea].AddBatchBlocksToCache(blockIndex, tmpVec);
    if (UNLIKELY(ret != MMS_OK)) {
        MEM_LOG_ERROR("Error thread cache.");
        return MMS_INNER_ERR;
    }

    MEM_LOG_DEBUG("Mem pool hit, block size:" << mBlockSize[blockIndex] << ".");
    return MMS_OK;
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
    uint64_t count = threadCache[mArea].GetBatchBuddyBlocksFromCache(order, blocks, BUDDY_CACHE_DRAIN_COUNT);
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

    BResult ret = MMS_ERR;
    BlockNode *block = reinterpret_cast<BlockNode*>(blockAddr);
    if (threadCache[mArea].GetNumaId() != header->numaId) { // 从其它numa借的
        ret = mNumaPool->AddOneBlocksToPool(header->numaId, blockIndex, block);
        if (UNLIKELY(ret != MMS_OK)) {
            MEM_LOG_ERROR("Return block to numa pool failed, numaId:" << header->numaId << ", ret:" << ret << ".");
            return ret;
        }
        MEM_LOG_DEBUG("Return one block to other numa pool, block size:" << blockSize << ", numa:" << header->numaId
                                                                         << ".");
        return MMS_OK;
    }

    if (LIKELY(threadCache[mArea].AddOneBlockToCache(blockIndex, blockAddr) == MMS_OK)) {
        MEM_LOG_DEBUG("Return one block to thread cache, block size:" << blockSize << ".");
        return MMS_OK;
    }

    auto blocks = threadCache[mArea].GetBatchBlocksFromCache(blockIndex, CACHE_LIMIT_PER_THREAD / NO_2);

    std::vector<BlockNode*> cache;
    cache.reserve(blocks.size());
    std::transform(blocks.begin(), blocks.end(), std::back_inserter(cache), [](uintptr_t addr) -> BlockNode* {
        return reinterpret_cast<BlockNode*>(addr);
    });

    uint16_t numaId = threadCache[mArea].GetNumaId();
    ret = mNumaPool->AddBatchBlocksToPool(numaId, blockIndex, cache);
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
    uint64_t unitIndex = UnitIndex(chunkAddr);
    auto header = reinterpret_cast<BlockHeader *>(chunkAddr);
    header->numaId = mNumaId;
    header->order = order;
    header->blockSize = PayloadSize(order);
    auto block = reinterpret_cast<BuddyBlockNode *>(chunkAddr + sizeof(BlockHeader));

    std::lock_guard<std::mutex> lock(mFreeLocks[order]);
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

BResult BuddyNumaMemoryPool::Alloc(uint16_t order, uintptr_t &blockAddr)
{
    if (UNLIKELY(order > mMaxOrder || order > BUDDY_MAX_ORDER)) {
        return MMS_ALLOC_FAIL;
    }

    uint16_t curOrder = order;
    BuddyBlockNode *block = nullptr;
    for (; curOrder <= mMaxOrder; ++curOrder) {
        block = PopFreeChunk(curOrder);
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
        AddFreeChunk(buddyAddr, curOrder);
    }

    auto header = reinterpret_cast<BlockHeader *>(chunkAddr);
    header->numaId = mNumaId;
    header->order = order;
    header->blockSize = PayloadSize(order);
    blockAddr = chunkAddr + sizeof(BlockHeader);
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

    uint64_t unitIndex = UnitIndex(chunkAddr);
    while (order < mMaxOrder) {
        uint64_t orderUnits = OrderUnits(order);
        uint64_t buddyUnitIndex = unitIndex ^ orderUnits;
        if (buddyUnitIndex + orderUnits > mUnitCount) {
            break;
        }

        if (!RemoveFreeChunk(buddyUnitIndex, order)) {
            break;
        }

        unitIndex = std::min(unitIndex, buddyUnitIndex);
        chunkAddr = ChunkAddr(unitIndex);
        ++order;
    }

    AddFreeChunk(chunkAddr, order);
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

BResult BuddyNumaPoolManager::FreeToPool(uint16_t numaId, uintptr_t blockAddr)
{
    ReadLocker<ReadWriteLock> lock(&mLock);
    auto it = mNumaPools.find(numaId);
    if (it == mNumaPools.end()) {
        return MMS_NOT_EXISTS;
    }
    return it->second->Free(blockAddr);
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

    blocks = pool->GetBatchBlocks(blockIndex, CACHE_LIMIT_PER_THREAD / NO_2);
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
        blocks = pool->GetBatchBlocks(blockIndex, CACHE_LIMIT_PER_THREAD / NO_2);
        if (!blocks.empty()) {
            return MMS_OK;
        }
        MEM_LOG_WARN("Numa pool is empty, numa id:" << item.first << ".");
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

        for (int index = 0; index < MAX_BLOCK_NUM; ++index) {
            if (mMemCaches[index].empty()) {
                continue;
            }

            std::vector< BlockNode *> cache;
            cache.reserve(mMemCaches[index].size());
            std::transform(mMemCaches[index].begin(), mMemCaches[index].end(), std::back_inserter(cache),
                [](uint64_t addr) -> BlockNode* {
                    return reinterpret_cast<BlockNode*>(addr);
                });

            ret = mMemAllocator->GetNumaPool()->AddBatchBlocksToPool(mNumaId, index, cache);
            if (UNLIKELY(ret != MMS_OK)) {
                MEM_LOG_ERROR("Add blocks to numa memory pool failed, ret:" << ret << ".");
            }
        }
    } catch (const std::exception &ex) {
        MEM_LOG_ERROR("Exception caught in thread cache destructor: " << ex.what());
    }
}

BResult ThreadCache::GetOneBlockFromCache(uint64_t blockIndex, uintptr_t &blockAddr)
{
    if (mMemCaches[blockIndex].empty()) {
        return MMS_ALLOC_FAIL;
    }

    blockAddr = mMemCaches[blockIndex].back();
    mMemCaches[blockIndex].pop_back();
    return MMS_OK;
}

BResult ThreadCache::AddOneBlockToCache(uint64_t blockIndex, uintptr_t blockAddr)
{
    if (mMemCaches[blockIndex].size() == CACHE_LIMIT_PER_THREAD) {
        MEM_LOG_DEBUG("Thread cache is full, thread id:" << std::this_thread::get_id() << ".");
        return MMS_INNER_RETRY;
    }

    mMemCaches[blockIndex].emplace_back(blockAddr);
    return MMS_OK;
}

BResult ThreadCache::AddBatchBlocksToCache(uint64_t blockIndex, std::vector <uintptr_t> &blockAddrs)
{
    if ((mMemCaches[blockIndex].size() + blockAddrs.size()) >= CACHE_LIMIT_PER_THREAD) {
        MEM_LOG_ERROR("Thread cache is full, thread id:" << std::this_thread::get_id() << ".");
        return MMS_INNER_RETRY;
    }

    mMemCaches[blockIndex].insert(mMemCaches[blockIndex].end(), blockAddrs.begin(), blockAddrs.end());
    return MMS_OK;
}

BResult ThreadCache::GetOneBuddyBlockFromCache(uint16_t order, uintptr_t &blockAddr)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER || mBuddyCaches[order].empty()) {
        return MMS_ALLOC_FAIL;
    }

    blockAddr = mBuddyCaches[order].back();
    mBuddyCaches[order].pop_back();
    return MMS_OK;
}

BResult ThreadCache::AddOneBuddyBlockToCache(uint16_t order, uintptr_t blockAddr)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER) {
        return MMS_INVALID_PARAM;
    }
    if (mBuddyCaches[order].size() >= BUDDY_CACHE_LIMIT_PER_THREAD) {
        return MMS_INNER_RETRY;
    }

    mBuddyCaches[order].emplace_back(blockAddr);
    return MMS_OK;
}

BResult ThreadCache::AddBatchBuddyBlocksToCache(uint16_t order, const uintptr_t blockAddrs[], uint64_t count)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER) {
        return MMS_INVALID_PARAM;
    }
    if (mBuddyCaches[order].size() + count > BUDDY_CACHE_LIMIT_PER_THREAD) {
        return MMS_INNER_RETRY;
    }

    mBuddyCaches[order].insert(mBuddyCaches[order].end(), blockAddrs, blockAddrs + count);
    return MMS_OK;
}

uint64_t ThreadCache::GetBatchBuddyBlocksFromCache(uint16_t order, uintptr_t blockAddrs[], uint64_t count)
{
    if (order > BUDDY_THREAD_CACHE_MAX_ORDER || mBuddyCaches[order].empty()) {
        return 0;
    }

    uint64_t realCount = std::min<uint64_t>(count, mBuddyCaches[order].size());
    auto begin = mBuddyCaches[order].end() - static_cast<std::vector<uintptr_t>::difference_type>(realCount);
    std::copy(begin, mBuddyCaches[order].end(), blockAddrs);
    mBuddyCaches[order].erase(begin, mBuddyCaches[order].end());
    return realCount;
}

BResult ThreadCache::FlushBuddyCaches()
{
    if (mMemAllocator == nullptr) {
        return MMS_OK;
    }

    for (uint16_t order = 0; order <= BUDDY_THREAD_CACHE_MAX_ORDER; ++order) {
        auto &cache = mBuddyCaches[order];
        while (!cache.empty()) {
            uintptr_t blockAddr = cache.back();
            cache.pop_back();
            BResult ret = mMemAllocator->ReturnBuddyBlockToPool(blockAddr);
            if (UNLIKELY(ret != MMS_OK)) {
                MEM_LOG_ERROR("Return buddy cache block failed, ret:" << ret << ".");
                return ret;
            }
        }
    }
    return MMS_OK;
}

std::vector<uintptr_t> ThreadCache::GetBatchBlocksFromCache(uint64_t blockIndex, uint64_t count)
{
    if (mMemCaches[blockIndex].empty()) {
        return {};
    }

    if (count >= mMemCaches[blockIndex].size()) {
        std::vector <uintptr_t> res(mMemCaches[blockIndex].begin(), mMemCaches[blockIndex].end());
        mMemCaches[blockIndex].clear();
        return res;
    }

    std::vector <uintptr_t> res(mMemCaches[blockIndex].end() - count, mMemCaches[blockIndex].end());
    mMemCaches[blockIndex].erase(mMemCaches[blockIndex].end() - count, mMemCaches[blockIndex].end());
    return std::move(res);
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
