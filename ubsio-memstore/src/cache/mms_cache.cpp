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
constexpr uint32_t FNV_OFFSET_BASIS = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;

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

void Cache::SetArtSwitch(bool artSwitch)
{
    mArtSwitch = artSwitch;
}

void FreeValueBlock(IndexValue *indexValue, MmsMemMgrPtr memMgr, MmsMemAllocatorPtr valueAllocator)
{
    if (indexValue->blockOffset == INVALID_BLOCK_OFFSET) {
        return;
    }

    uint64_t blockAddr;
    memMgr->Trans2Addr(MMAP_AREA_VALUE, indexValue->blockOffset, blockAddr);
    valueAllocator->MmsFree(blockAddr);
}

static uint32_t HashKey(const char *key, uint16_t keyLen)
{
    uint32_t hashCode = FNV_OFFSET_BASIS;
    for (uint16_t index = 0; index < keyLen; index++) {
        hashCode ^= static_cast<uint8_t>(key[index]);
        hashCode *= FNV_PRIME;
    }
    return hashCode;
}

static bool IsSameKey(const IndexValue *indexValue, const char *key, uint16_t keyLen)
{
    return indexValue->keyLen == keyLen && memcmp(indexValue->key, key, keyLen) == 0;
}

static BResult CopyIndexKey(IndexValue *indexValue, const char *key, uint16_t keyLen)
{
    BResult ret = memcpy_s(indexValue->key, MAX_KEY_SIZE, key, keyLen);
    if (UNLIKELY(ret != EOK)) {
        return MMS_ERR;
    }
    indexValue->key[keyLen] = '\0';
    indexValue->keyLen = keyLen;
    return MMS_OK;
}

BResult Cache::AllocDataBlock(uint64_t remainLen, uint16_t &numaId, uint64_t &curBlockAddr, uint64_t &curBuffSize)
{
    BResult ret = mValueAllocator->MmsAlloc(remainLen + DATA_HEADER_SIZE, numaId, curBlockAddr);
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
    if (UNLIKELY(indexValue == nullptr || data == nullptr || dataLen == 0)) {
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

uint64_t Cache::GetDataAddrFromBlock(IndexValue *indexValue, char **data, uint64_t offset, uint64_t dataLen)
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

    *data = header->data + offset;
    return realLen;
}

static IndexNode *FindExistingNode(BucketNode *bucketNode, const char *key, uint16_t keyLen, uint32_t hashCode)
{
    IndexNode *node = &bucketNode->head;
    while (node->valid == FLAG_VALID) {
        IndexValue *currentValue = reinterpret_cast<IndexValue *>(node->indexValueAddr);
        if (node->hashCode == hashCode && IsSameKey(currentValue, key, keyLen)) {
            return node;
        }
        node = &currentValue->next;
    }

    return nullptr;
}

BResult Cache::HandlePutExistingNode(const ExistingPutPara &para)
{
    IndexValue *currentValue = reinterpret_cast<IndexValue *>(para.existingNode->indexValueAddr);
    if (currentValue->totalDataLen != para.length) {
        CACHE_LOG_ERROR("Conflict put, key:" << std::string(para.key, para.keyLen) << ".");
        return MMS_INNER_ERR;
    }

    char *cacheValue = new (std::nothrow) char[para.length];
    if (UNLIKELY(cacheValue == nullptr)) {
        CACHE_LOG_ERROR("Memory alloc failed.");
        return MMS_ALLOC_FAIL;
    }

    uint64_t realLen = GetDataFromBlock(currentValue, cacheValue, 0, para.length);
    if (realLen == 0 || realLen != para.length) {
        CACHE_LOG_ERROR("Get data from cache failed, key:" << std::string(para.key, para.keyLen) << ".");
        delete[] cacheValue;
        return MMS_INNER_ERR;
    }

    if (memcmp(cacheValue, para.value, para.length) == 0) {
        if (para.valueAddr != nullptr) {
            uint64_t realLen = GetDataAddrFromBlock(currentValue, para.valueAddr, 0, para.length);
            if (UNLIKELY(realLen != para.length)) {
                CACHE_LOG_ERROR("Get data address from cache failed, key:" << std::string(para.key, para.keyLen)
                                                                           << ".");
                delete[] cacheValue;
                return MMS_INNER_ERR;
            }
        }
        CACHE_LOG_INFO("Repeat put, key:" << std::string(para.key, para.keyLen) << ".");
        delete[] cacheValue;
        return MMS_PUT_REPEAT;
    }

    CACHE_LOG_ERROR("Conflict put, key:" << std::string(para.key, para.keyLen) << ".");
    delete[] cacheValue;
    return MMS_INNER_ERR;
}

BResult Cache::CreatePutIndexValue(const PutPara &para, BucketNode *bucketNode, IndexValueCtx &ctx)
{
    BResult ret = mIndexMemAllocator->MmsAlloc(INDEX_VALUE_SIZE, ctx.numaId, ctx.addr);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Alloc indexValue block failed, ret:" << ret << ".");
        return MMS_ALLOC_FAIL;
    }

    mMemMgr->Trans2Offset(MMAP_AREA_INDEX, ctx.addr, ctx.numaOffset);
    ctx.value = reinterpret_cast<IndexValue *>(ctx.addr);
    ctx.value->totalDataLen = 0;
    ctx.value->version = para.version;
    ctx.value->ptId = para.ptId;
    ctx.value->isDelete = DATA_ALIVE;
    ctx.value->blockOffset = INVALID_BLOCK_OFFSET;
    ctx.value->bucketNode = bucketNode;

    ret = CopyIndexKey(ctx.value, para.key, para.keyLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Copy key failed, ret:" << ret << ", key:" << std::string(para.key, para.keyLen) << ".");
        mIndexMemAllocator->MmsFree(ctx.addr);
        return ret;
    }

    ret = PutDataIntoBlock(ctx.value, para.value, para.length);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Put data into cache failed, ret:" << ret << ".");
        mIndexMemAllocator->MmsFree(ctx.addr);
        return ret;
    }

    return MMS_OK;
}

