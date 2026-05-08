/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "mms_cache.h"
#include <cstring>
#include "securec.h"
#include "mms_trace.h"

namespace ock {
namespace mms {

static constexpr double BLOCK_WASTE_THRESHOLD = 0.2;
static constexpr uint64_t INVALID_BLOCK_OFFSET = UINT64_MAX;
constexpr uint16_t DATA_ALIVE = 0;
constexpr uint16_t DATA_DELETED = 1;

BResult Cache::Init(uint64_t bucketMemAddr, uint64_t bucketMemSize, CacheLogFunc func, bool server,
                    std::pair<uint64_t, uint64_t> blockSize)
{
    CacheLog::Instance()->SetLogFuncFunc(func);

    mBaseAddr = bucketMemAddr;
    mBaseSize = bucketMemSize;
    mMinBlockSize = blockSize.first;
    mMaxBlockSize = blockSize.second;

    mMemMgr = MmsMemMgr::Instance();

    if (!server) {
        return MMS_OK;
    }

    mIndexMemAllocator = MmsMemAllocator::Instance(MMAP_AREA_INDEX);
    mValueAllocator = MmsMemAllocator::Instance(MMAP_AREA_VALUE);

    uint32_t bucketCount = static_cast<uint32_t>((bucketMemSize - BUCKET_NODE_BASE_OFFSET) / BUCKET_NODE_SIZE);
    PutBucketCount(bucketCount);
    for (uint32_t bucketIndex = 0; bucketIndex < bucketCount; bucketIndex++) {
        uint64_t bucketAddr = GetBucketAddr(bucketIndex);
        BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);
        bucketNode->status.state = 0;
        bucketNode->head.valid = FLAG_INVALID;
    }

    CACHE_LOG_INFO("Init cache success, bucketCount:" << bucketCount << ", min block size:" << mMinBlockSize
                                                      << ", max block size:" << mMaxBlockSize << ".");
    return MMS_OK;
}

void Cache::Exit()
{
}

void FreeBlocks(std::vector<uint64_t> allocBlocks, MmsMemAllocatorPtr allocator)
{
    for (auto &addr : allocBlocks) {
        allocator->MmsFree(addr);
    }
}

BResult Cache::AllocDataBlock(uint64_t remainLen, uint16_t &numaId, uint64_t &curBlockAddr, uint64_t &curBuffSize)
{
    uint64_t curBestDataSize = GetBestBlockSize(remainLen, BLOCK_WASTE_THRESHOLD);  // 仅表示数据部分长度
    uint64_t realBlockSize = curBestDataSize + DATA_HEADER_SIZE;
    BResult ret = mValueAllocator->MmsAlloc(realBlockSize, numaId, curBlockAddr);
    if (LIKELY(ret == MMS_OK)) {
        curBuffSize = curBestDataSize;
        return MMS_OK;
    }

    uint64_t standbyBlockSize = (curBestDataSize == mMinBlockSize ? mMaxBlockSize : mMinBlockSize) + DATA_HEADER_SIZE;
    ret = mValueAllocator->MmsAlloc(standbyBlockSize, numaId, curBlockAddr);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Alloc value block failed, size:" << standbyBlockSize << ", ret:" << ret << ".");
        return MMS_ALLOC_FAIL;
    }

    curBuffSize = standbyBlockSize - DATA_HEADER_SIZE;
    return MMS_OK;
}

uint64_t Cache::GetDataFromBlockList(IndexValue *indexValue, char *data, uint64_t offset, uint64_t dataLen)
{
    if (UNLIKELY(indexValue == nullptr || data == nullptr || dataLen == 0 || (offset >= indexValue->totalDataLen))) {
        CACHE_LOG_ERROR("Invalid para.");
        return 0;
    }

    uint64_t currentBlockAddr;
    uint64_t currentBlockOffset = indexValue->firstBlockOffset;
    uint64_t currentOffsetSeek = offset;
    DataHeader *header = nullptr;

    while (currentBlockOffset != INVALID_BLOCK_OFFSET) {
        mMemMgr->Trans2Addr(MMAP_AREA_VALUE, currentBlockOffset, currentBlockAddr);
        header = reinterpret_cast<DataHeader *>(currentBlockAddr);
        if (currentOffsetSeek < header->blockSize) {
            break;
        }

        currentOffsetSeek -= header->blockSize;
        currentBlockOffset = header->nextBlockOffset;
    }

    if (currentBlockOffset == INVALID_BLOCK_OFFSET) {
        CACHE_LOG_ERROR("Block list error, out of range.");
        return 0;
    }

    char *destPtr = data;
    uint64_t totalBytesRemain = indexValue->totalDataLen - offset;
    uint64_t remainingLen = std::min(dataLen, totalBytesRemain);
    uint64_t totalRead = 0;
    uint64_t offsetInsideBlock = currentOffsetSeek;
    uint64_t destCapacity = dataLen;

    while (remainingLen > 0 && currentBlockOffset != INVALID_BLOCK_OFFSET) {
        mMemMgr->Trans2Addr(MMAP_AREA_VALUE, currentBlockOffset, currentBlockAddr);
        header = reinterpret_cast<DataHeader *>(currentBlockAddr);
        char *srcPtr = header->data;
        uint64_t availableInBlock = header->blockSize - offsetInsideBlock;
        uint64_t bytesToCopy = std::min(remainingLen, availableInBlock);

        int ret = memcpy_s(destPtr, destCapacity, srcPtr + offsetInsideBlock, bytesToCopy);
        if (UNLIKELY(ret != MMS_OK)) {
            CACHE_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
            return 0;
        }

        destPtr += bytesToCopy;
        destCapacity -= bytesToCopy;
        remainingLen -= bytesToCopy;
        totalRead += bytesToCopy;

        offsetInsideBlock = 0;
        currentBlockOffset = header->nextBlockOffset;
    }

    return totalRead;
}

