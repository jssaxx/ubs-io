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

void FreeValueBlocks(IndexValue *indexValue, MmsMemMgrPtr memMgr, MmsMemAllocatorPtr valueAllocator)
{
    uint64_t curBlockAddr;
    uint64_t curBlockOffset = indexValue->firstBlockOffset;
    uint64_t nextBlockOffset;
    while (curBlockOffset != INVALID_BLOCK_OFFSET) {
        memMgr->Trans2Addr(MMAP_AREA_VALUE, curBlockOffset, curBlockAddr);
        auto header = reinterpret_cast<DataHeader *>(curBlockAddr);
        nextBlockOffset = header->nextBlockOffset;
        valueAllocator->MmsFree(curBlockAddr);
        curBlockOffset = nextBlockOffset;
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

BResult Cache::PutDataIntoBlockList(IndexValue *indexValue, const char *data, uint64_t dataLen)
{
    if (indexValue == nullptr || data == nullptr || dataLen == 0) {
        CACHE_LOG_ERROR("Invalid para.");
        return MMS_INVALID_PARAM;
    }

    uint64_t lastBlockAddr = 0;
    uint64_t remainLen = dataLen;
    uint64_t curBlockAddr;
    uint64_t curBuffSize;
    uint64_t curBlockOffset;
    uint64_t dataOffset = 0;
    uint16_t numaId;
    std::vector<uint64_t> allocBlocks{};
    BResult ret;

    while (remainLen > 0) {
        ret = AllocDataBlock(remainLen, numaId, curBlockAddr, curBuffSize);
        if (UNLIKELY(ret != MMS_OK)) {
            CACHE_LOG_ERROR("Alloc value block failed, all memory blocks are exhausted, ret: " << ret << ".");
            FreeBlocks(allocBlocks, mValueAllocator);
            return MMS_ALLOC_FAIL;
        }
        allocBlocks.push_back(curBlockAddr);

        // 填入数据
        auto header = reinterpret_cast<DataHeader *>(curBlockAddr);
        header->blockSize = curBuffSize;
        header->nextBlockOffset = INVALID_BLOCK_OFFSET;
        char *dstPtr = header->data;
        uint64_t bytesToCopy = std::min(remainLen, curBuffSize);
        ret = memcpy_s(dstPtr, curBuffSize, data + dataOffset, bytesToCopy);
        if (UNLIKELY(ret != MMS_OK)) {
            CACHE_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
            FreeBlocks(allocBlocks, mValueAllocator);
            return MMS_INNER_ERR;
        }

        mMemMgr->Trans2Offset(MMAP_AREA_VALUE, curBlockAddr, curBlockOffset);
        if (lastBlockAddr == 0) {
            // 如果是第一个块
            indexValue->firstBlockOffset = curBlockOffset;
        } else {
            auto lastBlockHeader = reinterpret_cast<DataHeader *>(lastBlockAddr);
            lastBlockHeader->nextBlockOffset = curBlockOffset;
        }

        lastBlockAddr = curBlockAddr;
        dataOffset += bytesToCopy;
        remainLen -= bytesToCopy;
        indexValue->totalDataLen += bytesToCopy;
    }

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

IndexNode *FindExistingNode(BucketNode *bucketNode, const char *key, uint32_t hashCode)
{
    IndexNode *node = &bucketNode->head;
    while (node->valid == FLAG_VALID) {
        IndexValue *currentValue = reinterpret_cast<IndexValue *>(node->indexValueAddr);
        if (node->hashCode == hashCode && strcmp(currentValue->key, key) == 0) {
            return node;
        }
        node = &currentValue->next;
    }

    return nullptr;
}

BResult Cache::HandlePutExistingNode(IndexNode *existingNode, const char *key, const char *value, uint64_t length)
{
    IndexValue *currentValue = reinterpret_cast<IndexValue *>(existingNode->indexValueAddr);
    if (currentValue->totalDataLen != length) {
        CACHE_LOG_ERROR("Conflict put, key:" << key << ".");
        return MMS_INNER_ERR;
    }

    char *cacheValue = new (std::nothrow) char[length];
    if (UNLIKELY(cacheValue == nullptr)) {
        CACHE_LOG_ERROR("Memory alloc failed.");
        return MMS_ALLOC_FAIL;
    }

    uint64_t realLen = GetDataFromBlockList(currentValue, cacheValue, 0, length);
    if (realLen == 0 || realLen != length) {
        CACHE_LOG_ERROR("Get data from cache failed, key:" << key << ".");
        delete[] cacheValue;
        return MMS_INNER_ERR;
    }

    if (memcmp(cacheValue, value, length) == 0) {
        CACHE_LOG_INFO("Repeat put, key:" << key << ".");
        delete[] cacheValue;
        return MMS_PUT_REPEAT;
    }

    CACHE_LOG_ERROR("Conflict put, key:" << key << ".");
    delete[] cacheValue;
    return MMS_INNER_ERR;
}

BResult Cache::Put(const char *key, const char *value, uint64_t length, uint32_t version, MmsPtId ptId)
{
    std::string keyStr = std::string(key);
    uint32_t hashCode = static_cast<uint32_t>(std::hash<std::string>{}(keyStr));
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

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
    indexValue->version = version;
    indexValue->ptId = ptId;
    indexValue->isDelete = DATA_ALIVE;
    indexValue->firstBlockOffset = INVALID_BLOCK_OFFSET;
    indexValue->bucketNode = bucketNode;

    ret = strncpy_s(indexValue->key, MAX_KEY_SIZE, key, keyStr.size());
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

    CacheWriteLock(&bucketNode->status);
    IndexNode *existingNode = FindExistingNode(bucketNode, key, hashCode);
    if (existingNode != nullptr) {
        ret = HandlePutExistingNode(existingNode, key, value, length);
        CacheWriteUnLock(&bucketNode->status);
        FreeValueBlocks(indexValue, mMemMgr, mValueAllocator);
        mIndexMemAllocator->MmsFree(indexValueAddr);
        return ret;
    }

    indexValue->next = bucketNode->head;
    bucketNode->head = {hashCode, FLAG_VALID, numaId, indexNumaOffset, indexValueAddr};
    CacheWriteUnLock(&bucketNode->status);

    // insert art tree
    mLsmArtTree.Insert(std::move(keyStr), indexValue);
    CACHE_LOG_DEBUG("Put success, key:" << key << ", length:" << length << ", ptId:" << ptId << ", version:" << version
                                        << ".");
    return MMS_OK;
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

BResult Cache::HandleUpdateData(IndexValue *indexValue, uint64_t blockOffset, uint64_t currentOffsetSeek,
                                DataHeader *preHeader, UpdateInfo &updateInfo)
{
    std::vector<uint64_t> allocBlocks{};
    uint64_t remainLen = updateInfo.length;
    uint64_t curBuffSize;
    uint16_t numaId;
    uint64_t offsetInsideBlock = currentOffsetSeek;
    DataHeader *header = preHeader;
    uint64_t dataOffset = 0;
    uint64_t curBlockAddr;
    uint64_t curBlockOffset = blockOffset;
    BResult ret;

    while (remainLen > 0) {
        if (curBlockOffset == INVALID_BLOCK_OFFSET) {  // 从数据最末尾接着写,先申请一个块接上去
            ret = AllocDataBlock(remainLen, numaId, curBlockAddr, curBuffSize);
            if (UNLIKELY(ret != MMS_OK)) {
                CACHE_LOG_ERROR("Alloc value block failed, all memory blocks are exhausted, ret: " << ret << ".");
                FreeBlocks(allocBlocks, mValueAllocator);
                return MMS_ALLOC_FAIL;
            }

            auto curHeader = reinterpret_cast<DataHeader *>(curBlockAddr);
            curHeader->nextBlockOffset = INVALID_BLOCK_OFFSET;
            curHeader->blockSize = curBuffSize;
            allocBlocks.push_back(curBlockAddr);
            mMemMgr->Trans2Offset(MMAP_AREA_VALUE, curBlockAddr, curBlockOffset);
            header->nextBlockOffset = curBlockOffset;
        }

        mMemMgr->Trans2Addr(MMAP_AREA_VALUE, curBlockOffset, curBlockAddr);
        header = reinterpret_cast<DataHeader *>(curBlockAddr);
        uint64_t availableInBlock = header->blockSize - offsetInsideBlock;
        uint64_t bytesToCopy = std::min(remainLen, availableInBlock);
        char *dest = header->data + offsetInsideBlock;

        ret = memcpy_s(dest, header->blockSize - offsetInsideBlock, updateInfo.data + dataOffset, bytesToCopy);
        if (UNLIKELY(ret != MMS_OK)) {
            CACHE_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
            FreeBlocks(allocBlocks, mValueAllocator);
            return MMS_INNER_ERR;
        }

        remainLen -= bytesToCopy;
        dataOffset += bytesToCopy;
        offsetInsideBlock = 0;
        curBlockOffset = header->nextBlockOffset;
    }

    indexValue->totalDataLen = std::max(indexValue->totalDataLen, updateInfo.offset + updateInfo.length);
    return MMS_OK;
}

BResult Cache::UpdateDataInBlockList(IndexValue *indexValue, const char *data, uint64_t offset, uint64_t dataLen)
{
    if (UNLIKELY(indexValue == nullptr || data == nullptr)) {
        CACHE_LOG_ERROR("Invalid para.");
        return MMS_INVALID_PARAM;
    }

    if (offset > indexValue->totalDataLen) {  // 不允许本次更新与老数据之间有空洞
        CACHE_LOG_ERROR("Out of bounds, data end:" << indexValue->totalDataLen << ", offset" << offset << ".");
        return MMS_INNER_ERR;
    }

    uint64_t curBlockAddr;
    uint64_t curBlockOffset = indexValue->firstBlockOffset;
    if (UNLIKELY(curBlockOffset == INVALID_BLOCK_OFFSET)) {
        if (UNLIKELY(offset != 0)) {
            CACHE_LOG_ERROR("Cannot update empty data with nonzero offset, offset:" << offset << ".");
            return MMS_INNER_ERR;
        }

        //复活墓碑数据
        BResult ret = PutDataIntoBlockList(indexValue, data, dataLen);
        if (UNLIKELY(ret != MMS_OK)) {
            CACHE_LOG_ERROR("Put data into cache failed, ret:" << ret << ", key:" << indexValue->key <<  ".");
            return ret;
        }
        mLsmArtTree.Insert(indexValue->key, indexValue);
    }

    uint64_t currentOffsetSeek = offset;
    DataHeader *header = nullptr;

    // 先偏移到offset位置
    while (curBlockOffset != INVALID_BLOCK_OFFSET) {
        mMemMgr->Trans2Addr(MMAP_AREA_VALUE, curBlockOffset, curBlockAddr);
        header = reinterpret_cast<DataHeader *>(curBlockAddr);
        if (currentOffsetSeek < header->blockSize) {
            break;
        }

        currentOffsetSeek -= header->blockSize;
        curBlockOffset = header->nextBlockOffset;
    }

    UpdateInfo para = {data, offset, dataLen};
    BResult ret = HandleUpdateData(indexValue, curBlockOffset, currentOffsetSeek, header, para);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Update in block list failed, ret:" << ret << ".");
        return ret;
    }

    return MMS_OK;
}

BResult Cache::Update(const char *key, const char *value, uint64_t offset, uint64_t length, uint32_t version)
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

        BResult ret = UpdateDataInBlockList(indexValue, value, offset, length);
        if (UNLIKELY(ret != MMS_OK)) {
            CacheWriteUnLock(&bucketNode->status);
            CACHE_LOG_ERROR("Update data in block list failed, key:" << key << ", ret:" << ret << ".");
            return MMS_ERR;
        }

        CACHE_LOG_DEBUG("Update success, key:" << key << ", offset:" << offset << ", length:" << length
                                               << ", new length:" << indexValue->totalDataLen << ", old version:"
                                               << indexValue->version << ", new version:" << version << ".");
        indexValue->version = version;
        CacheWriteUnLock(&bucketNode->status);
        return MMS_OK;
    }

    CacheWriteUnLock(&bucketNode->status);
    return MMS_NOT_EXISTS;
}

BResult Cache::InsertTombEntry(BucketNode *bucketNode, uint32_t hashCode, uint32_t version, const char *key)
{
    uint64_t indexValueAddr;
    uint16_t numaId;
    // 申请indexValue内存
    BResult ret = mIndexMemAllocator->MmsAlloc(INDEX_VALUE_SIZE, numaId, indexValueAddr);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Alloc indexValue block failed, ret:" << ret << ".");
        return MMS_ALLOC_FAIL;
    }

    uint64_t indexNumaOffset;
    mMemMgr->Trans2Offset(MMAP_AREA_INDEX, indexValueAddr, indexNumaOffset);
    IndexValue *indexValue = reinterpret_cast<IndexValue *>(indexValueAddr);
    indexValue->totalDataLen = 0;
    indexValue->version = version;
    indexValue->isDelete = DATA_DELETED;
    indexValue->firstBlockOffset = INVALID_BLOCK_OFFSET;

    ret = strncpy_s(indexValue->key, MAX_KEY_SIZE, key, strlen(key));
    if (UNLIKELY(ret != 0)) {
        CACHE_LOG_ERROR("Copy key failed, ret:" << ret << "key:" << key << ".");
        mIndexMemAllocator->MmsFree(indexValueAddr);
        return MMS_ERR;
    }

    indexValue->next = bucketNode->head;
    bucketNode->head = {hashCode, FLAG_VALID, numaId, indexNumaOffset, indexValueAddr};

    return MMS_OK;
}

