/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "wcache_index.h"
namespace ock {
namespace bio {
inline uint32_t WCacheIndex::Hash(const Key &key)
{
    // TODO: optimize me.
    return std::hash<std::string>{}(key) % HASH_BUCKET_NUM;
}

BResult WCacheIndex::Insert(uint64_t ptId, const Key &key, const WCacheSliceRefPtr &sliceRef)
{
    WCacheIndexTable *table = GetIndexTable(ptId);
    ChkTrueNot(table != nullptr, BIO_ALLOC_FAIL);
    auto bucket = Hash(key);
    WriteLocker<ReadWriteLock> lock(&table->sliceIndexLock[bucket]);
    auto sliceMeta = table->sliceIndex[bucket].find(key);
    if (UNLIKELY(sliceMeta != table->sliceIndex[bucket].end())) {
        LOG_WARN("Repeat put, key:" << key);
    }
    table->sliceIndex[bucket].emplace(key, sliceRef);
    return BIO_OK;
}

WCacheSliceRefPtr WCacheIndex::Aquire(uint64_t ptId, const Key &key)
{
    WCacheIndexTable *table = GetIndexTable(ptId);
    ChkTrueNot(table != nullptr, nullptr);
    auto bucket = Hash(key);
    ReadLocker<ReadWriteLock> lock(&table->sliceIndexLock[bucket]);
    auto sliceMeta = table->sliceIndex[bucket].find(key);
    if (UNLIKELY(sliceMeta == table->sliceIndex[bucket].end())) {
        return nullptr;
    }
    auto sliceRef = sliceMeta->second;
    if (sliceRef->Aquire()) {
        return sliceRef;
    } else {
        return nullptr;
    }
}

BResult WCacheIndex::Delete(uint64_t ptId, const Key &key, WCacheSliceRefPtr sliceRef)
{
    WCacheIndexTable *table = GetIndexTable(ptId);
    ChkTrueNot(table != nullptr, BIO_ALLOC_FAIL);
    auto bucket = Hash(key);
    WriteLocker<ReadWriteLock> lock(&table->sliceIndexLock[bucket]);
    auto iter = table->sliceIndex[bucket].find(key);
    if (iter == table->sliceIndex[bucket].end()) {
        LOG_INFO("Delete write cache key:" << key << "have not exist.");
        return BIO_NOT_EXISTS;
    }
    if (iter->second != sliceRef) {
        LOG_INFO("Expired slice, key:" << key);
        return BIO_OK;
    }
    table->sliceIndex[bucket].erase(key);
    LOG_INFO("Delete key:" << key << " success.");
    return BIO_OK;
}

WCacheIndexTable *WCacheIndex::GetIndexTable(uint64_t ptId)
{
    {
        ReadLocker<ReadWriteLock> lock(&mTableLock);
        auto table = mTable.find(ptId);
        if (table != mTable.end()) {
            return table->second;
        }
    }
    {
        WriteLocker<ReadWriteLock> lock(&mTableLock);
        auto table = mTable.find(ptId);
        if (table != mTable.end()) {
            return table->second;
        }
        WCacheIndexTable *inTable = new WCacheIndexTable;
        ChkTrueNot(inTable != nullptr, nullptr);
        mTable.insert(std::make_pair(ptId, inTable));
        return mTable[ptId];
    }
}
}
}