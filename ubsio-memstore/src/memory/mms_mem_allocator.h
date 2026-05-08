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

#ifndef MMS_MEM_ALLOCTOR_H
#define MMS_MEM_ALLOCTOR_H

#include <atomic>
#include <sched.h>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include "mms_ref.h"
#include "mms_lock.h"
#include "mms_err.h"
#include "mms_comm.h"
#include "mms.h"
#include "mms_mem_common.h"
#include "mms_mem_log.h"

namespace ock {
    namespace mms {

    constexpr uint64_t CACHE_LIMIT_PER_THREAD = 128; // 每个线程能缓存的block数

    class ThreadCache;
    class NumaPoolManager;
    using NumaPoolManagerPtr = std::shared_ptr<NumaPoolManager>;

    class MmsMemAllocator;
    using MmsMemAllocatorPtr = Ref<MmsMemAllocator>;
    class MmsMemAllocator {
    public:
        explicit MmsMemAllocator(MmapArea area) : mArea(area) {}
        ~MmsMemAllocator() = default;

        static MmsMemAllocatorPtr &Instance(MmapArea area)
        {
            if (area == MMAP_AREA_IOCTX) {
                static auto ioCtxInstance = MakeRef<MmsMemAllocator>(area);
                return ioCtxInstance;
            } else if (area == MMAP_AREA_INDEX) {
                static auto indexInstance = MakeRef<MmsMemAllocator>(area);
                return indexInstance;
            } else if (area == MMAP_AREA_VALUE) {
                static auto valueInstance = MakeRef<MmsMemAllocator>(area);
                return valueInstance;
            } else {
                static auto other_instance = MakeRef<MmsMemAllocator>(area);
                return other_instance;
            }
        }

        BResult Initialize(MemAllocOptions &options);
        void Exit(void);

        BResult Reset(void);

        BResult MmsAlloc(uint64_t size, uint16_t &numaId, uintptr_t &blockAddr);

        BResult MmsFree(uintptr_t blockAddr);

        void PutThreadCacheMap(std::thread::id key, ThreadCache* value);

        void RemoveThreadCacheMap(std::thread::id key);

        void UpdateNumaId(uint16_t &numaId)
        {
            for (uint16_t i = 0; i < mNumaNum; i++) {
                if (mNumaId[i] == numaId) {
                    return;
                }
            }
            uint16_t index = __sync_fetch_and_add(&mNumaIndex, 1) % mNumaNum;
            numaId = mNumaId[index];
            return;
        }

        NumaPoolManagerPtr &GetNumaPool()
        {
            return mNumaPool;
        }

        uint16_t GetNumaId() const;

        DEFINE_REF_COUNT_FUNCTIONS;

    private:
        BResult InitMemNumaPool(uint16_t numaIndex);

    private:
        uint16_t mNumaNum = 0;
        uint16_t mNumaId[MAX_NUMAS_NUM] = {0};
        uint64_t mNumaSize[MAX_NUMAS_NUM] = {0};
        uint64_t mNumaAddress[MAX_NUMAS_NUM] = {0};

        NumaPoolManagerPtr mNumaPool = nullptr;

        uint16_t mNumaIndex = 0;

        uint32_t mBlockNum = 0;
        uint32_t mBlockRate[MAX_BLOCK_NUM] = {0};
        uint32_t mBlockSize[MAX_BLOCK_NUM] = {0};

        MmapArea mArea;
        ReadWriteLock mLock;
        std::unordered_map<std::thread::id, ThreadCache *> mThreadCacheMap;

        DEFINE_REF_COUNT_VARIABLE;
    };

    // 用于连接Node内存块
    struct BlockNode {
        BlockNode* next;
    };

    // 块的头部，用于存储块的信息
    struct BlockHeader {
        uint16_t numaId;
        uint32_t blockSize;
    };

    class MemBlockList {
    public:
        MemBlockList() = default;
        inline void PushOneBlock(BlockNode *block)
        {
            std::lock_guard<std::mutex> lock(mLock);
            block->next = mHead;
            mHead = block;
        }

        inline BlockNode* PopOneBlock()
        {
            std::lock_guard<std::mutex> lock(mLock);
            if (mHead == nullptr) {
                return nullptr;
            }

            BlockNode *block = mHead;
            mHead = mHead->next;
            return  block;
        }