BResult Cache::Delete(const char *key, uint32_t version)
{
    std::string keyStr = std::string(key);
    uint32_t hashCode = static_cast<uint32_t>(std::hash<std::string>{}(keyStr));
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

    mArtValueLock.LockWrite();
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
            mLsmArtTree.Delete(std::move(keyStr));
            mArtValueLock.UnLock();
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
        mLsmArtTree.Delete(std::move(keyStr));
        mArtValueLock.UnLock();
        CACHE_LOG_DEBUG("delete success, key:" << key << ".");
        return MMS_OK;
    }

    if (mIsRecovering.load(std::memory_order_acquire)) {
        // recover期间收到delete请求插入一条空的数据标记为delete，防止旧数据被写入
        BResult ret = InsertTombEntry(bucketNode, hashCode, version, key);
        if (UNLIKELY(ret != MMS_OK)) {
            CacheWriteUnLock(&bucketNode->status);
            mArtValueLock.UnLock();
            CACHE_LOG_ERROR("Insert a tomb entry failed, ret:" << ret << ", key:" << key << ".");
            return ret;
        }
    }

    CacheWriteUnLock(&bucketNode->status);
    mArtValueLock.UnLock();
    CACHE_LOG_DEBUG("Key not found, skipping deletion, key:" << key << ".");
    return MMS_KEY_NOT_EXISTS;
}