uint64_t Cache::GetBestBlockSize(uint64_t valueLen, double wasteThreshold)
{
    if (wasteThreshold <= 0 || wasteThreshold > NO_1) {
        return mMinBlockSize;
    }

    if (valueLen <= mMinBlockSize) {
        return mMinBlockSize;
    }

    if (valueLen >= mMaxBlockSize) {
        return mMaxBlockSize;
    }

    double factor = 1.0;
    uint64_t wasteThresholdSize = static_cast<uint64_t>(static_cast<double>(mMaxBlockSize) * (factor - wasteThreshold));
    if (wasteThresholdSize <= mMinBlockSize) {
        return mMinBlockSize;
    }

    if (valueLen >= wasteThresholdSize) {
        return mMaxBlockSize;
    }

    return mMinBlockSize;
}

BResult Cache::Get(const char *key, uint64_t offset, uint64_t length, char *value, uint64_t *realLength)
{
    uint32_t hashCode = static_cast<uint32_t>(std::hash<std::string>{}(key));
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

    CacheReadLock(&bucketNode->status);
    IndexNode *node = &bucketNode->head;
    while (node->valid == FLAG_VALID) {
        uint64_t indexAddr;
        mMemMgr->Trans2Addr(MMAP_AREA_INDEX, node->numaOffset, indexAddr);
        IndexValue *indexValue = reinterpret_cast<IndexValue *>(indexAddr);
        if (node->hashCode != hashCode || strcmp(indexValue->key, key) != 0) {
            node = &indexValue->next;
            continue;
        }

        if (indexValue->totalDataLen <= offset) {
            CACHE_LOG_ERROR("Out of bounds, key:" << key << ", offset:" << offset
                                                  << ", total len:" << indexValue->totalDataLen << ".");
            CacheReadUnLock(&bucketNode->status);
            return MMS_ERR;
        }

        uint64_t readLen = std::min(indexValue->totalDataLen - offset, length);
        uint64_t realLen = GetDataFromBlockList(indexValue, value, offset, readLen);
        if (UNLIKELY(realLen == 0)) {
            CacheReadUnLock(&bucketNode->status);
            CACHE_LOG_ERROR("Get data failed.");
            return MMS_INNER_ERR;
        }

        *realLength = realLen;
        CacheReadUnLock(&bucketNode->status);
        CACHE_LOG_DEBUG("Get success, key:" << key << ", offset:" << offset << ", real length:" << realLen << ".");
        return MMS_OK;
    }

    CacheReadUnLock(&bucketNode->status);
    *realLength = 0;  // 上层通过realLength == 0判断为该key没找到
    CACHE_LOG_WARN("Not found, key:" << key << ".");
    return MMS_NOT_EXISTS;
}

BResult Cache::Delete(const char *key, uint32_t version)
{
    uint32_t hashCode = static_cast<uint32_t>(std::hash<std::string>{}(key));
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

    CacheWriteLock(&bucketNode->status);
    IndexNode *node = &bucketNode->head;
    while (node->valid == FLAG_VALID) {
        IndexValue *indexValue = reinterpret_cast<IndexValue *>(node->indexValueAddr);
        if (node->hashCode != hashCode || strcmp(indexValue->key, key) != 0) {
            node = &indexValue->next;
            continue;
        }

        if (mIsRecovering.load(std::memory_order_acquire)) {
            indexValue->isDelete = DATA_DELETED;
            indexValue->version = version;
            CacheWriteUnLock(&bucketNode->status);
            return MMS_OK;
        }

        // 释放掉所有block
        uint64_t curBlockAddr;
        uint64_t curBlockOffset = indexValue->firstBlockOffset;
        uint64_t nextBlockOffset;
        while (curBlockOffset != INVALID_BLOCK_OFFSET) {
            mMemMgr->Trans2Addr(MMAP_AREA_VALUE, curBlockOffset, curBlockAddr);
            auto header = reinterpret_cast<DataHeader *>(curBlockAddr);
            nextBlockOffset = header->nextBlockOffset;
            mValueAllocator->MmsFree(curBlockAddr);
            curBlockOffset = nextBlockOffset;
        }

        uint64_t indexValueAddr = node->indexValueAddr;
        *node = indexValue->next;

        CacheWriteUnLock(&bucketNode->status);
        mIndexMemAllocator->MmsFree(indexValueAddr);
        CACHE_LOG_DEBUG("delete success, key:" << key << ".");
        return MMS_OK;
    }

    if (mIsRecovering.load(std::memory_order_acquire)) {
        // recover期间收到delete请求插入一条空的数据标记为delete，防止旧数据被写入
        BResult ret = InsertTombEntry(bucketNode, hashCode, version, key);
        if (UNLIKELY(ret != MMS_OK)) {
            CacheWriteUnLock(&bucketNode->status);
            CACHE_LOG_ERROR("Insert a tomb entry failed, ret:" << ret << ", key:" << key << ".");
            return ret;
        }
    }

    CacheWriteUnLock(&bucketNode->status);
    CACHE_LOG_DEBUG("Key not found, skipping deletion, key:" << key << ".");
    return MMS_KEY_NOT_EXISTS;
}