void Cache::FreeIndexValue(const IndexValueCtx &ctx)
{
    FreeValueBlock(ctx.value, mMemMgr, mValueAllocator);
    mIndexMemAllocator->MmsFree(ctx.addr);
}

void Cache::FillPutValueAddr(IndexValue *indexValue, char **valueAddr)
{
    if (UNLIKELY(valueAddr == nullptr)) {
        return;
    }

    uint64_t curValueBlockAddr;
    mMemMgr->Trans2Addr(MMAP_AREA_VALUE, indexValue->blockOffset, curValueBlockAddr);
    auto header = reinterpret_cast<DataHeader *>(curValueBlockAddr);
    *valueAddr = header->data;
}

void Cache::InsertPutIndexValue(BucketNode *bucketNode, uint32_t hashCode, const IndexValueCtx &ctx)
{
    ctx.value->next = bucketNode->head;
    bucketNode->head = {hashCode, FLAG_VALID, ctx.numaId, ctx.numaOffset, ctx.addr};
    if (mArtSwitch) {
        mLsmArtTree.Insert(reinterpret_cast<const unsigned char *>(ctx.value->key), ctx.value->keyLen, ctx.value);
    }
}

BResult Cache::Put(const PutPara &para)
{
    uint32_t hashCode = HashKey(para.key, para.keyLen);
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);
    IndexValueCtx ctx = {};
    BResult ret = CreatePutIndexValue(para, bucketNode, ctx);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    CacheWriteLock(&bucketNode->status);
    IndexNode *existingNode = FindExistingNode(bucketNode, para.key, para.keyLen, hashCode);
    if (existingNode != nullptr) {
        ret = HandlePutExistingNode({existingNode, para.key, para.keyLen, para.value, para.length, para.valueAddr});
        CacheWriteUnLock(&bucketNode->status);
        FreeIndexValue(ctx);
        return ret;
    }

    FillPutValueAddr(ctx.value, para.valueAddr);
    InsertPutIndexValue(bucketNode, hashCode, ctx);
    CacheWriteUnLock(&bucketNode->status);
    CACHE_LOG_DEBUG("Put success, key:" << std::string(para.key, para.keyLen) << ", length:" << para.length
                                        << ", ptId:" << para.ptId << ", version:" << para.version << ".");
    return MMS_OK;
}