        inline std::vector<BlockNode*> PopBatchBlocks(uint64_t count)
        {
            std::vector<BlockNode*> blocks{};
            blocks.reserve(count);
            BlockNode* block = nullptr;
            for (uint64_t i = 0; i < count; ++i) {
                block = PopOneBlock();
                if (block == nullptr) {
                    break;
                }
                blocks.emplace_back(block);
            }
            return std::move(blocks);
        }

        inline void PushBatchBlocks(const std::vector<BlockNode*>& blocks)
        {
            for (auto &block : blocks)
                PushOneBlock(block);
        }

    private:
        BlockNode *mHead = nullptr;
        std::mutex mLock;
    };

    class NumaMemoryPool {
    public:
        inline void AddOneBlock(uint16_t blockIndex, BlockNode* block)
        {
            memLists[blockIndex].PushOneBlock(block);
        }

        inline BlockNode* GetOneBlock(uint16_t blockIndex, uint64_t blockSize)
        {
            return memLists[blockIndex].PopOneBlock();
        }

        inline void AddBatchBlocks(uint16_t blockIndex, const std::vector<BlockNode*>& blocks)
        {
            memLists[blockIndex].PushBatchBlocks(blocks);
        }

        inline std::vector<BlockNode *> GetBatchBlocks(uint16_t blockIndex, uint64_t count)
        {
            return std::move(memLists[blockIndex].PopBatchBlocks(count));
        }

    private:
        MemBlockList memLists[MAX_BLOCK_NUM];
    };

    class NumaPoolManager {
    public:
        inline NumaMemoryPool* GetCurrentNumaPool(uint16_t numaId)
        {
            auto it = mNumaPools.find(numaId);
            if (it == mNumaPools.end()) {
                return nullptr;
            }
            return it->second;
        }

        NumaMemoryPool* CreatNumaMemPool(uint16_t numaId);

        BResult GetBatchBlocksFromPool(uint16_t numaId, uint64_t blockIndex, std::vector<BlockNode *> &blocks);
        BResult GetBatchBlocksFromOtherPool(uint16_t numaId, uint64_t blockIndex, std::vector<BlockNode *> &blocks);

        BResult AddBatchBlocksToPool(uint16_t numaId, uint64_t blockIndex, std::vector<BlockNode*>& blocks);
        BResult AddOneBlocksToPool(uint16_t numaId, uint64_t blockIndex, BlockNode *block);

        inline BResult Reset()
        {
            mLock.LockWrite();
            for (const auto &item: mNumaPools) {
                delete item.second;
            }
            mNumaPools.clear();
            mLock.UnLock();
            return MMS_OK;
        }

    private:
        std::unordered_map<uint16_t, NumaMemoryPool*> mNumaPools;
        ReadWriteLock mLock;
    };

    class ThreadCache {
    public:
        ThreadCache() = delete;

        explicit ThreadCache(MmsMemAllocatorPtr allocator)
        {
            for (auto &memCache : mMemCaches) {
                memCache.reserve(CACHE_LIMIT_PER_THREAD);
            }
            mNumaId = GetCurCPUNumaNode();
            mMemAllocator = allocator;
            allocator->UpdateNumaId(mNumaId);
            allocator->PutThreadCacheMap(std::this_thread::get_id(), this);
        }

        inline BResult ClearCache()
        {
            for (auto &mMemCache: mMemCaches) {
                uint64_t size = mMemCache.size();
                mMemCache.clear();
                size = mMemCache.size();
            }
            return MMS_OK;
        }

        ~ThreadCache();

        inline uint16_t GetNumaId()
        {
            return mNumaId;
        }

        BResult GetOneBlockFromCache(uint64_t blockIndex, uintptr_t &blockAddr);

        BResult AddOneBlockToCache(uint64_t blockIndex, uintptr_t blockAddr);

        BResult AddBatchBlocksToCache(uint64_t blockIndex, std::vector<uintptr_t> &blockAddrs);

        std::vector<uintptr_t> GetBatchBlocksFromCache(uint64_t blockIndex, uint64_t count);

    private:
        std::vector<uintptr_t> mMemCaches[MAX_BLOCK_NUM];
        uint16_t mNumaId;

        MmsMemAllocatorPtr mMemAllocator = nullptr;
    };
    }
}
#endif