BResult Cache::HandleReplacePut(IndexNode &curNode, const std::string &key, const char *value, uint64_t length)
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

    ret = strncpy_s(indexValue->key, MAX_KEY_SIZE, key.c_str(), key.size());
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
    std::string keyStr = std::string(para.key);
    uint32_t hashCode = static_cast<uint32_t>(std::hash<std::string>{}(keyStr));
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
    ret = HandleReplacePut(curNode, keyStr, para.value, para.length);
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
    indexValue->bucketNode = bucketNode;

    CacheWriteUnLock(&bucketNode->status);
    mLsmArtTree.Insert(std::move(keyStr), indexValue);
    CACHE_LOG_DEBUG("Put success, key:" << para.key << ", length:" << para.length << ", ptId:" << para.ptId
                                        << ", version:" << para.version << ".");
    return MMS_OK;
}

void Cache::ClearDeletedData()
{
    uint32_t bucketCount = GetBucketCount();
    for (uint32_t bucketIndex = 0; bucketIndex < bucketCount; bucketIndex++) {
        uint64_t bucketAddr = GetBucketAddr(bucketIndex);
        BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

        CacheWriteLock(&bucketNode->status);
        IndexNode *node = &bucketNode->head;
        while (node->valid == FLAG_VALID) {
            IndexValue *indexValue = reinterpret_cast<IndexValue *>(node->indexValueAddr);
            if (indexValue->isDelete != DATA_DELETED) {
                node = &indexValue->next;
                continue;
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

            CACHE_LOG_DEBUG("delete success, key:" << indexValue->key << ".");
            mIndexMemAllocator->MmsFree(indexValueAddr);
        }

        CacheWriteUnLock(&bucketNode->status);
    }

    CACHE_LOG_INFO("Clear deleted data done.");
}

struct CallBackCtx {
    std::vector<ValueInfo> *keyValueVec;
    Cache *self;
};

static int ArtSearchCallBack(void *data, const unsigned char *key, uint32_t keyLen, void *val)
{
    auto callBackCtx = static_cast<CallBackCtx *>(data);
    auto *keyValues = callBackCtx->keyValueVec;
    auto insCtx = callBackCtx->self;
    auto indexValue = reinterpret_cast<IndexValue *>(val);

    auto freeMemFuc = [keyValues]() {
        for (auto &item : *keyValues) {
            delete[] item.key;
            delete[] item.value;
        }
    };

    char *keyBuff = new (std::nothrow) char[keyLen + NO_1];
    if (UNLIKELY(keyBuff == nullptr)) {
        freeMemFuc();
        return MMS_ALLOC_FAIL;
    }

    auto bucketNode = indexValue->bucketNode;
    CacheReadLock(&bucketNode->status);
    char *valueBuff = new (std::nothrow) char[indexValue->totalDataLen];
    if (UNLIKELY(keyBuff == nullptr)) {
        delete[] keyBuff;
        freeMemFuc();
        return MMS_ALLOC_FAIL;
    }

    keyValues->push_back({keyBuff, valueBuff, indexValue->totalDataLen});
    int32_t ret = strncpy_s(keyBuff, MAX_KEY_SIZE, indexValue->key, keyLen);
    if (UNLIKELY(ret != 0)) {
        freeMemFuc();
        return MMS_INNER_ERR;
    }

    uint64_t readLen = insCtx->GetDataFromBlockList(indexValue, valueBuff, 0, indexValue->totalDataLen);
    if (UNLIKELY(readLen == 0)) {
        freeMemFuc();
        return MMS_INNER_ERR;
    }
    CacheReadUnLock(&bucketNode->status);

    return 0;
}

BResult Cache::GetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    std::vector<ValueInfo> keyValueVec;
    CallBackCtx ctx = {&keyValueVec, this};
    auto freeKVMemFunc = [&keyValueVec]() {
        for (auto &item : keyValueVec) {
            delete[] item.key;
            delete[] item.value;
        }
    };

    mArtValueLock.LockRead();
    int ret = mLsmArtTree.SearchPrefix(reinterpret_cast<const unsigned char *>(prefix),
                                       static_cast<int>(strlen(prefix)), ArtSearchCallBack, &ctx);
    mArtValueLock.UnLock();
    if (UNLIKELY(ret != 0)) {
        CACHE_LOG_ERROR("Search prefix in art tree failed, ret:" << ret << ".");
        return ret;
    }

    size_t count = keyValueVec.size();
    *itemNum = count;
    if (count == 0) {
        *valueInfoItems = nullptr;
        CACHE_LOG_DEBUG("No key matches the prefix:" << prefix << ".");
        return MMS_OK;
    }

    *valueInfoItems = new (std::nothrow) ValueInfo[count];
    if (UNLIKELY(*valueInfoItems == nullptr)) {
        freeKVMemFunc();
        return MMS_ALLOC_FAIL;
    }

    uint64_t totalSize = sizeof(ValueInfo) * count;
    ret = memcpy_s(*valueInfoItems, totalSize, keyValueVec.data(), totalSize);
    if (UNLIKELY(ret != 0)) {
        delete[] * valueInfoItems;
        freeKVMemFunc();
        return MMS_INNER_ERR;
    }

    CACHE_LOG_DEBUG("Get values by prefix success, prefix:" << prefix << ", count:" << count << ".");
    return MMS_OK;
}