BResult Cache::Get(const GetPara &para)
{
    uint32_t hashCode = HashKey(para.key, para.keyLen);
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

    CacheReadLock(&bucketNode->status);
    IndexNode *node = &bucketNode->head;
    while (node->valid == FLAG_VALID) {
        uint64_t indexAddr;
        mMemMgr->Trans2Addr(MMAP_AREA_INDEX, node->numaOffset, indexAddr);
        IndexValue *indexValue = reinterpret_cast<IndexValue *>(indexAddr);
        if (node->hashCode != hashCode || !IsSameKey(indexValue, para.key, para.keyLen)) {
            node = &indexValue->next;
            continue;
        }

        if (indexValue->totalDataLen <= para.offset) {
            CACHE_LOG_ERROR("Out of bounds, key:" << std::string(para.key, para.keyLen) << ", offset:" << para.offset
                                                  << ", total len:" << indexValue->totalDataLen << ".");
            CacheReadUnLock(&bucketNode->status);
            return MMS_ERR;
        }

        uint64_t readLen = std::min(indexValue->totalDataLen - para.offset, para.length);
        uint64_t realLen;
        if (*para.value == nullptr) {
            realLen = GetDataAddrFromBlock(indexValue, para.value, para.offset, readLen);
        } else {
            realLen = GetDataFromBlock(indexValue, *para.value, para.offset, readLen);
        }
        if (UNLIKELY(realLen == 0)) {
            CacheReadUnLock(&bucketNode->status);
            CACHE_LOG_ERROR("Get data failed.");
            return MMS_INNER_ERR;
        }

        *para.realLength = realLen;
        CacheReadUnLock(&bucketNode->status);
        CACHE_LOG_DEBUG("Get success, key:" << std::string(para.key, para.keyLen) << ", offset:" << para.offset
                                            << ", real length:" << realLen << ".");
        return MMS_OK;
    }

    CacheReadUnLock(&bucketNode->status);
    *para.realLength = 0;
    CACHE_LOG_WARN("Not found, key:" << std::string(para.key, para.keyLen) << ".");
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
    if (mArtSwitch) {
        mLsmArtTree.Insert(reinterpret_cast<const unsigned char *>(indexValue->key), indexValue->keyLen, indexValue);
    }
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

BResult Cache::Update(const UpdatePara &para)
{
    uint32_t hashCode = HashKey(para.key, para.keyLen);
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

    CacheWriteLock(&bucketNode->status);
    IndexNode *node = &bucketNode->head;
    while (node->valid == FLAG_VALID) {
        IndexValue *indexValue = reinterpret_cast<IndexValue *>(node->indexValueAddr);
        if (node->hashCode != hashCode || !IsSameKey(indexValue, para.key, para.keyLen)) {
            node = &indexValue->next;
            continue;
        }

        BResult ret = UpdateDataBlock(indexValue, para.value, para.offset, para.length);
        if (UNLIKELY(ret != MMS_OK)) {
            CacheWriteUnLock(&bucketNode->status);
            CACHE_LOG_ERROR("Update data block failed, key:" << std::string(para.key, para.keyLen) << ", ret:" << ret
                                                             << ".");
            return MMS_ERR;
        }

        CACHE_LOG_DEBUG("Update success, key:" << std::string(para.key, para.keyLen) << ", offset:" << para.offset
                                               << ", length:" << para.length
                                               << ", new length:" << indexValue->totalDataLen << ", old version:"
                                               << indexValue->version << ", new version:" << para.version << ".");
        indexValue->version = para.version;
        CacheWriteUnLock(&bucketNode->status);
        return MMS_OK;
    }

    CacheWriteUnLock(&bucketNode->status);
    return MMS_NOT_EXISTS;
}

BResult Cache::HandleDeleteExistingNode(BucketNode *bucketNode, IndexNode *node, uint32_t version)
{
    IndexValue *indexValue = reinterpret_cast<IndexValue *>(node->indexValueAddr);
    if (mIsRecovering.load(std::memory_order_acquire)) {
        indexValue->isDelete = DATA_DELETED;
        indexValue->version = version;
        CacheWriteUnLock(&bucketNode->status);
        return MMS_OK;
    }

    FreeValueBlock(indexValue, mMemMgr, mValueAllocator);
    uint64_t indexValueAddr = node->indexValueAddr;
    *node = indexValue->next;
    CacheWriteUnLock(&bucketNode->status);
    mIndexMemAllocator->MmsFree(indexValueAddr);
    return MMS_OK;
}

BResult Cache::HandleDeleteMissingNode(BucketNode *bucketNode, uint32_t hashCode, const char *key, uint16_t keyLen,
                                       uint32_t version)
{
    if (!mIsRecovering.load(std::memory_order_acquire)) {
        CacheWriteUnLock(&bucketNode->status);
        CACHE_LOG_DEBUG("Key not found, skipping deletion, key:" << std::string(key, keyLen) << ".");
        return MMS_KEY_NOT_EXISTS;
    }

    BResult ret = InsertTombEntry(bucketNode, hashCode, version, key, keyLen);
    CacheWriteUnLock(&bucketNode->status);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Insert a tomb entry failed, ret:" << ret << ", key:" << std::string(key, keyLen) << ".");
        return ret;
    }

    CACHE_LOG_DEBUG("Key not found, insert tomb entry success, key:" << std::string(key, keyLen) << ".");
    return MMS_KEY_NOT_EXISTS;
}

