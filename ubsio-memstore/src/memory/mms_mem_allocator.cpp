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

#include <functional>
#include <algorithm>
#include <regex>
#include <thread>
#include "mms_mem_allocator.h"

namespace ock {
    namespace mms {

    static constexpr uint8_t THREAD_CACHE_ALLOCATOR_NUM = 3;

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

    BResult MmsMemAllocator::Initialize(MemAllocOptions &options)
    {
        uint16_t index;

        for (index = 0; index < options.numaNum; index++) {
            mNumaId[index] = options.numaId[index];
            mNumaSize[index] = options.numaSize[index];
            mNumaAddress[index] = options.numaAddress[index];
        }
        mNumaNum = options.numaNum;

        for (index = 0; index < options.blockNum; index++) {
            mBlockRate[index] = options.blockRate[index];
            mBlockSize[index] = options.blockSize[index];
        }
        mBlockNum = options.blockNum;

        mNumaPool = std::make_shared<NumaPoolManager>();

        for (index = 0; index < mNumaNum; index++) {
            auto ret = InitMemNumaPool(index);
            if (UNLIKELY(ret != MMS_OK)) {
                MEM_LOG_ERROR("Init numa pool failed, numa id:" << mNumaId[index] << ".");
                return ret;
            }
        }
        return MMS_OK;
    }

    void MmsMemAllocator::Exit(void)
    {
        return;
    }

    BResult MmsMemAllocator::MmsAlloc(uint64_t size, uint16_t &numaId, uintptr_t &blockAddr)
    {
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

    BResult MmsMemAllocator::MmsFree(uintptr_t blockAddr)
    {
        BlockHeader *header = reinterpret_cast<BlockHeader*>(blockAddr - sizeof(BlockHeader));
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
        ret = mNumaPool->Reset();
        if (ret != MMS_OK) {
            MEM_LOG_ERROR("NumaPoolManager reset failed, ret:" << ret << ".");
        }
        mLock.UnLock();
        return ret;
    }
    }
}