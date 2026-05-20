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

static constexpr uint64_t INVALID_BLOCK_OFFSET = UINT64_MAX;
constexpr uint16_t DATA_ALIVE = 0;
constexpr uint16_t DATA_DELETED = 1;

BResult Cache::Init(uint64_t bucketMemAddr, uint64_t bucketMemSize, CacheLogFunc func, bool server,
                    std::pair<uint64_t, uint64_t> blockSize)
{
    CacheLog::Instance()->SetLogFuncFunc(func);

    mBaseAddr = bucketMemAddr;

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

    CACHE_LOG_INFO("Init cache success, bucketCount:" << bucketCount << ", value base block size:" << blockSize.first
                                                      << ".");
    return MMS_OK;
}

void Cache::Exit()
{
}

void FreeValueBlock(IndexValue *indexValue, MmsMemMgrPtr memMgr, MmsMemAllocatorPtr valueAllocator)
{
    if (indexValue->blockOffset == INVALID_BLOCK_OFFSET) {
        return;
    }

    uint64_t blockAddr;
    memMgr->Trans2Addr(MMAP_AREA_VALUE, indexValue->blockOffset, blockAddr);
    MMS_TRACE_START(CACHE_FREE_BLOCK);
    valueAllocator->MmsFree(blockAddr);
    MMS_TRACE_END(CACHE_FREE_BLOCK, MMS_OK);
}

BResult Cache::AllocDataBlock(uint64_t remainLen, uint16_t &numaId, uint64_t &curBlockAddr, uint64_t &curBuffSize)
{
    MMS_TRACE_START(CACHE_ALLOC_BLOCK);
    BResult ret = mValueAllocator->MmsAlloc(remainLen + DATA_HEADER_SIZE, numaId, curBlockAddr);
    MMS_TRACE_END(CACHE_ALLOC_BLOCK, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Alloc value block failed, size:" << remainLen + DATA_HEADER_SIZE << ", ret:" << ret << ".");
        return MMS_ALLOC_FAIL;
    }

    uint64_t blockSize = mValueAllocator->GetBlockSize(curBlockAddr);
    if (UNLIKELY(blockSize <= DATA_HEADER_SIZE)) {
        mValueAllocator->MmsFree(curBlockAddr);
        CACHE_LOG_ERROR("Invalid value block size:" << blockSize << ".");
        return MMS_ALLOC_FAIL;
    }

    curBuffSize = blockSize - DATA_HEADER_SIZE;
    return MMS_OK;
}

BResult Cache::PutDataIntoBlock(IndexValue *indexValue, const char *data, uint64_t dataLen)
{
    if (indexValue == nullptr || data == nullptr || dataLen == 0) {
        CACHE_LOG_ERROR("Invalid para.");
        return MMS_INVALID_PARAM;
    }

    uint64_t curBlockAddr;
    uint64_t curBuffSize;
    uint16_t numaId;
    BResult ret = AllocDataBlock(dataLen, numaId, curBlockAddr, curBuffSize);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    auto header = reinterpret_cast<DataHeader *>(curBlockAddr);
    header->blockSize = curBuffSize;
    ret = memcpy_s(header->data, curBuffSize, data, dataLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
        mValueAllocator->MmsFree(curBlockAddr);
        return MMS_INNER_ERR;
    }

    mMemMgr->Trans2Offset(MMAP_AREA_VALUE, curBlockAddr, indexValue->blockOffset);
    indexValue->totalDataLen += dataLen;
    return MMS_OK;
}