BResult Cache::InsertTombEntry(BucketNode *bucketNode, uint32_t hashCode, uint32_t version, const char *key,
                               uint16_t keyLen)
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

    ret = CopyIndexKey(indexValue, key, keyLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Copy key failed, ret:" << ret << ", key:" << std::string(key, keyLen) << ".");
        mIndexMemAllocator->MmsFree(indexValueAddr);
        return ret;
    }

    indexValue->next = bucketNode->head;
    bucketNode->head = {hashCode, FLAG_VALID, numaId, indexNumaOffset, indexValueAddr};

    return MMS_OK;
}

BResult Cache::Delete(const char *key, uint16_t keyLen, uint32_t version)
{
    uint32_t hashCode = HashKey(key, keyLen);
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

    if (mArtSwitch) {
        mArtValueLock.LockWrite();
    }
    CacheWriteLock(&bucketNode->status);
    IndexNode *node = &bucketNode->head;
    while (node->valid == FLAG_VALID) {
        IndexValue *indexValue = reinterpret_cast<IndexValue *>(node->indexValueAddr);
        if (node->hashCode != hashCode || !IsSameKey(indexValue, key, keyLen)) {
            node = &indexValue->next;
            continue;
        }

        BResult ret = HandleDeleteExistingNode(bucketNode, node, version);
        if (UNLIKELY(ret != MMS_OK)) {
            if (mArtSwitch) {
                mArtValueLock.UnLock();
            }
            CACHE_LOG_ERROR("Delete existing node failed, ret:" << ret << ", key:" << std::string(key, keyLen) << ".");
            return ret;
        }
        if (mArtSwitch) {
            mLsmArtTree.Delete(reinterpret_cast<const unsigned char *>(key), keyLen);
            mArtValueLock.UnLock();
        }
        CACHE_LOG_DEBUG("Delete success, key:" << std::string(key, keyLen) << ".");
        return MMS_OK;
    }

    BResult ret = HandleDeleteMissingNode(bucketNode, hashCode, key, keyLen, version);
    if (mArtSwitch) {
        mArtValueLock.UnLock();
    }
    return ret;
}

