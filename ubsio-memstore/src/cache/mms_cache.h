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

#ifndef MMS_CACHE_H
#define MMS_CACHE_H

#include <functional>
#include <algorithm>
#include <string>
#include <vector>

#include "mms_err.h"
#include "mms_ref.h"
#include "mms_types.h"
#include "mms_cache_log.h"
#include "mms_cache_lock.h"
#include "mms_lock.h"
#include "mms_mem_allocator.h"
#include "mms_mem_mgr.h"
#include "art_index/art_range.h"
#include "art_index/lsm_art_tree.h"

namespace ock {
namespace mms {
constexpr uint16_t FLAG_INVALID = 0;
constexpr uint16_t FLAG_VALID = 1;

constexpr uint32_t ROOT_POS_OFFSET = 0;
constexpr uint32_t INDEX_COUNT_OFFSET = 4;
constexpr uint32_t BUCKET_COUNT_OFFSET = 8;
constexpr uint32_t BUCKET_NODE_BASE_OFFSET = 12;

struct IndexNode {
    uint32_t hashCode;
    uint16_t valid;
    uint16_t numaId;
    uint64_t numaOffset;
    uint64_t indexValueAddr; // 下一个节点的index value地址
};

struct BucketNode {
    struct RwLockStatus status;
    IndexNode head;
};

struct IndexValue {
    IndexNode next;
    char key[MAX_KEY_SIZE];
    MmsPtId ptId;
    uint16_t isDelete; // 墓碑标记
    uint32_t version;
    uint64_t totalDataLen;
    uint64_t blockOffset;
    BucketNode *bucketNode;
};

struct DataHeader {
    uint64_t blockSize; // block里的数据部分长度，不包含DataHeader的长度
    char data[0];
};

struct ReplacePara {
    const char *key;
    const char *value;
    uint64_t offset;
    uint64_t length;
    uint32_t version;
    MmsPtId ptId;
};

constexpr uint32_t INDEX_NODE_SIZE = sizeof(IndexNode);
constexpr uint32_t INDEX_VALUE_SIZE = sizeof(IndexValue);
constexpr uint32_t BUCKET_NODE_SIZE = sizeof(BucketNode);
constexpr uint32_t DATA_HEADER_SIZE = sizeof(DataHeader);

class Cache;
using CachePtr = Ref<Cache>;
class Cache {
public:
    inline static CachePtr &Instance()
    {
        static auto instance = MakeRef<Cache>();
        return instance;
    }

    BResult Init(uint64_t bucketMemAddr, uint64_t bucketMemSize, CacheLogFunc func, bool server,
                 std::pair<uint64_t, uint64_t> blockSize);

    void Exit();

    BResult Reset()
    {
        return MMS_OK;
    }

    void ResetLogLevel(int32_t level)
    {
        CacheLog::Instance()->SetMinLogLevel(level);
    }

    inline uint32_t GetBucketCount() const
    {
        return *reinterpret_cast<uint32_t *>(mBaseAddr + BUCKET_COUNT_OFFSET);
    }

    inline uint64_t GetBucketAddr(uint32_t bucketIndex)
    {
        return mBaseAddr + BUCKET_NODE_BASE_OFFSET + static_cast<uint64_t>(bucketIndex) * BUCKET_NODE_SIZE;
    }

    inline void SetRecoverStatus(bool isRecovering)
    {
        mIsRecovering.store(isRecovering, std::memory_order_release);
    }

    BResult HandlePutExistingNode(IndexNode *existingNode, const char *key, const char *value, uint64_t length);
    BResult HandleReplacePut(IndexNode &curNode, const std::string &key, const char *value, uint64_t length);
    BResult InsertTombEntry(BucketNode *bucketNode, uint32_t hashCode, uint32_t version, const char *key);

    BResult Put(const char *key, const char *value, uint64_t length, uint32_t version, MmsPtId ptId);
    BResult Get(const char *key, uint64_t offset, uint64_t length, char *value, uint64_t *realLength);
    BResult Update(const char *key, const char *value, uint64_t offset, uint64_t length, uint32_t version);
    BResult Delete(const char *key, uint32_t version);
    BResult Replace(const ReplacePara &para);

    // 返回实际读取到的字节数
    uint64_t GetDataFromBlock(IndexValue *indexValue, char *data, uint64_t offset, uint64_t dataLen);
    void ClearDeletedData();

    BResult GetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum);
    BResult GetValuesByRange(const char *keyStart, const char *keyEnd, ValueInfo **valueInfoItems, uint64_t *itemNum);
    BResult GetKeysByRange(const char *keyStart, const char *keyEnd, std::vector<std::string> &matchedKeys);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    inline void PutBucketCount(uint32_t value)
    {
        *reinterpret_cast<uint32_t *>(mBaseAddr + BUCKET_COUNT_OFFSET) = value;
    }

    BResult AllocDataBlock(uint64_t remainLen, uint16_t &numaId, uint64_t &curBlockAddr, uint64_t &curBuffSize);

    BResult PutDataIntoBlock(IndexValue *indexValue, const char *data, uint64_t dataLen);

    BResult ReviveDataBlock(IndexValue *indexValue, const char *data, uint64_t offset, uint64_t dataLen);

    BResult UpdateDataInCurrentBlock(IndexValue *indexValue, DataHeader *header, const char *data, uint64_t offset,
                                     uint64_t dataLen);

    BResult ExpandAndUpdateDataBlock(IndexValue *indexValue, const char *data, uint64_t offset, uint64_t dataLen,
                                     uint64_t curBlockAddr);

    BResult UpdateDataBlock(IndexValue *indexValue, const char *data, uint64_t offset, uint64_t dataLen);

private:
    MmsMemMgrPtr mMemMgr = nullptr;
    MmsMemAllocatorPtr mIndexMemAllocator = nullptr;
    MmsMemAllocatorPtr mValueAllocator = nullptr;

    uint64_t mBaseAddr = 0;
    std::atomic<bool> mIsRecovering{false};

    LsmArtTree mLsmArtTree;
    ReadWriteLock mArtValueLock;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}

#endif // MMS_CACHE_H