uint64_t Cache::GetDataFromBlock(IndexValue *indexValue, char *data, uint64_t offset, uint64_t dataLen)
{
    if (UNLIKELY(indexValue == nullptr || data == nullptr || dataLen == 0 || (offset >= indexValue->totalDataLen))) {
        CACHE_LOG_ERROR("Invalid para.");
        return 0;
    }

    uint64_t currentBlockAddr;
    mMemMgr->Trans2Addr(MMAP_AREA_VALUE, indexValue->blockOffset, currentBlockAddr);
    auto header = reinterpret_cast<DataHeader *>(currentBlockAddr);
    uint64_t realLen = std::min(indexValue->totalDataLen - offset, dataLen);
    if (UNLIKELY(offset > header->blockSize || realLen > header->blockSize - offset)) {
        CACHE_LOG_ERROR("Block data out of range.");
        return 0;
    }
    int ret = memcpy_s(data, dataLen, header->data + offset, realLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
        return 0;
    }
    return realLen;
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

    uint64_t realLen = GetDataFromBlock(currentValue, cacheValue, 0, length);
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
    indexValue->blockOffset = INVALID_BLOCK_OFFSET;
    indexValue->bucketNode = bucketNode;

    ret = strncpy_s(indexValue->key, MAX_KEY_SIZE, key, keyStr.size());
    if (UNLIKELY(ret != 0)) {
        CACHE_LOG_ERROR("Copy key failed, ret:" << ret << "key:" << key << ".");
        mIndexMemAllocator->MmsFree(indexValueAddr);
        return MMS_ERR;
    }
    // 拷贝数据到cache
    ret = PutDataIntoBlock(indexValue, value, length);
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
        FreeValueBlock(indexValue, mMemMgr, mValueAllocator);
        mIndexMemAllocator->MmsFree(indexValueAddr);
        return ret;
    }

    indexValue->next = bucketNode->head;
    bucketNode->head = {hashCode, FLAG_VALID, numaId, indexNumaOffset, indexValueAddr};

    // insert art tree
    mLsmArtTree.Insert(std::move(keyStr), indexValue);
    CacheWriteUnLock(&bucketNode->status);
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
        uint64_t realLen = GetDataFromBlock(indexValue, value, offset, readLen);
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

BResult Cache::ReviveDataBlock(IndexValue *indexValue, const char *data, uint64_t offset, uint64_t dataLen)
{
    if (UNLIKELY(offset != 0)) {
        CACHE_LOG_ERROR("Cannot update empty data with nonzero offset, offset:" << offset << ".");
        return MMS_INNER_ERR;
    }

    // 复活墓碑数据
    BResult ret = PutDataIntoBlock(indexValue, data, dataLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Put data into cache failed, ret:" << ret << ", key:" << indexValue->key <<  ".");
        return ret;
    }
    mLsmArtTree.Insert(indexValue->key, indexValue);
    return MMS_OK;
}

BResult Cache::UpdateDataInCurrentBlock(IndexValue *indexValue, DataHeader *header, const char *data, uint64_t offset,
                                        uint64_t dataLen)
{
    BResult ret = memcpy_s(header->data + offset, header->blockSize - offset, data, dataLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
        return MMS_INNER_ERR;
    }
    indexValue->totalDataLen = std::max(indexValue->totalDataLen, offset + dataLen);
    return MMS_OK;
}

BResult Cache::ExpandAndUpdateDataBlock(IndexValue *indexValue, const char *data, uint64_t offset, uint64_t dataLen,
                                        uint64_t curBlockAddr)
{
    auto header = reinterpret_cast<DataHeader *>(curBlockAddr);
    uint64_t newDataLen = std::max(indexValue->totalDataLen, offset + dataLen);
    uint64_t newBlockAddr;
    uint64_t newBuffSize;
    uint16_t numaId;
    BResult ret = AllocDataBlock(newDataLen, numaId, newBlockAddr, newBuffSize);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Alloc value block failed, ret:" << ret << ".");
        return ret;
    }

    auto newHeader = reinterpret_cast<DataHeader *>(newBlockAddr);
    newHeader->blockSize = newBuffSize;
    ret = memcpy_s(newHeader->data, newBuffSize, header->data, indexValue->totalDataLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
        mValueAllocator->MmsFree(newBlockAddr);
        return MMS_INNER_ERR;
    }

    ret = memcpy_s(newHeader->data + offset, newBuffSize - offset, data, dataLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Memory copy failed, ret:" << ret << ".");
        mValueAllocator->MmsFree(newBlockAddr);
        return MMS_INNER_ERR;
    }

    mMemMgr->Trans2Offset(MMAP_AREA_VALUE, newBlockAddr, indexValue->blockOffset);
    indexValue->totalDataLen = newDataLen;
    mValueAllocator->MmsFree(curBlockAddr);
    return MMS_OK;
}