BResult Cache::HandleReplacePut(IndexNode &curNode, const char *key, uint16_t keyLen, const char *value,
                                uint64_t length)
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

    ret = CopyIndexKey(indexValue, key, keyLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Copy key failed, ret:" << ret << ", key:" << std::string(key, keyLen) << ".");
        mIndexMemAllocator->MmsFree(indexValueAddr);
        return ret;
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

BResult Cache::ReplaceExistingNode(IndexNode *existingNode, const ReplacePara &para)
{
    IndexValue *indexValue = reinterpret_cast<IndexValue *>(existingNode->indexValueAddr);
    if (para.version < indexValue->version) {
        CACHE_LOG_DEBUG("Data version is lower, key:" << std::string(para.key, para.keyLen)
                                                      << ", new version:" << para.version
                                                      << ", old version:" << indexValue->version << ".");
        return MMS_OK;
    }

    BResult ret = UpdateDataBlock(indexValue, para.value, para.offset, para.length);
    if (UNLIKELY(ret != MMS_OK)) {
        CACHE_LOG_ERROR("Update data block failed, key:" << std::string(para.key, para.keyLen) << ", ret:" << ret
                                                         << ".");
        return MMS_ERR;
    }

    CACHE_LOG_DEBUG("Update success, key:" << std::string(para.key, para.keyLen) << ", offset:" << para.offset
                                           << ", length:" << para.length
                                           << ", new length:" << indexValue->totalDataLen << ", old version:"
                                           << indexValue->version << ", new version:" << para.version << ".");
    indexValue->version = para.version;
    indexValue->isDelete = DATA_ALIVE;
    return MMS_OK;
}

BResult Cache::InsertReplaceNode(BucketNode *bucketNode, uint32_t hashCode, const ReplacePara &para)
{
    IndexNode curNode;
    curNode.hashCode = hashCode;
    curNode.valid = FLAG_VALID;
    BResult ret = HandleReplacePut(curNode, para.key, para.keyLen, para.value, para.length);
    if (UNLIKELY(ret != MMS_OK)) {
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

    if (mArtSwitch) {
        mLsmArtTree.Insert(reinterpret_cast<const unsigned char *>(indexValue->key), indexValue->keyLen, indexValue);
    }
    CACHE_LOG_DEBUG("Put success, key:" << std::string(para.key, para.keyLen) << ", length:" << para.length
                                        << ", ptId:" << para.ptId << ", version:" << para.version << ".");
    return MMS_OK;
}

BResult Cache::Replace(const ReplacePara &para)
{
    uint32_t hashCode = HashKey(para.key, para.keyLen);
    uint32_t bucketIndex = hashCode % GetBucketCount();
    uint64_t bucketAddr = GetBucketAddr(bucketIndex);
    BucketNode *bucketNode = reinterpret_cast<BucketNode *>(bucketAddr);

    CacheWriteLock(&bucketNode->status);
    IndexNode *existingNode = FindExistingNode(bucketNode, para.key, para.keyLen, hashCode);
    BResult ret = existingNode == nullptr ? InsertReplaceNode(bucketNode, hashCode, para)
                                          : ReplaceExistingNode(existingNode, para);
    CacheWriteUnLock(&bucketNode->status);
    return ret;
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
    if (!mArtSwitch) {
        *valueInfoItems = nullptr;
        *itemNum = 0;
        CACHE_LOG_WARN("Art query switch is off.");
        return MMS_NOT_READY;
    }

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
    if (!mArtSwitch) {
        *valueInfoItems = nullptr;
        *itemNum = 0;
        CACHE_LOG_WARN("Art query switch is off.");
        return MMS_NOT_READY;
    }

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
    if (!mArtSwitch) {
        CACHE_LOG_WARN("Art query switch is off.");
        return MMS_NOT_READY;
    }

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