BResult Cache::HandleReplacePut(IndexNode &curNode, const char *key, const char *value, uint64_t length)
{
    BResult ret;
    uint64_t indexValueAddr;
    uint16_t numaId;
    // 申请indexValue内存
    ret = mIndexMemAllocator->MmsAlloc(INDEX_VALUE_SIZE, numaId, indexValueAddr);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Alloc indexValue block failed, ret:" << ret << ".");
        return MMS_ALLOC_FAIL;
    }

    uint64_t indexNumaOffset;
    mMemMgr->Trans2Offset(MMAP_AREA_INDEX, indexValueAddr, indexNumaOffset);
    IndexValue *indexValue = reinterpret_cast<IndexValue *>(indexValueAddr);
    indexValue->totalDataLen = 0;
    indexValue->firstBlockOffset = INVALID_BLOCK_OFFSET;

    ret = strncpy_s(indexValue->key, MAX_KEY_SIZE, key, strlen(key));
    if (UNLIKELY(ret != 0)) {
        CACHE_LOG_ERROR("Copy key failed, ret:" << ret << "key:" << key << ".");
        mIndexMemAllocator->MmsFree(indexValueAddr);
        return MMS_ERR;
    }
    // 拷贝数据到cache
    ret = PutDataIntoBlockList(indexValue, value, length);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Put data into cache failed, ret:" << ret << ".");
        mIndexMemAllocator->MmsFree(indexValueAddr);
        return ret;
    }

    curNode.numaId = numaId;
    curNode.numaOffset = indexNumaOffset;
    curNode.indexValueAddr = indexValueAddr;
    return MMS_OK;
}

BResult Cache::Replace(const ReplacePara &para)
{
    uint32_t hashCode = static_cast<uint32_t>(std::hash<std::string>{}(para.key));
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

    BResult ret;
    CacheWriteLock(&bucketNode->status);
    IndexNode *existingNode = FindExistingNode(bucketNode, para.key, hashCode);
    if (existingNode != nullptr) {  // 该key已经存在, 更新数据
        IndexValue *indexValue = reinterpret_cast<IndexValue *>(existingNode->indexValueAddr);
        if (para.version < indexValue->version) {
            CACHE_LOG_DEBUG("Data version is lower, key:" << para.key << ", new version:" << para.version
                                                          << ", old version:" << indexValue->version << ".");
            CacheWriteUnLock(&bucketNode->status);
            return MMS_OK;
        }

        ret = UpdateDataInBlockList(indexValue, para.value, para.offset, para.length);
        if (UNLIKELY(ret != MMS_OK)) {
            CacheWriteUnLock(&bucketNode->status);
            CACHE_LOG_ERROR("Update data in block list failed, key:" << para.key << ", ret:" << ret << ".");
            return MMS_ERR;
        }

        CACHE_LOG_DEBUG("Update success, key:" << para.key << ", offset:" << para.offset << ", length:" << para.length
                                               << ", new length:" << indexValue->totalDataLen << ", old version:"
                                               << indexValue->version << ", new version:" << para.version << ".");
        indexValue->version = para.version;
        indexValue->isDelete = DATA_ALIVE;
        CacheWriteUnLock(&bucketNode->status);
        return ret;
    }

    // key不存在，put数据
    IndexNode curNode;
    curNode.hashCode = hashCode;
    curNode.valid = FLAG_VALID;
    ret = HandleReplacePut(curNode, para.key, para.value, para.length);
    if (UNLIKELY(ret != MMS_OK)) {
        CacheWriteUnLock(&bucketNode->status);
        CACHE_LOG_ERROR("Put data into cache failed, ret:" << ret << ".");
        return ret;
    }

    IndexValue *indexValue = reinterpret_cast<IndexValue *>(curNode.indexValueAddr);
    indexValue->next = bucketNode->head;
    indexValue->ptId = para.ptId;
    indexValue->isDelete = DATA_ALIVE;
    indexValue->version = para.version;
    bucketNode->head = curNode;

    CacheWriteUnLock(&bucketNode->status);
    CACHE_LOG_DEBUG("Put success, key:" << para.key << ", length:" << para.length << ", ptId:" << para.ptId
                                        << ", version:" << para.version << ".");
    return MMS_OK;
}
}  // namespace mms
}  // namespace ock