BResult Cache::UpdateDataBlock(IndexValue *indexValue, const char *data, uint64_t offset, uint64_t dataLen)
{
    if (UNLIKELY(indexValue == nullptr || data == nullptr)) {
        CACHE_LOG_ERROR("Invalid para.");
        return MMS_INVALID_PARAM;
    }

    if (offset > indexValue->totalDataLen) {  // 不允许本次更新与老数据之间有空洞
        CACHE_LOG_ERROR("Out of bounds, data end:" << indexValue->totalDataLen << ", offset" << offset << ".");
        return MMS_INNER_ERR;
    }

    uint64_t curBlockOffset = indexValue->blockOffset;
    if (UNLIKELY(curBlockOffset == INVALID_BLOCK_OFFSET)) {
        return ReviveDataBlock(indexValue, data, offset, dataLen);
    }

    if (UNLIKELY(dataLen > UINT64_MAX - offset)) {
        CACHE_LOG_ERROR("Invalid update range, offset:" << offset << ", length:" << dataLen << ".");
        return MMS_INVALID_PARAM;
    }

    uint64_t curBlockAddr;
    uint64_t newDataLen = std::max(indexValue->totalDataLen, offset + dataLen);
    mMemMgr->Trans2Addr(MMAP_AREA_VALUE, curBlockOffset, curBlockAddr);
    auto header = reinterpret_cast<DataHeader *>(curBlockAddr);
    if (LIKELY(newDataLen <= header->blockSize)) {
        return UpdateDataInCurrentBlock(indexValue, header, data, offset, dataLen);
    }

    return ExpandAndUpdateDataBlock(indexValue, data, offset, dataLen, curBlockAddr);
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

        BResult ret = UpdateDataBlock(indexValue, value, offset, length);
        if (UNLIKELY(ret != MMS_OK)) {
            CacheWriteUnLock(&bucketNode->status);
            CACHE_LOG_ERROR("Update data block failed, key:" << key << ", ret:" << ret << ".");
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
    indexValue->blockOffset = INVALID_BLOCK_OFFSET;
    indexValue->bucketNode = bucketNode;

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

        FreeValueBlock(indexValue, mMemMgr, mValueAllocator);
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
    indexValue->blockOffset = INVALID_BLOCK_OFFSET;

    ret = strncpy_s(indexValue->key, MAX_KEY_SIZE, key.c_str(), key.size());
    if (UNLIKELY(ret != 0)) {
        CACHE_LOG_ERROR("Copy key failed, ret:" << ret << "key:" << key << ".");
        mIndexMemAllocator->MmsFree(indexValueAddr);
        return MMS_ERR;
    }
    // 拷贝数据到cache
    ret = PutDataIntoBlock(indexValue, value, length);
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

        ret = UpdateDataBlock(indexValue, para.value, para.offset, para.length);
        if (UNLIKELY(ret != MMS_OK)) {
            CacheWriteUnLock(&bucketNode->status);
            CACHE_LOG_ERROR("Update data block failed, key:" << para.key << ", ret:" << ret << ".");
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

    mLsmArtTree.Insert(std::move(keyStr), indexValue);
    CacheWriteUnLock(&bucketNode->status);
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

            FreeValueBlock(indexValue, mMemMgr, mValueAllocator);

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
    if (UNLIKELY(valueBuff == nullptr)) {
        CacheReadUnLock(&bucketNode->status);
        delete[] keyBuff;
        freeMemFuc();
        return MMS_ALLOC_FAIL;
    }

    keyValues->push_back({keyBuff, valueBuff, indexValue->totalDataLen});
    int32_t ret = strncpy_s(keyBuff, keyLen + NO_1, indexValue->key, keyLen);
    if (UNLIKELY(ret != 0)) {
        CacheReadUnLock(&bucketNode->status);
        freeMemFuc();
        return MMS_INNER_ERR;
    }

    uint64_t readLen = insCtx->GetDataFromBlock(indexValue, valueBuff, 0, indexValue->totalDataLen);
    if (UNLIKELY(readLen == 0)) {
        CacheReadUnLock(&bucketNode->status);
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
    art_range_bound startBound = {reinterpret_cast<const unsigned char *>(keyStart),
                                  static_cast<int>(strlen(keyStart))};
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
