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
    sliceRef->Aquire();
    return sliceRef;
}

void WCacheIndex::Release(uint64_t ptId, WCacheSliceRefPtr &sliceRef)
{
    sliceRef->Release();
}

BResult WCacheIndex::Delete(uint64_t ptId, const Key &key)
{
    WCacheIndexTable *table = GetIndexTable(ptId);
    ChkTrueNot(table != nullptr, BIO_ALLOC_FAIL);
    auto bucket = Hash(key);
    WriteLocker<ReadWriteLock> lock(&table->sliceIndexLock[bucket]);
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