BResult Cache::GetValuesByRange(const char *keyStart, const char *keyEnd, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    std::vector<ValueInfo> keyValueVec;
    CallBackCtx ctx = {&keyValueVec, this};
    auto freeKVMemFunc = [&keyValueVec]() {
        for (auto &item : keyValueVec) {
            delete[] item.key;
            delete[] item.value;
        }
    };

    art_range_bound startBound = {reinterpret_cast<const unsigned char *>(keyStart),
                                  static_cast<int>(strlen(keyStart))};
    art_range_bound endBound = {reinterpret_cast<const unsigned char *>(keyEnd), static_cast<int>(strlen(keyEnd))};
    mArtValueLock.LockRead();
    int ret = mLsmArtTree.SearchRange(startBound, endBound, ArtSearchCallBack, &ctx);
    mArtValueLock.UnLock();
    if (UNLIKELY(ret != 0)) {
        CACHE_LOG_ERROR("Search prefix in art tree failed, ret:" << ret << ".");
        return ret;
    }

    size_t count = keyValueVec.size();
    *itemNum = count;
    if (count == 0) {
        *valueInfoItems = nullptr;
        CACHE_LOG_DEBUG("No key matches for the range [" << keyStart << ", " << keyEnd << "]"
                                                         << ".");
        return MMS_OK;
    }

    *valueInfoItems = new (std::nothrow) ValueInfo[count];
    if (UNLIKELY(*valueInfoItems == nullptr)) {
        freeKVMemFunc();
        return MMS_ALLOC_FAIL;
    }

    uint64_t totalSize = sizeof(ValueInfo) * count;
    ret = memcpy_s(*valueInfoItems, totalSize, keyValueVec.data(), totalSize);
    if (UNLIKELY(ret != 0)) {
        delete[] *valueInfoItems;
        freeKVMemFunc();
        return MMS_INNER_ERR;
    }

    CACHE_LOG_DEBUG("Get values by range success, range:[" << keyStart << ", " << keyEnd << "]"
                                                           << ", count:" << count << ".");
    return MMS_OK;
}

BResult Cache::GetKeysByRange(const char *keyStart, const char *keyEnd, std::vector<std::string> &matchedKeys)
{
    auto callback = [](void *data, const unsigned char *key, uint32_t keyLen, void *val) -> int {
        auto *keys = static_cast<std::vector<std::string> *>(data);
        keys->emplace_back(reinterpret_cast<const char *>(key), keyLen);
        return 0;
    };

    matchedKeys.clear();
    art_range_bound startBound = {reinterpret_cast<const unsigned char *>(keyStart), static_cast<int>(strlen(keyStart))};
    art_range_bound endBound = {reinterpret_cast<const unsigned char *>(keyEnd), static_cast<int>(strlen(keyEnd))};
    int searchRet = mLsmArtTree.SearchRange(startBound, endBound, callback, &matchedKeys);
    if (UNLIKELY(searchRet != 0)) {
        CACHE_LOG_ERROR("Search range in art tree failed, ret:" << searchRet << ".");
        return searchRet;
    }

    return MMS_OK;
}

}  // namespace mms
}  // namespace ock
