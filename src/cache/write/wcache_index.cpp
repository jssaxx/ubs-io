/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "bio_tracepoint_helper.h"
#include "wcache_index.h"

namespace ock {
namespace bio {
WCacheIndex::~WCacheIndex()
{
    for (const auto &sliceIndex : mTable) {
        delete sliceIndex.second;
    }

    mTable.clear();
}
inline uint32_t WCacheIndex::Hash(const Key &key)
{
    return std::hash<std::string>{}(key) % HASH_BUCKET_NUM;
}

BResult WCacheIndex::Insert(uint16_t ptId, const Key &key, const WCacheSliceRefPtr &sliceRef)
{
    WCacheIndexTable *table = GetIndexTable(ptId);
    ChkTrue(table != nullptr, BIO_INVALID_PARAM, "Get write cache index table fail, ptId:" << ptId << ", key:" << key);
    auto bucket = Hash(key);
    WriteLocker<ReadWriteLock> lock(&table->sliceIndexLock[bucket]);
    auto sliceMeta = table->sliceIndex[bucket].find(key);
    if (UNLIKELY(sliceMeta != table->sliceIndex[bucket].end())) {
        LOG_WARN("Repeat put, key:" << key);
    }
    table->sliceIndex[bucket].emplace(key, sliceRef);
    return BIO_OK;
}

WCacheSliceRefPtr WCacheIndex::Aquire(uint16_t ptId, const Key &key)
{
    WCacheIndexTable *table = GetIndexTable(ptId);
    ChkTrue(table != nullptr, nullptr, "Invalid table, ptId:" << ptId << ".");
    auto bucket = Hash(key);
    ReadLocker<ReadWriteLock> lock(&table->sliceIndexLock[bucket]);
    auto sliceMeta = table->sliceIndex[bucket].find(key);
    if (UNLIKELY(sliceMeta == table->sliceIndex[bucket].end())) {
        return nullptr;
    }
    auto sliceRef = sliceMeta->second;
    if (LIKELY(sliceRef->Aquire())) {
        return sliceRef;
    } else {
        return nullptr;
    }
}

BResult WCacheIndex::FuzzyAquire(uint16_t ptId, const char *prefix, std::unordered_map<std::string, CacheObjStat> &objs)
{
    WCacheIndexTable *table = GetIndexTable(ptId);
    ChkTrue(table != nullptr, BIO_INVALID_PARAM, "Get wcache index table failed.");

    std::vector<WCacheSliceRefPtr> listSliceVec;
    for (uint32_t bucket = 0; bucket < HASH_BUCKET_NUM; bucket++) {
        ReadLocker<ReadWriteLock> lock(&table->sliceIndexLock[bucket]);
        for (auto &info : table->sliceIndex[bucket]) {
            if (UNLIKELY(objs.size() >= 1000U)) {
                return BIO_OK;
            }
            if (info.first.find(prefix) == 0) {
                auto sliceRef = info.second;
                if (sliceRef->Aquire()) {
                    auto sliceLen = static_cast<uint32_t>(sliceRef->GetSlice()->GetLength());
                    listSliceVec.push_back(sliceRef);
                    LOG_DEBUG("Wcache list success, key:" << info.first << ", slice length:" << sliceLen << ", time:" <<
                        time(nullptr) << ".");
                    objs.insert({ info.first, { sliceLen, time(nullptr) } });
                }
            }
        }
    }

    for (const auto &slice : listSliceVec) {
        slice->Release();
    }
    return BIO_OK;
}

BResult WCacheIndex::Delete(uint16_t ptId, const Key &key, WCacheSliceRefPtr sliceRef)
{
    WCacheIndexTable *table = GetIndexTable(ptId);
    ChkTrueNot(table != nullptr, BIO_ALLOC_FAIL);
    auto bucket = Hash(key);
    WriteLocker<ReadWriteLock> lock(&table->sliceIndexLock[bucket]);
    auto iter = table->sliceIndex[bucket].find(key);
    if (iter == table->sliceIndex[bucket].end()) {
        LOG_DEBUG("Delete write cache key:" << key << " have not exist.");
        return BIO_NOT_EXISTS;
    }
    if (iter->second != sliceRef) {
        LOG_DEBUG("Expired slice, key:" << key);
        return BIO_OK;
    }
    table->sliceIndex[bucket].erase(key);
    LOG_DEBUG("Delete key:" << key << " success.");
    return BIO_OK;
}

void WCacheIndex::ExpiredClear(uint16_t ptId)
{
    WCacheIndexTable *table = GetIndexTable(ptId);
    ChkTrueExNot(table != nullptr);

    for (uint32_t bucket = 0; bucket < HASH_BUCKET_NUM; bucket++) {
        WriteLocker<ReadWriteLock> lock(&table->sliceIndexLock[bucket]);
        for (auto it = table->sliceIndex[bucket].begin(); it != table->sliceIndex[bucket].end();) {
            if (it->second->GetSlice() == nullptr) {
                LOG_DEBUG("Expired clear, ptId:" << ptId << ", key:" << it->first);
                it = table->sliceIndex[bucket].erase(it);
            } else {
                ++it;
            }
        }
    }
    return;
}

void WCacheIndex::Exit()
{
    ReadLocker<ReadWriteLock> lock(&mTableLock);
    for (auto iter = mTable.begin(); iter != mTable.end(); iter++) {
        WCacheIndexTable *inTable = iter->second;
        delete inTable;
    }
    mTable.clear();
}

WCacheIndexTable *WCacheIndex::GetIndexTable(uint16_t ptId)
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
        WCacheIndexTable *inTable = nullptr;
        LVOS_TP_START(WCACHE_INDEX_TABLE_FAIL, &inTable, nullptr);
        inTable = new WCacheIndexTable;
        LVOS_TP_END;
        ChkTrue(inTable != nullptr, nullptr, "Alloc memory failed.");
        mTable.insert(std::make_pair(ptId, inTable));
        return mTable[ptId];
    }
}
}
}