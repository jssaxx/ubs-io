/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef MMS_CACHE_H
#define MMS_CACHE_H

#include <functional>
#include <algorithm>

#include "mms_err.h"
#include "mms_ref.h"
#include "mms_types.h"
#include "mms_cache_log.h"
#include "mms_cache_lock.h"
#include "mms_mem_allocator.h"
#include "mms_mem_mgr.h"

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

struct IndexValue {
    IndexNode next;
    char key[MAX_KEY_SIZE];
    MmsPtId ptId;
    uint16_t isDelete; // 墓碑标记
    uint32_t version;
    uint64_t totalDataLen;
    uint64_t firstBlockOffset;
};

struct DataHeader {
    uint64_t nextBlockOffset;
    uint64_t blockSize; // block里的数据部分长度，不包含DataHeader的长度
    char data[0];
};

struct BucketNode {
    struct RwLockStatus status;
    IndexNode head;
};

struct UpdateInfo {
    const char *data;
    uint64_t offset;
    uint64_t length;
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
    BResult HandleReplacePut(IndexNode &curNode, const char *key, const char *value, uint64_t length);
    BResult InsertTombEntry(BucketNode *bucketNode, uint32_t hashCode, uint32_t version, const char *key);

    BResult Put(const char *key, const char *value, uint64_t length, uint32_t version, MmsPtId ptId);
    BResult Get(const char *key, uint64_t offset, uint64_t length, char *value, uint64_t *realLength);
    BResult Update(const char *key, const char *value, uint64_t offset, uint64_t length, uint32_t version);
    BResult Delete(const char *key, uint32_t version);
    BResult Replace(const ReplacePara &para);

    // 返回实际读取到的字节数
    uint64_t GetDataFromBlockList(IndexValue *indexValue, char *data, uint64_t offset, uint64_t dataLen);
    void ClearDeletedData();

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    inline void PutBucketCount(uint32_t value)
    {
        *reinterpret_cast<uint32_t *>(mBaseAddr + BUCKET_COUNT_OFFSET) = value;
    }

    BResult AllocDataBlock(uint64_t remainLen, uint16_t &numaId, uint64_t &curBlockAddr, uint64_t &curBuffSize);

    BResult PutDataIntoBlockList(IndexValue *indexValue, const char *data, uint64_t dataLen);

    BResult HandleUpdateData(IndexValue *indexValue, uint64_t blockOffset, uint64_t currentOffsetSeek,
                             DataHeader *preHeader, UpdateInfo &updateInfo);

    BResult UpdateDataInBlockList(IndexValue *indexValue, const char *data, uint64_t offset, uint64_t dataLen);

    // 根据value大小匹配最佳的数据块大小，wasteThreshold:允许浪费的空间比例来满足最大匹配
    uint64_t GetBestBlockSize(uint64_t valueLen, double wasteThreshold);

private:
    MmsMemMgrPtr mMemMgr = nullptr;
    MmsMemAllocatorPtr mIndexMemAllocator = nullptr;
    MmsMemAllocatorPtr mValueAllocator = nullptr;

    uint64_t mBaseAddr = 0;
    uint64_t mBaseSize = 0;
    uint64_t mMinBlockSize = 0; // 仅表示实际能存数据的大小，不含任何头部信息
    uint64_t mMaxBlockSize = 0;

    std::atomic<bool> mIsRecovering{false};

    DEFINE_REF_COUNT_VARIABLE;
};
}
}

#endif // MMS_CACHE_H

