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

#include "wcache_manager.h"
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>
#include "bio_log.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"
#include "bio_monotonic.h"
#include "flow_id_allocator.h"
#include "bio_crc_util.h"
#include "bio_config_instance.h"
#include "message.h"
#include "securec.h"
#include "bio_server.h"
#include "bdm_core.h"
#include "cache_flow.h"

namespace ock {
namespace bio {
constexpr uint16_t MEM_EVICT_THREAD_NUM = 4;
constexpr uint32_t MEM_EVICT_QUEUE_SIZE = 8192;
constexpr uint16_t DISK_EVICT_THREAD_NUM = 8;
constexpr uint32_t DISK_EVICT_QUEUE_SIZE = 8192;
constexpr uint16_t RETRY_EVICT_THREAD_NUM = 1;
constexpr uint32_t RETRY_EVICT_QUEUE_SIZE = 8192;
constexpr uint16_t NEGOTIATE_EVICT_THREAD_NUM = 4;
constexpr uint32_t NEGOTIATE_QUEUE_SIZE = 8192;
constexpr uint16_t DESTROY_EVICT_THREAD_NUM = 1;
constexpr uint32_t DESTROY_EVICT_QUEUE_SIZE = 8192;
constexpr uint16_t META_REPORT_THREAD_NUM = 1;
constexpr uint32_t META_REPORT_QUEUE_SIZE = 8192;
constexpr uint32_t DESTROY_EVICT_TIMEOUT = 60;
constexpr uint32_t DESTROY_EVICT_INTERAL = 15;
constexpr uint32_t FLUSH_RETRY_MAX_TIME = 1000000;
constexpr uint32_t FLUSH_INTERAL_TIME = 100000;
constexpr uint32_t BROKEN_INTERAL_TIME = 1000000;
constexpr uint32_t MAX_NEGOTIATE_DELAY = 1000000;

BResult WCacheManager::Init(const RCacheManagerPtr &rCacheManager)
{
    auto daemonConfig = BioConfig::Instance()->GetDaemonConfig();
    mEnableCrc = daemonConfig.enableCrc;
    mHasDiskCache = daemonConfig.hasDiskCache;
    mCacheIndex = MakeRef<WCacheIndex>();
    ChkTrue(mCacheIndex != nullptr, BIO_ALLOC_FAIL, "Make write cache index instance failed.");

    if (MemoryEvictExecutorInit() != BIO_OK) {
        return BIO_INNER_ERR;
    }

    if (mHasDiskCache && DiskEvictExecutorInit() != BIO_OK) {
        return BIO_INNER_ERR;
    }

    if (GcEvictExecutorInit() != BIO_OK) {
        return BIO_INNER_ERR;
    }

    if (RetryEvictExecutorInit() != BIO_OK) {
        return BIO_INNER_ERR;
    }

    if (DelayDestroyExecutorInit() != BIO_OK) {
        return BIO_INNER_ERR;
    }

    if (MetaReportExecutorInit() != BIO_OK) {
        return BIO_INNER_ERR;
    }

    mRCacheManager = rCacheManager;
    return BIO_OK;
}

BResult WCacheManager::MemoryEvictExecutorInit()
{
    mEvictService[WCACHE_MEMORY] = ExecutorService::Create(MEM_EVICT_THREAD_NUM, MEM_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mEvictService[WCACHE_MEMORY] == nullptr)) {
        LOG_ERROR("Failed to start execution service for mem evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    mEvictService[WCACHE_MEMORY]->SetThreadName("wcache-evict-mem");
    BResult result = mEvictService[WCACHE_MEMORY]->Start();
    ChkTrue(result, BIO_INNER_ERR, "Start memory evict service failed.");
    return BIO_OK;
}

BResult WCacheManager::DiskEvictExecutorInit()
{
    mEvictService[WCACHE_DISK] = ExecutorService::Create(DISK_EVICT_THREAD_NUM, DISK_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mEvictService[WCACHE_DISK] == nullptr)) {
        LOG_ERROR("Failed to start execution service for disk evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    mEvictService[WCACHE_DISK]->SetThreadName("wcache-evict-disk");
    auto result = mEvictService[WCACHE_DISK]->Start();
    ChkTrue(result, BIO_INNER_ERR, "Start disk evict service failed.");
    return BIO_OK;
}

BResult WCacheManager::GcEvictExecutorInit()
{
    mGcEvictService = ExecutorService::Create(DISK_EVICT_THREAD_NUM, DISK_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mGcEvictService == nullptr)) {
        LOG_ERROR("Failed to start execution service for gc evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }
    mGcEvictService->SetThreadName("wcache-evict-gc");
    auto result = mGcEvictService->Start();
    ChkTrue(result, BIO_INNER_ERR, "Start gc evict service failed.");
    return BIO_OK;
}

BResult WCacheManager::RetryEvictExecutorInit()
{
    mRetryEvictService = ExecutorService::Create(RETRY_EVICT_THREAD_NUM, RETRY_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mRetryEvictService == nullptr)) {
        LOG_ERROR("Failed to start execution service for retry evict, probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    mRetryEvictService->SetThreadName("wcache-retry-evict");
    auto result = mRetryEvictService->Start();
    ChkTrue(result, BIO_INNER_ERR, "Start gc evict service failed.");

    result = mRetryEvictService->Execute([this]() { RetryEvictThread(); });
    ChkTrue(result, BIO_INNER_ERR, "Execute gc evict service failed.");
    return BIO_OK;
}

BResult WCacheManager::DelayDestroyExecutorInit()
{
    mDestroyEvictService = ExecutorService::Create(DESTROY_EVICT_THREAD_NUM, DESTROY_EVICT_QUEUE_SIZE);
    if (UNLIKELY(mDestroyEvictService == nullptr)) {
        LOG_ERROR("Failed to start execution service for delay destroy, probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    mDestroyEvictService->SetThreadName("wcache-delay-destroy");
    auto result = mDestroyEvictService->Start();
    ChkTrue(result, BIO_INNER_ERR, "Start delay destroy service failed.");
    return BIO_OK;
}

BResult WCacheManager::MetaReportExecutorInit()
{
    mMetaReportService = ExecutorService::Create(META_REPORT_THREAD_NUM, META_REPORT_QUEUE_SIZE);
    if (UNLIKELY(mMetaReportService == nullptr)) {
        LOG_ERROR("Failed to start execution service for meta report, probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    mMetaReportService->SetThreadName("wcache-meta-report");
    auto result = mMetaReportService->Start();
    ChkTrue(result, BIO_INNER_ERR, "Start meta report service failed.");
    return BIO_OK;
}

void WCacheManager::Exit()
{
    mRunning.store(false);
    mRetryEvictService->Stop();
    mEvictService[WCACHE_MEMORY]->Stop();
    if (mEvictService[WCACHE_DISK] != nullptr) {
        mEvictService[WCACHE_DISK]->Stop();
    }

    mCacheIndex->Exit();
    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        for (auto iter = mWCacheManager.begin(); iter != mWCacheManager.end(); iter++) {
            iter->second->Exit();
        }
        mWCacheManager.clear();
    }

    FlushMetaEvents();
    if (mMetaReportService != nullptr) {
        mMetaReportService->Stop();
    }
    FlushMetaEvents();
}

BResult WCacheManager::AllocateFlowId(uint16_t ptId, uint64_t ptv, uint64_t &flowId)
{
    BIO_TRACE_START(WCACHE_TRACE_ALLOC_ID);
    auto flowIdAllocator = FlowIdAllocator::Instance();
    if (UNLIKELY(flowIdAllocator == nullptr)) {
        LOG_ERROR("Make flow id allocator instance failed.");
        return BIO_ALLOC_FAIL;
    }
    uint64_t flowPrefix = CacheFlowIdManager::GenerateCacheFlowIdPrefix(ptId, ptv, WRITE_CACHE, 0);
    flowId = flowIdAllocator->GenerateFlowId(flowPrefix);
    BIO_TRACE_END(WCACHE_TRACE_ALLOC_ID, 0);
    return BIO_OK;
}

BResult WCacheManager::CreateWCache(uint64_t procId, uint64_t flowId, uint16_t ptId, uint64_t ptv,
    uint16_t diskId, bool isDegrade, bool isRecover)
{
    WCachePtr wcache = nullptr;
    BIO_TP_START(WCACHE_ALLOC_FAIL, &wcache, nullptr);
    wcache = MakeRef<WCache>(procId, flowId, ptId, ptv, diskId, isDegrade);
    BIO_TP_END;
    ChkTrue(wcache != nullptr, BIO_ALLOC_FAIL, "Make wcache instance failed.");

    WCache::RecordMetaDeleteEventCallback recordMetaDeleteEventCallback = [this](uint16_t ptId, const Key &key,
        WCacheSliceRefPtr sliceRef, const UbsIoMetaEventBatchPtr &batch) -> BResult {
        mCacheIndex->Delete(ptId, key, sliceRef);
        AppendMetaEvent(UBSIO_META_DELETE, key, batch);
        return BIO_OK;
    };

    WCache::SubmitMetaEventBatchCallback submitMetaEventBatchCallback = [this](
        const UbsIoMetaEventBatchPtr &batch) -> void {
        SubmitMetaEventBatch(batch);
    };

    WCache::RetryCallback retryCallback = [this](uint64_t flowId, WCacheTierType cacheTier) -> void {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        mRetryManager[cacheTier].push_back(flowId);
    };

    wcache->RegOp(mGetLocDiskStatus, mLocRole, mEvictOffset, recordMetaDeleteEventCallback, retryCallback,
        submitMetaEventBatchCallback);
    auto ret = wcache->Init(mEvictService, mRCacheManager, isRecover);
    ChkTrue(ret == BIO_OK, ret, "Failed to init WCache, flowId:" << flowId);

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        mWCacheManager.emplace(flowId, wcache);
    }

    LOG_INFO("Create cache success, procId:" << procId << ", flowId:" << flowId << ", ptId:" <<
        ptId << ", ptv:" << ptv << ", isDegrade:" << isDegrade);
    return BIO_OK;
}

BResult WCacheManager::DestroyWCache(uint64_t procId, uint64_t flowId, uint16_t ptId, uint64_t ptv)
{
    auto wcache = GetWCacheForCleanup(flowId);
    if (wcache != nullptr && !wcache->IsWritable()) {
        if (wcache->IsIoFinish() && wcache->IsEmptyEvict(WCACHE_MEMORY) &&
            wcache->IsEmptyEvict(WCACHE_DISK)) {
            uint64_t evictTime = Monotonic::TimeSec();
            {
                WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
                mDestroyManager.emplace(flowId, evictTime);
            }
            bool isSucceed = mDestroyEvictService->Execute([this]() { DestroyEvictThread(); });
            LOG_INFO("Schedule read-only wcache destroy, flowId:" << flowId << ", ptId:" << ptId <<
                ", ptv:" << ptv << ", execute:" << isSucceed << ".");
            return isSucceed ? BIO_OK : BIO_ERR;
        }
        LOG_INFO("Keep read-only wcache, flowId:" << flowId << ", ptId:" << ptId << ", ptv:" << ptv << ".");
        return BIO_OK;
    }
    LOG_INFO("Handle cache broken:" << procId << ", flowId:" << flowId);
    bool isSucceed = true;
    BIO_TP_START(DESTROY_WCACHE_FAIL, &isSucceed, false);
    isSucceed = mGcEvictService->Execute([this, procId, flowId]() { HandleCacheBrokenHdl(procId, flowId); });
    BIO_TP_END;
    return (isSucceed) ? BIO_OK : BIO_ERR;
}

uint32_t WCacheManager::MarkPtFlowsReadOnly(uint16_t ptId, uint64_t ptv, uint16_t diskId)
{
    std::vector<uint64_t> flowIds;
    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        for (const auto &flowEntry : mWCacheManager) {
            const WCachePtr &wcache = flowEntry.second;
            if (wcache->GetPtId() != ptId || wcache->GetPtv() != ptv || wcache->GetDiskId() != diskId ||
                !wcache->IsWritable()) {
                continue;
            }
            wcache->MarkReadOnly();
            mDestroyManager.erase(flowEntry.first);
            flowIds.push_back(flowEntry.first);
        }
    }
    if (!flowIds.empty()) {
        WriteLocker<ReadWriteLock> lock(&mReuseFlowsLock);
        auto reuseIter = mReuseFlows.find(ptId);
        if (reuseIter != mReuseFlows.end()) {
            for (uint64_t flowId : flowIds) {
                reuseIter->second.erase(flowId);
            }
            if (reuseIter->second.empty()) {
                mReuseFlows.erase(reuseIter);
            }
        }
    }
    return static_cast<uint32_t>(flowIds.size());
}

BResult WCacheManager::DeleteWCache(uint64_t flowId)
{
    mWCacheManagerLock.LockWrite();
    auto iter = mWCacheManager.find(flowId);
    if (iter == mWCacheManager.end()) {
        mWCacheManagerLock.UnLock();
        return BIO_OK;
    }

    WCachePtr wcache = iter->second;
    if (!wcache->IsWritable()) {
        // A read-only flow is retained until every admitted write and every
        // queued eviction has finished. GetRef() == 2 means only the manager
        // map and this local reference remain, so no deferred SetSlice callback
        // or in-flight eviction is still using the flow.
        if (wcache->IsIoFinish() && wcache->IsEmptyEvict(WCACHE_MEMORY) &&
            wcache->IsEmptyEvict(WCACHE_DISK) && wcache->GetRef() == 2) {
            LOG_INFO("Delete drained read-only wcache, flowId:" << flowId << ", ptId:" <<
                wcache->GetPtId() << ", ptv:" << wcache->GetPtv() << ".");
        } else {
            mWCacheManagerLock.UnLock();
            return BIO_OK;
        }
    }
    if (wcache->IsStandaloneFault() || (mHasDiskCache && BioServer::Instance()->IsStandaloneMode() &&
        BdmGetDiskStatus(wcache->GetDiskId()) != BDM_DISK_STATE_NORMAL)) {
        mWCacheManagerLock.UnLock();
        return BIO_INNER_RETRY;
    }
    wcache->Destroy();
    mWCacheManager.erase(iter);
    mWCacheManagerLock.UnLock();
    LOG_INFO("Delete cache, procId:" << wcache->GetProcId() << ", flowId:" << wcache->GetFlowId() << ", ptId:" <<
        wcache->GetPtId() << ", ptv:" << wcache->GetPtv());
    return BIO_OK;
}

BResult WCacheManager::RecoverCache(FlowPtr metaFlow)
{
    uint64_t flowId = CacheFlowIdManager::GenOutFlowId(metaFlow->GetFlowId());
    uint16_t ptId = CacheFlowIdManager::GetPtId(flowId);
    uint32_t diskId = metaFlow->GetMediaId();

    LOG_INFO("Recover wcache, ptId:" << ptId << ", flowId:" << flowId);

    BResult ret = BIO_INNER_ERR;
    BIO_TP_START(RECOVER_CACHE_FLOWID_FAIL, &flowId, NO_1536);
    ret = CreateWCache(0, flowId, ptId, 0, static_cast<uint16_t>(diskId), false, true);
    BIO_TP_END;
    ChkTrue(ret == BIO_OK, ret, "Failed to create wcache, ret:" << ret << ", flowId:" << flowId);

    // 2. Get write flow
    auto wcache = GetWCache(flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("Failed to get wcache, flowId:" << flowId);
        return BIO_NOT_EXISTS;
    }

    auto metaEventBatch = std::make_shared<UbsIoMetaEventBatch>();
    ChkTrueNot(metaEventBatch != nullptr, BIO_ALLOC_FAIL);
    WCache::RecoverCallback recoverCallback = [this, metaEventBatch](uint16_t ptId, const Key &key,
        const WCacheSliceRefPtr &sliceRef) -> BResult {
        BIO_TRACE_START(WCACHE_TRACE_RECOVER);
        LOG_TRACE("Recover key:" << key << ", pt:" << ptId << ", flowId:" << sliceRef->GetSlice()->GetFlowId() <<
            ", flowOffset:" << sliceRef->GetSlice()->GetOffsetInFlow() << ", length:" <<
            sliceRef->GetSlice()->GetLength());
        auto ret = mCacheIndex->Insert(ptId, key, sliceRef);
        BIO_TRACE_END(WCACHE_TRACE_RECOVER, ret);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Insert slice to index failed, ret:" << ret << ", key:" << key << ".");
            return ret;
        }
        AppendMetaEvent(UBSIO_META_RECOVER, key, metaEventBatch);
        return BIO_OK;
    };

    ret = wcache->Recover(recoverCallback);
    if (ret != BIO_OK) {
        LOG_ERROR("Recover fail:" << ret << ", flowId:" << flowId);
        return ret;
    }
    SubmitMetaEventBatch(metaEventBatch);
    FlushMetaEvents();

    return BIO_OK;
}

BResult WCacheManager::ServiceUngradeFlush()
{
    std::list<WCachePtr> flushList;

    ScanUpgradeCache(flushList);

    BIO_TP_START(NO_PROCESS_UPGRADE_FLUSH, 0);
    for (const auto &flow : flushList) {
        flow->Flush(flow);
    }

    if (!flushList.empty()) {
        return BIO_INNER_RETRY;
    }
    BIO_TP_END
    auto ret = ClearUpgradeCache();
    ChkTrueNot(ret == BIO_OK, ret);
    return ret;
}

void WCacheManager::ScanUpgradeCache(std::list<WCachePtr> &list)
{
    WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    for (const auto &flowIt : mWCacheManager) {
        if (flowIt.second->GetDegradeState() || !flowIt.second->IsWritable()) {
            continue;
        }
        flowIt.second->SetState(false);
        if (flowIt.second->IsEmptyEvict(WCACHE_MEMORY) &&
            flowIt.second->IsEmptyEvict(WCACHE_DISK)) {
            continue;
        }
        uint16_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
        LOG_INFO("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() << ", flowId:" << flowIt.first <<
            ", Mem:" << flowIt.second->GetCapacity(WCACHE_MEMORY) << ", Disk:" <<
            flowIt.second->GetCapacity(WCACHE_DISK));
        list.emplace_back(flowIt.second);
    }

    return;
}

BResult WCacheManager::ClearUpgradeCache()
{
    bool result = false;
    uint64_t evictTime = Monotonic::TimeSec() + DESTROY_EVICT_TIMEOUT;

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        for (const auto &flowIt : mWCacheManager) {
            if (flowIt.second->GetDegradeState() || !flowIt.second->IsWritable()) {
                continue;
            }
            uint16_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
            LOG_INFO("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() << ", flowId:" << flowIt.first <<
                ", Vir Mem:" << flowIt.second->GetVirCapacity(WCACHE_MEMORY) << ", Vir Disk:" <<
                flowIt.second->GetVirCapacity(WCACHE_DISK));
            mDestroyManager.emplace(flowIt.first, evictTime);
        }
    }

    result = mDestroyEvictService->Execute([this]() { DestroyEvictThread(); });
    ChkTrueNot(result, BIO_INNER_ERR);
    return BIO_OK;
}

BResult WCacheManager::GetWCacheSlice(const SliceKey &sliceKey, WCacheSlicePtr &slice)
{
    ChkTrueNot(sliceKey.Validate(), BIO_INVALID_PARAM);

    auto wcache = GetWCache(sliceKey.flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("failed to get flow by id:" << sliceKey.flowId);
        return BIO_INNER_RETRY;
    }
    if (UNLIKELY(!wcache->IsWritable())) {
        return BIO_INNER_RETRY;
    }

    BIO_TRACE_START(WCACHE_TRACE_GET_SLICE);
    auto ret = wcache->GetWCacheSlice(sliceKey, slice);
    BIO_TRACE_END(WCACHE_TRACE_GET_SLICE, ret);
    return ret;
}

void WCacheManager::SetDegradeState(const WCacheSlicePtr &slice, bool flag)
{
    if (slice == nullptr) {
        LOG_ERROR("Slice is nullptr.");
        return;
    }
    auto wcache = GetWCache(slice->GetFlowId());
    if (wcache == nullptr) {
        LOG_ERROR("Failed to get wcache, flowId:" << slice->GetFlowId() << ".");
        return;
    }
    wcache->SetDegradeState(flag);
}

BResult WCacheManager::Put(const Key &key, const WCacheSlicePtr &slice, const SliceReader &sliceReader, CacheAttr &attr,
    bool isDegrade)
{
    ChkTrue(key != nullptr, BIO_INVALID_PARAM, "Key is nullptr.");
    ChkTrue(strlen(key) < NO_256, BIO_INVALID_PARAM, "Key length more 256.");
    ChkTrue(slice != nullptr, BIO_INVALID_PARAM, "Slice is nullptr.");
    ChkTrue(sliceReader != nullptr, BIO_INVALID_PARAM, "Slice reader is nullptr.");
    // 1. Get write cache flow instance.
    BIO_TRACE_START(WCACHE_TRACE_PUT_GET_WCACHE);
    auto wcache = AcquireWCacheForPut(slice->GetFlowId());
    BIO_TRACE_END(WCACHE_TRACE_PUT_GET_WCACHE, (wcache == nullptr) ? BIO_INNER_RETRY : BIO_OK);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("Failed to get write cache flow, flowId:" << slice->GetFlowId() << ", key:" << key << ".");
        return BIO_INNER_RETRY;
    }

    // 2. Get flow instance state, and check whether the status is consistent.
    bool wcacheDegarde = wcache->GetDegradeState();
    if (UNLIKELY(wcacheDegarde != isDegrade)) {
        LOG_WARN("Check degrade fail, flowId:" << slice->GetFlowId() << ", inner:" << wcacheDegarde << ", outer:" <<
            isDegrade << ", key:" << key << ".");
        wcache->DecFlyIo();
        return BIO_INNER_RETRY;
    }

    // 3. write slice to flow instance.
    BResult ret = BIO_ERR;
    BIO_TP_START(NO_PROCESS_WCACHE_PUT, 0);
    WCacheSliceRefPtr sliceRef = nullptr;
    BIO_TRACE_START(WCACHE_TRACE_PUT_WRITE_FLOW);
    BIO_TP_START(WCACHE_PUT_FAIL, &ret, BIO_ERR);
    ret = wcache->Put(key, slice, sliceReader, sliceRef, attr);
    BIO_TP_END;
    BIO_TRACE_END(WCACHE_TRACE_PUT_WRITE_FLOW, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        wcache->DecFlyIo();
        LOG_ERROR("Put slice to write cache failed, ret:" << ret << ", key:" << key << ".");
        return ret;
    }
    if (UNLIKELY(wcacheDegarde)) {
        wcache->DecFlyIo();
        return BIO_OK;
    }

    // 4. Insert slice reference to write cache index manager.
    BIO_TRACE_START(WCACHE_TRACE_PUT_INSERT_INDEX);
    ret = mCacheIndex->Insert(CacheFlowIdManager::GetPtId(slice->GetFlowId()), key, sliceRef);
    BIO_TRACE_END(WCACHE_TRACE_PUT_INSERT_INDEX, ret);
    BIO_TP_END;
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Insert slice reference to write cache index manager failed, ret:" << ret << ", key:" << key << ".");
    }
    wcache->DecFlyIo();
    return ret;
}

BResult WCacheManager::ParseKeyAddr(const Key &key, uint16_t ptId, BatchKeyAddrInfo *info)
{
    ChkTrue(key != nullptr, BIO_INVALID_PARAM, "Key is nullptr.");
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (UNLIKELY(sliceRef == nullptr)) {
        return BIO_NOT_EXISTS;
    }
    auto &addrs = sliceRef->GetSlice()->GetAddrs();
    ChkTrue(sliceRef->GetSlice()->GetFlowType() == FLOW_DISK, BIO_INNER_RETRY, "key not in disk");
    ChkTrue(addrs.size() <= NO_2, BIO_INVALID_PARAM, "Key addr size is more 2.");

    BResult ret = BIO_OK;
    info->count = 0;
    for (uint8_t i = 0; i < addrs.size(); i++) {
        uint64_t offset = 0;
        ret = BdmParseChunkId(addrs[i].chunkId, &offset, info->path);
        if (UNLIKELY(ret != BIO_OK)) {
            sliceRef->Release();
            LOG_ERROR("Parse key:" << key << ", addr:" << addrs[i].chunkId << " failed, ret:" << ret);
            return ret;
        }
        info->offset[i] = offset + addrs[i].chunkOffset;
        info->length[i] = addrs[i].chunkLen;
        info->count++;
    }
    sliceRef->Release();
    return ret;
}

BResult WCacheManager::Get(const Key &key, uint64_t offset, const RCacheSlicePtr &slice, const SliceWriter &sliceWriter,
    uint64_t &realLen)
{
    ChkTrue(key != nullptr, BIO_INVALID_PARAM, "Key is nullptr.");
    ChkTrue(slice != nullptr, BIO_INVALID_PARAM, "Slice is nullptr.");
    ChkTrue(sliceWriter != nullptr, BIO_INVALID_PARAM, "Slice writer is nullptr.");

    uint16_t ptId = slice->GetPtId();
    BIO_TRACE_START(WCACHE_TRACE_GET_QUERY_INDEX);
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    BIO_TRACE_END(WCACHE_TRACE_GET_QUERY_INDEX, ((sliceRef == nullptr) ? BIO_NOT_EXISTS : BIO_OK));
    if (UNLIKELY(sliceRef == nullptr)) {
        return BIO_NOT_EXISTS;
    }
    BResult ret = BIO_OK;
    if (mEnableCrc) {
        WCacheSlicePtr originSlice = sliceRef->GetSlice();
        ret = originSlice->VerifyDataCrc(originSlice->GetDataCrc(), 0, originSlice->GetLength(), nullptr);
        if (ret != BIO_OK) {
            LOG_ERROR("Server wcache get verify the CRC fail, key:"<< key << ", ret:" << ret);
            sliceRef->Release();
            return ret;
        }
    }

    BIO_TRACE_START(WCACHE_TRACE_GET_READ_DATA);
    ret = Read(offset, sliceRef->GetSlice(), slice, sliceWriter, realLen);
    BIO_TRACE_END(WCACHE_TRACE_GET_READ_DATA, ret);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("WCache Read data failed, key :" << key << ", offset:" << offset << ", length:" <<
            sliceRef->GetSlice()->GetLength() << ".");
        sliceRef->Release();
        return ret;
    } else {
        if (mEnableCrc) {
            uint32_t readCrc;
            ret = sliceRef->GetSlice()->CalculateDataCrc(readCrc, offset, realLen);
            if (ret != BIO_OK) {
                LOG_ERROR("Server rcache get verify the CRC fail, key:"<< key <<", ret: " << ret);
                sliceRef->Release();
                return ret;
            }
            slice->SetDataCrc(readCrc);
        }
    }
    FlowType flowType = sliceRef->GetSlice()->GetFlowType();
    WCacheStatistic::Instance().StatisticalByType(flowType);
    sliceRef->Release();
    return ret;
}

BResult WCacheManager::GetBatch(const Key &key, uint64_t offset, const RCacheSlicePtr &slice,
    const WCacheBatchSliceWriter &sliceWriter, uint64_t &realLen)
{
    ChkTrue(key != nullptr, BIO_INVALID_PARAM, "Key is nullptr.");
    ChkTrue(slice != nullptr, BIO_INVALID_PARAM, "Slice is nullptr.");
    ChkTrue(sliceWriter != nullptr, BIO_INVALID_PARAM, "Slice writer is nullptr.");
    ChkTrue(!mEnableCrc, BIO_INNER_RETRY, "Batch WCache read does not support CRC.");

    uint16_t ptId = slice->GetPtId();
    BIO_TRACE_START(WCACHE_TRACE_GET_QUERY_INDEX);
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    BIO_TRACE_END(WCACHE_TRACE_GET_QUERY_INDEX, ((sliceRef == nullptr) ? BIO_NOT_EXISTS : BIO_OK));
    if (UNLIKELY(sliceRef == nullptr)) {
        return BIO_NOT_EXISTS;
    }

    WCacheSlicePtr srcSlice = sliceRef->GetSlice();
    if (UNLIKELY(srcSlice == nullptr || offset >= srcSlice->GetLength())) {
        LOG_ERROR("Failed to split batch WCache slice, offset:" << offset << ".");
        sliceRef->Release();
        return BIO_READ_EXCEED;
    }

    realLen = std::min<uint64_t>(srcSlice->GetLength() - offset, slice->GetLength());
    SlicePtr readSlice = srcSlice->Split(offset, realLen);
    if (UNLIKELY(readSlice == nullptr)) {
        LOG_ERROR("Failed to split batch WCache slice, offset:" << offset << ", length:" << realLen << ".");
        sliceRef->Release();
        return BIO_READ_EXCEED;
    }

    BIO_TRACE_START(WCACHE_TRACE_GET_READ_DATA);
    BResult ret = sliceWriter(readSlice.Get(), slice.Get(), sliceRef);
    BIO_TRACE_END(WCACHE_TRACE_GET_READ_DATA, ret);
    if (sliceRef != nullptr) {
        sliceRef->Release();
    }
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Batch WCache read failed, key:" << key << ", offset:" << offset << ", length:" << realLen <<
            ", ret:" << ret << ".");
        return ret;
    }

    FlowType flowType = readSlice->GetFlowType();
    WCacheStatistic::Instance().StatisticalByType(flowType);
    return BIO_OK;
}

BResult WCacheManager::Stat(uint16_t ptId, const Key &key, CacheObjStat &cacheObjStat)
{
    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (sliceRef != nullptr) {
        cacheObjStat.size = sliceRef->GetSlice()->GetLength();
        cacheObjStat.time = time(nullptr);
        sliceRef->Release();
        return BIO_OK;
    }
    return BIO_NOT_EXISTS;
}

bool WCacheManager::Exist(uint16_t ptId, const Key &key)
{
    return mCacheIndex->Exist(ptId, key);
}

BResult WCacheManager::List(char *prefix, uint16_t ptId, std::unordered_map<std::string, CacheObjStat> &objs)
{
    ChkTrueNot(prefix != nullptr, BIO_INVALID_PARAM);
    return mCacheIndex->FuzzyAquire(ptId, prefix, objs);
}

BResult WCacheManager::ScanDiskKeys(std::unordered_map<std::string, uint64_t> &items)
{
    return mCacheIndex->ScanDiskKeys(items);
}

BResult WCacheManager::Delete(uint16_t ptId, const Key &key)
{
    ChkTrue(key != nullptr, BIO_INVALID_PARAM, "Key is nullptr.");

    WCacheSliceRefPtr sliceRef = mCacheIndex->Aquire(ptId, key);
    if (UNLIKELY(sliceRef == nullptr)) {
        LOG_WARN("Write cache acquire slice failed, key:" << key << ", ptId:" << ptId << ".");
        return BIO_NOT_EXISTS;
    }
    if (!sliceRef->OpLock()) {
        LOG_WARN("Write cache oplock slice failed, key:" << key << ", ptId:" << ptId << ".");
        sliceRef->Release();
        return BIO_INNER_RETRY;
    }

    auto slice = sliceRef->GetSlice();
    uint64_t flowId = -1;
    BIO_TP_START(WCACHE_DELETE_FLOWID_ERR, &flowId, -1);
    flowId = CacheFlowIdManager::GenOutFlowId(slice->GetFlowId());
    BIO_TP_END;
    auto wcache = GetWCache(flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("Failed to get flow, flowId:" << flowId << ", key:" << key << ".");
        sliceRef->OpUnLock();
        sliceRef->Release();
        return BIO_INNER_RETRY;
    }

    auto ret = wcache->Delete(key, sliceRef);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Delete slice from flow failed, ret:" << ret << ", key:" << key << ".");
        sliceRef->OpUnLock();
        sliceRef->Release();
        return ret;
    }

    ret = mCacheIndex->Delete(ptId, key, sliceRef);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to delete. key:" << key << ", ret:" << ret);
        sliceRef->OpUnLock();
        sliceRef->Release();
        return ret;
    }

    sliceRef->SetState(SLICE_INVALID);
    sliceRef->OpUnLock();
    sliceRef->Release();
    return BIO_OK;
}

void WCacheManager::RegGetLocDiskStatus(GetLocDiskStatus getLocDiskStatus)
{
    LOG_INFO("Register get loc disk status func");
    mGetLocDiskStatus = getLocDiskStatus;
}

void WCacheManager::RegGetGlobEvictOffset(GetGlobEvictOffset evictOffset)
{
    LOG_INFO("Register get evict offset func");
    mEvictOffset = evictOffset;
}

void WCacheManager::RegCheckLocRole(CheckLocRole localRole)
{
    LOG_INFO("Register check loc role func");
    mLocRole = localRole;
}

void WCacheManager::RegUbsIoMetaEventCallback(UbsIoMetaEventCallback callback)
{
    LOG_INFO("Register UBS IO meta event callback");
    std::lock_guard<std::mutex> lock(mMetaCallbackLock);
    mMetaEventCallback = std::move(callback);
}

void WCacheManager::AppendMetaEvent(UbsIoMetaEventType type, const Key &key,
    const UbsIoMetaEventBatchPtr &batch)
{
    if (key == nullptr) {
        return;
    }
    if (batch != nullptr) {
        std::lock_guard<std::mutex> lock(batch->lock);
        if (!batch->closed) {
            batch->events.push_back({ type, std::string(key) });
            return;
        }
    }
    std::vector<UbsIoMetaEvent> events;
    events.push_back({ type, std::string(key) });
    AppendMetaEvents(std::move(events));
}

void WCacheManager::AppendMetaEvents(std::vector<UbsIoMetaEvent> &&events)
{
    if (events.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mMetaReportLock);
        mPendingMetaEvents.insert(mPendingMetaEvents.end(), std::make_move_iterator(events.begin()),
            std::make_move_iterator(events.end()));
    }
    ScheduleFlushMetaEvents();
}

void WCacheManager::ReportMetaEventsSync(std::vector<UbsIoMetaEvent> &&events)
{
    if (events.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mMetaCallbackLock);
    if (mMetaEventCallback != nullptr) {
        mMetaEventCallback(events);
    } else {
        LOG_DEBUG("Skip UBS IO meta event report, callback is not registered, event count:" << events.size());
    }
}

void WCacheManager::SubmitMetaEventBatch(const UbsIoMetaEventBatchPtr &batch)
{
    if (batch == nullptr) {
        return;
    }

    std::vector<UbsIoMetaEvent> events;
    {
        std::lock_guard<std::mutex> lock(batch->lock);
        batch->closed = true;
        events.swap(batch->events);
    }
    AppendMetaEvents(std::move(events));
}

BResult WCacheManager::GetEvictOffset(uint64_t flowId, uint64_t &flowOffset)
{
    auto wcache = GetWCache(flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_WARN("Failed to get flow by id:" << flowId);
        return BIO_NOT_EXISTS;
    }

    BIO_TRACE_START(WCACHE_TRACE_GET_EVICT_OFFSET);
    flowOffset = wcache->GetEvictOffset();
    BIO_TRACE_END(WCACHE_TRACE_GET_EVICT_OFFSET, BIO_OK);

    return BIO_OK;
}

BResult WCacheManager::Flush(uint16_t ptId, uint64_t ptv)
{
    LOG_INFO("WCache flush, ptId:" << ptId << ", ptv:" << ptv << ".");
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeUs();
    BResult ret;

    BIO_TP_START(NO_PROCESS_FLUSH, 0);
    BIO_TP_START(WCACHE_FLUSH_FAIL, &ret, BIO_INNER_RETRY);
    do {
        isRetry = false;
        ret = FlushImpl(ptId, ptv);
        if (ret != BIO_OK) {
            retryTime = Monotonic::TimeUs() - startTime;
            if (retryTime < FLUSH_RETRY_MAX_TIME) {
                isRetry = true;
                usleep(FLUSH_INTERAL_TIME);
            }
        }
    } while (isRetry);
    BIO_TP_END;
    if (ret != BIO_OK) {
        return ret;
    }
    BIO_TP_END;

    ret = ClearOldCache(ptId, ptv);
    ChkTrueNot(ret == BIO_OK, ret);
    return ret;
}

BResult WCacheManager::FlushImpl(uint16_t ptId, uint64_t ptv)
{
    std::list<WCachePtr> flushList;
    ScanOldCache(ptId, ptv, flushList);
    for (const auto &flow : flushList) {
        flow->Flush(flow);
    }
    return (!flushList.empty()) ? BIO_INNER_RETRY : BIO_OK;
}

BResult WCacheManager::ExpiredClear(uint16_t ptId, uint64_t ptv)
{
    LOG_INFO("WCache expired clear, ptId:" << ptId << ", ptv:" << ptv << ".");
    bool isRetry = false;
    uint64_t retryTime;
    uint64_t startTime = Monotonic::TimeUs();
    BResult ret = BIO_INNER_ERR;

    BIO_TP_START(NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR, 0);
    BIO_TP_START(WCACHE_EXPIRE_FAIL, &ret, BIO_INNER_RETRY);
    do {
        isRetry = false;
        ret = ExpiredClearImpl(ptId, ptv);
        if (ret != BIO_OK) {
            retryTime = Monotonic::TimeUs() - startTime;
            if (retryTime < FLUSH_RETRY_MAX_TIME) {
                isRetry = true;
                usleep(FLUSH_INTERAL_TIME);
            }
        }
    } while (isRetry);
    BIO_TP_END;
    if (ret != BIO_OK) {
        return ret;
    }

    ret = ClearOldCache(ptId, ptv);
    ChkTrue(ret == BIO_OK, ret, "Clear old cache failed, ret:" << ret << ".");
    BIO_TP_END;

    mCacheIndex->ExpiredClear(ptId);
    return ret;
}

void WCacheManager::CollectFaultedFlows(uint16_t failedDiskId,
    std::unordered_map<uint16_t, std::list<WCachePtr>> &faultedFlowsByPt,
    std::unordered_map<uint16_t, std::unordered_set<uint64_t>> &flowIdsByPt)
{
    WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    for (const auto &flowEntry : mWCacheManager) {
        const WCachePtr &wcache = flowEntry.second;
        // A faulted disk can still back historical flows whose PT was moved to
        // another disk by an earlier failover/add-disk operation. Those flows
        // are read-only but their index entries and chunk addresses still
        // point at the faulted disk, so they must be cleaned up here as well.
        if (wcache->GetDiskId() != failedDiskId) {
            continue;
        }
        wcache->SetStandaloneFault();
        wcache->SetState(false);
        uint16_t ptId = wcache->GetPtId();
        faultedFlowsByPt[ptId].push_back(wcache);
        flowIdsByPt[ptId].insert(wcache->GetFlowId());
    }
}

BResult WCacheManager::WaitFaultedFlowsIdle(uint16_t failedDiskId, const std::list<WCachePtr> &faultedFlows)
{
    for (const auto &wcache : faultedFlows) {
        uint64_t startTime = Monotonic::TimeUs();
        while (!wcache->IsIoFinish()) {
            if (Monotonic::TimeUs() - startTime >= FLUSH_RETRY_MAX_TIME) {
                LOG_WARN("Wait faulted flow idle failed, failedDiskId:" << failedDiskId << ", flowId:" <<
                    wcache->GetFlowId() << ".");
                return BIO_INNER_RETRY;
            }
            usleep(FLUSH_INTERAL_TIME);
        }
    }
    return BIO_OK;
}

void WCacheManager::ReportRemovedKeys(std::vector<std::string> &&removedKeys)
{
    if (removedKeys.empty()) {
        return;
    }
    std::vector<UbsIoMetaEvent> events;
    events.reserve(removedKeys.size());
    for (auto &key : removedKeys) {
        events.push_back({ UBSIO_META_DELETE, std::move(key) });
    }
    ReportMetaEventsSync(std::move(events));
}

BResult WCacheManager::CleanupFaultedDiskFlows(uint16_t failedDiskId)
{
    LOG_INFO("Cleanup faulted disk flows, failedDiskId:" << failedDiskId << ".");
    std::unordered_map<uint16_t, std::list<WCachePtr>> faultedFlowsByPt;
    std::unordered_map<uint16_t, std::unordered_set<uint64_t>> flowIdsByPt;
    CollectFaultedFlows(failedDiskId, faultedFlowsByPt, flowIdsByPt);

    size_t wcacheCount = 0;
    for (const auto &ptFlows : faultedFlowsByPt) {
        BResult ret = WaitFaultedFlowsIdle(failedDiskId, ptFlows.second);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
        wcacheCount += ptFlows.second.size();
    }

    // Erase index entries and report keys before clearing the memory tier.
    // WCacheIndex is the only place that keeps key->slice for both memory and
    // disk resident slices, and ForceClearMemoryTier below nulls the slice of
    // every memory resident entry, after which EraseFlowEntries would skip it.
    std::vector<std::string> removedKeys;
    for (const auto &ptFlows : faultedFlowsByPt) {
        auto flowIdsIter = flowIdsByPt.find(ptFlows.first);
        if (flowIdsIter == flowIdsByPt.end()) {
            continue;
        }
        mCacheIndex->EraseFlowEntries(ptFlows.first, flowIdsIter->second, removedKeys);
    }
    size_t removedKeyCount = removedKeys.size();
    ReportRemovedKeys(std::move(removedKeys));

    for (const auto &ptFlows : faultedFlowsByPt) {
        for (const auto &wcache : ptFlows.second) {
            uint64_t startTime = Monotonic::TimeUs();
            BResult clearRet = BIO_INNER_RETRY;
            bool needRetry = false;
            do {
                if (wcache->IsIoFinish()) {
                    clearRet = wcache->ForceClearMemoryTier();
                }
                needRetry = clearRet != BIO_OK && Monotonic::TimeUs() - startTime < FLUSH_RETRY_MAX_TIME;
                if (needRetry) {
                    usleep(FLUSH_INTERAL_TIME);
                }
            } while (needRetry);
            if (UNLIKELY(clearRet != BIO_OK)) {
                LOG_WARN("Force clear faulted flow memory tier failed, failedDiskId:" << failedDiskId <<
                    ", ptId:" << ptFlows.first << ", flowId:" << wcache->GetFlowId() << ", ret:" << clearRet << ".");
                return clearRet;
            }
        }
    }

    for (const auto &ptFlows : faultedFlowsByPt) {
        BResult ret = UnregisterFaultedFlows(ptFlows.first, ptFlows.second);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
    }
    LOG_INFO("Cleanup faulted disk flows success, failedDiskId:" << failedDiskId << ", wcacheCount:" <<
        wcacheCount << ", keyCount:" << removedKeyCount << ".");
    return BIO_OK;
}

BResult WCacheManager::UnregisterFaultedFlows(uint16_t ptId, const std::list<WCachePtr> &faultedFlows)
{
    std::vector<uint64_t> unregisteredFlowIds;
    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        for (const auto &wcache : faultedFlows) {
            uint64_t flowId = wcache->GetFlowId();
            auto iter = mWCacheManager.find(flowId);
            if (iter == mWCacheManager.end() || iter->second != wcache) {
                continue;
            }
            unregisteredFlowIds.push_back(flowId);
            mDestroyManager.erase(flowId);
            for (auto &retryFlows : mRetryManager) {
                retryFlows.erase(std::remove(retryFlows.begin(), retryFlows.end(), flowId), retryFlows.end());
            }
            mWCacheManager.erase(iter);
        }
    }
    {
        WriteLocker<ReadWriteLock> lock(&mReuseFlowsLock);
        auto iter = mReuseFlows.find(ptId);
        if (iter != mReuseFlows.end()) {
            for (uint64_t flowId : unregisteredFlowIds) {
                iter->second.erase(flowId);
            }
            if (iter->second.empty()) {
                mReuseFlows.erase(iter);
            }
        }
    }
    return BIO_OK;
}

BResult WCacheManager::ExpiredClearImpl(uint16_t ptId, uint64_t ptv)
{
    std::list<WCachePtr> expiredList;

    ScanOldCache(ptId, ptv, expiredList);

    for (const auto &flow : expiredList) {
        flow->ExpiredClear(flow);
    }

    return (expiredList.size() != 0) ? BIO_INNER_RETRY : BIO_OK;
}

void WCacheManager::ScanOldCache(uint16_t ptId, uint64_t ptv, std::list<WCachePtr> &list)
{
    WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    for (const auto &flowIt : mWCacheManager) {
        if (!flowIt.second->IsWritable()) {
            continue;
        }
        uint16_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
        if (ptId != flowPtId) {
            continue;
        }
        if (flowIt.second->GetPtv() >= ptv) {
            continue;
        }
        if (flowIt.second->IsEmptyEvict(WCACHE_MEMORY) &&
            flowIt.second->IsEmptyEvict(WCACHE_DISK)) {
            continue;
        }
        LOG_TRACE("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() << ", flowId:" << flowIt.first <<
            ", Mem:" << flowIt.second->GetCapacity(WCACHE_MEMORY) << ", Disk:" <<
            flowIt.second->GetCapacity(WCACHE_DISK));
        list.emplace_back(flowIt.second);
    }

    return;
}

BResult WCacheManager::ClearOldCache(uint16_t ptId, uint64_t ptv)
{
    bool result = false;
    BIO_TP_START(NO_PROCESS_CLEAR_OLD_CACHE, 0);
    uint64_t evictTime = Monotonic::TimeSec() + DESTROY_EVICT_TIMEOUT;
    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        for (const auto &flowIt : mWCacheManager) {
            if (!flowIt.second->IsWritable()) {
                continue;
            }
            uint16_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
            if (ptId != flowPtId) {
                continue;
            }
            if (flowIt.second->GetPtv() >= ptv) {
                continue;
            }
            LOG_INFO("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() << ", flowId:" << flowIt.first <<
                ", Vir Mem:" << flowIt.second->GetVirCapacity(WCACHE_MEMORY) << ", Vir Disk:" <<
                flowIt.second->GetVirCapacity(WCACHE_DISK));
            mDestroyManager.emplace(flowIt.first, evictTime);
        }
    }

    result = mDestroyEvictService->Execute([this]() { DestroyEvictThread(); });
    BIO_TP_END;
    ChkTrue(result, BIO_INNER_ERR, "Execute destroy evict service failed.");
    return BIO_OK;
}

BResult WCacheManager::HandleCacheBrokenHdl(uint64_t procId, uint64_t flowId)
{
    LOG_INFO("Destroy wcache begin, procId:" << procId << ", flowId:" << flowId << ".");
    auto wcache = GetWCache(flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_WARN("Failed to get wcache flow by id:" << flowId << ".");
        return BIO_NOT_EXISTS;
    }
    if (!wcache->IsWritable()) {
        return BIO_OK;
    }
    wcache->SetState(false);

    BResult ret = BIO_INNER_ERR;
    do {
        BIO_TP_START(HANDLE_CACHE_BROKE_OK, &ret, BIO_OK);
        ret = HandleCacheBrokenImpl(wcache);
        if (ret != BIO_OK) {
            usleep(BROKEN_INTERAL_TIME);
        }
        BIO_TP_END;
    } while (ret != BIO_OK);

    BIO_TP_START(NO_PROCESS_DESTROY_EVICT_THREAD, 0);
    uint64_t evictTime = Monotonic::TimeSec() + DESTROY_EVICT_TIMEOUT;
    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        mDestroyManager.emplace(flowId, evictTime);
    }

    bool result = mDestroyEvictService->Execute([this]() { DestroyEvictThread(); });
    ChkTrue(result, BIO_INNER_ERR, "Execute destroy evict service failed.");
    BIO_TP_END;

    return BIO_OK;
}

BResult WCacheManager::HandleCacheBrokenImpl(WCachePtr wcache)
{
    if (wcache->IsStandaloneFault() || (mHasDiskCache && BioServer::Instance()->IsStandaloneMode() &&
        BdmGetDiskStatus(wcache->GetDiskId()) != BDM_DISK_STATE_NORMAL)) {
        return BIO_OK;
    }
    BIO_TP_START(NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT, 0);
    if (wcache->IsEmptyEvict(WCACHE_MEMORY) &&
        wcache->IsEmptyEvict(WCACHE_DISK)) {
        return BIO_OK;
    }
    BIO_TP_END;

    bool isMaster = false;
    uint16_t flowPtId = 0;
    BIO_TP_START(WCACHE_HANDLE_BROCK_FLOWID_FAIL, &flowPtId, NO_1024);
    flowPtId = wcache->GetPtId();
    BIO_TP_END;
    auto ret = mLocRole(static_cast<uint16_t>(flowPtId), isMaster);
    if (ret != BIO_OK) {
        LOG_ERROR("Get local role fail:" << ret << ", ptId:" << flowPtId << ", flowId:" <<
            wcache->GetFlowId());
        return BIO_ERR;
    }
    BIO_TP_START(WCACHE_HANDLE_BROCK_FLUSH, &isMaster, true);
    BIO_TP_END;
    BIO_TP_START(WCACHE_HANDLE_BROCK_EXPIRED_CLEAR, &isMaster, false);
    BIO_TP_END;
    if (isMaster) {
        wcache->Flush(wcache);
    } else {
        wcache->ProcAndCacheBrokenExpiredClear();
    }

    return BIO_INNER_RETRY;
}

BResult WCacheManager::HandleProcBroken(uint64_t procId)
{
    LOG_INFO("Handle proc broken:" << procId);
    bool isSucceed = false;
    BIO_TP_START(HANDLE_PROC_BROKEN_FAIL, &isSucceed, false);
    isSucceed = mGcEvictService->Execute([this, procId]() { HandleProcBrokenHdl(procId); });
    BIO_TP_END;
    return (isSucceed) ? BIO_OK : BIO_ERR;
}

BResult WCacheManager::HandleProcBrokenHdl(uint64_t procId)
{
    startedProcLock.DoLock();
    auto task = startedProc.find(procId);
    if (task != startedProc.end()) {
        startedProcLock.UnLock();
        return BIO_OK;
    }
    startedProc.insert(procId);
    startedProcLock.UnLock();

    BResult ret = BIO_INNER_ERR;
    std::list<WCache*> oldList;
    ScanProcCache(procId, oldList);
    do {
        BIO_TP_START(HANDLE_PROC_BROKE_OK, &ret, BIO_OK);
        ret = HandleProcBrokenImpl(procId, oldList);
        BIO_TP_END;
        if (ret != BIO_OK) {
            usleep(BROKEN_INTERAL_TIME);
        }
    } while (ret != BIO_OK);

    startedProcLock.DoLock();
    startedProc.erase(procId);
    startedProcLock.UnLock();
    return ret;
}

void WCacheManager::InitCallbackCtx(ProcBrokenCallbackCtx &cbCtx, uint32_t quota)
{
    cbCtx.result = BIO_OK;
    sem_init(&cbCtx.sem, 0, 0);
    cbCtx.quota = quota;
    cbCtx.needDestroy = false;
}

BResult WCacheManager::SendProcBrokenSyncRequest(WCachePtr flow, CmPtInfo ptEntry, uint32_t localNid,
    bool *slaveResult, bool &needDestroy)
{
    std::vector<uint32_t> slaveNode;
    for (uint32_t idx = 0; idx < ptEntry.copys.size(); idx++) {
        if (ptEntry.copys[idx].state != CM_COPY_RUNNING && ptEntry.copys[idx].state != CM_COPY_RECOVERY) {
            continue;
        }
        if (ptEntry.copys[idx].nodeId == localNid) {
            continue;
        }
        slaveNode.emplace_back(ptEntry.copys[idx].nodeId);
    }
    ProcBrokenCallbackCtx cbCtx;
    InitCallbackCtx(cbCtx, slaveNode.size());
    if (slaveNode.empty()) {
        sem_destroy(&cbCtx.sem);
        needDestroy = false;
        return BIO_OK;
    }
    auto cbFunc = [&slaveResult](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto cbCtx = static_cast<ProcBrokenCallbackCtx *>(ctx);
        if (UNLIKELY(result != BIO_OK)) {
            cbCtx->result = result;
        }
        if (UNLIKELY(resp == nullptr)) {
            if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
                sem_post(&cbCtx->sem);
            }
            return;
        }
        auto procBrokenResp = static_cast<ProcFlowSyncResponse *>(resp);
        slaveResult[procBrokenResp->nodeId] = procBrokenResp->needDestroy;
        if (procBrokenResp->needDestroy) {
            cbCtx->needDestroy = true;
        }
        if (__sync_sub_and_fetch(&cbCtx->quota, 1) == 0) {
            sem_post(&cbCtx->sem);
        }
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    ProcFlowSyncRequest req;
    req.comm = {MESSAGE_MAGIC, ptEntry.ptId, ptEntry.version, localNid, getpid()};
    req.needDestroy = false;
    req.index = flow->GetIndex();
    req.offset = flow->GetOffset();
    req.flowId = flow->GetFlowId();
    NetEnginePtr rpcEngine = BioServer::Instance()->GetNetEngine();
    for (auto nodeId : slaveNode) {
        rpcEngine->AsyncCall(nodeId, BIO_OP_SERVER_PROCBROCKEN_SYNC_FLOW, req, callback);
    }
    sem_wait(&cbCtx.sem);
    sem_destroy(&cbCtx.sem);

    needDestroy = cbCtx.needDestroy;
    return cbCtx.result;
}

void WCacheManager::HandleProcBrokenDestroyFlow(WCachePtr flow, uint32_t localNid, bool *slaveResult)
{
    bool destroyLocal = false;
    uint16_t ptId = flow->GetPtId();
    uint64_t ptV = flow->GetPtv();
    uint64_t flowId = flow->GetFlowId();
    uint64_t procId = flow->GetProcId();
    flow->SetState(true);
    NetEnginePtr rpcEngine = BioServer::Instance()->GetNetEngine();
    DestroyFlowRequest req = { { MESSAGE_MAGIC, ptId, ptV, localNid, getpid() }, flowId };
    DestroyFlowResponse rsp;
    BResult ret = BIO_OK;

    do {
        BIO_TP_START(WCACHE_DESTROY_LOCAL_FALSE, &destroyLocal, true);
        BIO_TP_END;
        if (!destroyLocal) {
            ret = DestroyWCache(procId, flowId, ptId, ptV);
            if (ret == BIO_OK) {
                destroyLocal = true;
            } else {
                continue;
            }
        }
        for (uint32_t i = 0; i < CLUSTER_SIZEMAX; i++) {
            if (!slaveResult[i]) {
                ret = rpcEngine->SyncCall<DestroyFlowRequest, DestroyFlowResponse>(static_cast<BioNodeId>(i),
                    BIO_OP_SDK_DESTROY_FLOW, req, rsp);
            }
            if (ret == BIO_OK) {
                slaveResult[i] = true;
            } else {
                break;
            }
        }
        BIO_TP_START(BROCK_DESTROY_FLOW_OK, &ret, BIO_OK);
        BIO_TP_END;
    } while (ret != BIO_OK);
}

BResult WCacheManager::MasterProcBrokenSyncFlow(WCachePtr flow, CmPtInfo ptEntry, uint32_t localNid)
{
    bool slaveResult[CLUSTER_SIZEMAX];
    std::fill(std::begin(slaveResult), std::end(slaveResult), true);
    bool needDestroy = false;
    auto ret = SendProcBrokenSyncRequest(flow, ptEntry, localNid, slaveResult, needDestroy);
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Send proc broken sync request fail, flow:"<< flow->GetFlowId() <<
                                                             ", ptId:" << flow->GetPtId() << ", ret:" << ret);
        return ret;
    }
    if (!needDestroy) {
        WriteLocker<ReadWriteLock> lock(&mReuseFlowsLock);
        auto oldFlow = mReuseFlows.find(flow->GetPtId());
        if (oldFlow == mReuseFlows.end()) {
            std::unordered_set<uint64_t> flows;
            flows.insert(flow->GetFlowId());
            mReuseFlows.insert(std::pair<uint16_t, std::unordered_set<uint64_t>>(flow->GetPtId(), flows));
        } else {
            oldFlow->second.insert(flow->GetFlowId());
        }
        LOG_DEBUG("Inser reuse flow collection, flow:" << flow->GetFlowId() << ", pt:" << flow->GetPtId() << ".");
        flow->SetState(true);
    } else {
        bool isSucceed = mGcEvictService->Execute(
            [this, &slaveResult, flow, localNid]() { HandleProcBrokenDestroyFlow(flow, localNid, slaveResult); });
        ret = (isSucceed) ? BIO_OK : BIO_ERR;
    }

    return ret;
}

BResult WCacheManager::HandleProcBrokenImpl(uint64_t procId, std::list<WCache*> &oldList)
{
    BResult ret = BIO_INNER_ERR;
    bool isMaster = false;
    auto it = oldList.begin();
    while (it != oldList.end()) {
        auto flow = *it;
        while (!flow->IsIoFinish()) {
            usleep(NO_20 * NO_10000);
        }
        CmPtInfo ptEntry;
        ret = Cm::Instance()->GetPtInfo(flow->GetPtId(), ptEntry);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Get pt info fail,flow:" << flow->GetFlowId() << ", ptId:" << flow->GetPtId() << ".");
            it++;
            continue;
        }
        uint16_t localNid = Cm::Instance()->GetCmLocalNodeId().VNodeId();
        BIO_TP_START(WCACHE_HANDLE_PROC_BROCK_FLUSH, &isMaster, true);
        BIO_TP_END;
        BIO_TP_START(WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR, &isMaster, false);
        BIO_TP_END;

        ret = MasterProcBrokenSyncFlow(flow, ptEntry, localNid);
        if (ret != BIO_OK && flow->GetState()) {
            flow->SetState(false);
            LOG_ERROR("proc broken sync flow fail,flow:" << flow->GetFlowId() << ", ptId:" <<
                                                         flow->GetPtId() << ", ret:" << ret <<".");
            it++;
            continue;
        }
        *it = nullptr;
        it = oldList.erase(it);
    }

    return (oldList.empty()) ? BIO_OK : BIO_INNER_RETRY;
}

void WCacheManager::ScanProcCache(uint64_t procId, std::list<WCache*> &list)
{
    ReadLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    bool isMaster = false;
    for (const auto &flowIt : mWCacheManager) {
        uint16_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
        if (procId != flowIt.second->GetProcId() || !flowIt.second->GetState() ||
            !flowIt.second->IsWritable()) {
            continue;
        }
        isMaster = false;
        auto ret = mLocRole(flowPtId, isMaster);
        if (UNLIKELY(ret != BIO_OK)) {
            LOG_ERROR("Get pt info fail, ptId:" << flowPtId << ".");
        }
        if (!isMaster) {
            continue;
        }
        flowIt.second->SetState(false);
        LOG_DEBUG("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() << ", flowId:" << flowIt.first <<
            ", procId:" << procId << ", Mem:" << flowIt.second->GetCapacity(WCACHE_MEMORY) << ", Disk:" <<
            flowIt.second->GetCapacity(WCACHE_DISK));
        list.emplace_back(flowIt.second.Get());
    }

    return;
}

BResult WCacheManager::ClearProcCache(uint32_t procId)
{
    bool result = false;
    BIO_TP_START(NO_PROCESS_CLEAR_PROC_CACHE, 0);
    uint64_t evictTime = Monotonic::TimeSec() + DESTROY_EVICT_TIMEOUT;

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        for (const auto &flowIt : mWCacheManager) {
            if (procId != flowIt.second->GetProcId() || !flowIt.second->IsWritable()) {
                continue;
            }
            uint16_t flowPtId = CacheFlowIdManager::GetPtId(flowIt.first);
            LOG_INFO("Flow ptId:" << flowPtId << ", ptv:" << flowIt.second->GetPtv() << ", flowId:" << flowIt.first <<
                ", procId:" << procId << ", Vir Mem:" << flowIt.second->GetVirCapacity(WCACHE_MEMORY) <<
                ", Vir Disk:" << flowIt.second->GetVirCapacity(WCACHE_DISK));
            mDestroyManager.emplace(flowIt.first, evictTime);
        }
    }

    result = mDestroyEvictService->Execute([this]() { DestroyEvictThread(); });
    ChkTrue(result, BIO_INNER_ERR, "Execute destroy evict service failed.");
    BIO_TP_END;
    return BIO_OK;
}

inline WCachePtr WCacheManager::GetWCache(uint64_t flowId)
{
    ReadLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    auto wflowIt = mWCacheManager.find(flowId);
    if (UNLIKELY(wflowIt == mWCacheManager.end())) {
        return nullptr;
    }

    WCachePtr wcache = wflowIt->second;
    bool isNormal = true;
    BIO_TP_START(WCACHE_STATE_NORMAL, &isNormal, true);
    BIO_TP_START(WCACHE_STATE_NOT_NORMAL, &isNormal, false);
    isNormal = wcache->GetState();
    BIO_TP_END;
    BIO_TP_END;
    if (!isNormal) {
        LOG_WARN("Check wcache state failed, flowId:" << flowId << ", state:" << isNormal << ".");
        return nullptr;
    }

    return wcache;
}

WCachePtr WCacheManager::GetWCacheForCleanup(uint64_t flowId)
{
    ReadLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    auto iter = mWCacheManager.find(flowId);
    return iter == mWCacheManager.end() ? nullptr : iter->second;
}

WCachePtr WCacheManager::AcquireWCacheForPut(uint64_t flowId)
{
    ReadLocker<ReadWriteLock> lock(&mWCacheManagerLock);
    auto iter = mWCacheManager.find(flowId);
    if (iter == mWCacheManager.end()) {
        return nullptr;
    }

    WCachePtr wcache = iter->second;
    bool isNormal = true;
    BIO_TP_START(WCACHE_STATE_NORMAL, &isNormal, true);
    BIO_TP_START(WCACHE_STATE_NOT_NORMAL, &isNormal, false);
    isNormal = wcache->GetState();
    BIO_TP_END;
    BIO_TP_END;

    if (!isNormal || !wcache->IsWritable()) {
        return nullptr;
    }
    wcache->IncFlyIo();
    return wcache;
}

BResult WCacheManager::Read(uint64_t offset, const WCacheSlicePtr &srcSlice, const RCacheSlicePtr &destSlice,
    const SliceWriter &sliceWriter, uint64_t &realLen)
{
    if (srcSlice == nullptr) {
        LOG_ERROR("src slice is null.");
        return BIO_INNER_ERR;
    }
    if (UNLIKELY(offset >= srcSlice->GetLength())) {
        LOG_ERROR("Failed to split slice. offset:" << offset << ", length:" << srcSlice->GetLength());
        return BIO_READ_EXCEED;
    }

    realLen = srcSlice->GetLength() - offset;
    if (realLen > destSlice->GetLength()) {
        realLen = destSlice->GetLength();
    }
    auto newSlice = srcSlice->Split(offset, realLen);
    if (UNLIKELY(newSlice == nullptr)) {
        LOG_ERROR("Failed to split slice. offset:" << offset << ", length:" << realLen);
        return BIO_READ_EXCEED;
    }

    auto ret = sliceWriter(newSlice, destSlice.Get());
    if (ret != BIO_OK) {
        LOG_ERROR("Call slice writer to dst slice failed, ret:" << ret << ", offset:" << offset << ", length:" <<
            realLen << ".");
    }
    return ret;
}

void WCacheManager::RetryEvictThread()
{
    std::vector<uint64_t> retryFlows;
    while (mRunning.load()) {
        {
            WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
            retryFlows = std::move(mRetryManager[WCACHE_MEMORY]);
        }

        for (const auto &flowId : retryFlows) {
            ReadLocker<ReadWriteLock> lock(&mWCacheManagerLock);
            auto wflowIt = mWCacheManager.find(flowId);
            if (UNLIKELY(wflowIt == mWCacheManager.end())) {
                LOG_WARN("Failed to get flow by id:" << flowId);
                continue;
            }
            wflowIt->second->RetryEvictTask(WCACHE_MEMORY);
        }
        retryFlows.clear();
        {
            WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
            retryFlows = std::move(mRetryManager[WCACHE_DISK]);
        }

        for (const auto &flowId: retryFlows) {
            ReadLocker<ReadWriteLock> lock(&mWCacheManagerLock);
            auto wflowIt = mWCacheManager.find(flowId);
            if (UNLIKELY(wflowIt == mWCacheManager.end())) {
                LOG_WARN("Failed to get flow by id:" << flowId);
                continue;
            }
            wflowIt->second->RetryEvictTask(WCACHE_DISK);
        }
        retryFlows.clear();
        sleep(1);
    }
}

void WCacheManager::DestroyEvictThread()
{
    std::unordered_map<uint64_t, uint64_t> destroyManager;

    {
        WriteLocker<ReadWriteLock> lock(&mWCacheManagerLock);
        std::swap(mDestroyManager, destroyManager);
    }

    while (!destroyManager.empty()) {
        uint64_t curTime = Monotonic::TimeSec();
        for (auto it = destroyManager.begin(); it != destroyManager.end();) {
            if (it->second <= curTime) {
                auto ret = DeleteWCache(it->first);
                if (ret != BIO_OK) {
                    ++it;
                    continue;
                }
                it = destroyManager.erase(it);
            } else {
                LOG_INFO("Delay destroy, flowId:" << it->first << ", expire:" << it->second << ", curTime:" << curTime);
                ++it;
            }
        }
        sleep(DESTROY_EVICT_INTERAL);
    }
}

void WCacheManager::ScheduleFlushMetaEvents()
{
    bool expected = false;
    if (!mMetaReportScheduled.compare_exchange_weak(expected, true)) {
        return;
    }

    if (mMetaReportService == nullptr) {
        FlushMetaEvents();
        return;
    }

    bool isSucceed = mMetaReportService->Execute([this]() { FlushMetaEvents(); });
    if (!isSucceed) {
        mMetaReportScheduled.store(false);
        LOG_WARN("Schedule UBS IO meta event report failed.");
    }
}

void WCacheManager::FlushMetaEvents()
{
    std::vector<UbsIoMetaEvent> events;
    {
        std::lock_guard<std::mutex> lock(mMetaReportLock);
        events.swap(mPendingMetaEvents);
    }

    ReportMetaEventsSync(std::move(events));

    mMetaReportScheduled.store(false);
    {
        std::lock_guard<std::mutex> lock(mMetaReportLock);
        if (mPendingMetaEvents.empty()) {
            return;
        }
    }
    ScheduleFlushMetaEvents();
}

BResult WCacheManager::GetTruncateIndex(uint64_t flowId, uint64_t &truncateIndex)
{
    LOG_DEBUG("Get negotiate message,flow:" << flowId);
    WCachePtr wCache = GetWCache(flowId);
    if (UNLIKELY(wCache == nullptr)) {
        LOG_ERROR("Failed to get WCache. flowId:" << flowId << ".");
        return BIO_OK;
    }
    if (UNLIKELY(wCache->GetState() == false)) {
        return BIO_OK;
    }
    truncateIndex = wCache->GetTruncateIndex();
    return BIO_OK;
}

BResult WCacheManager::GetReuseFlowId(uint16_t ptId, uint64_t &flowId)
{
    mReuseFlowsLock.LockWrite();
    if (mReuseFlows.find(ptId) != mReuseFlows.end()) {
        if (mReuseFlows[ptId].empty()) {
            mReuseFlows.erase(ptId);
            mReuseFlowsLock.UnLock();
            return BIO_NOT_EXISTS;
        }
        flowId = *(mReuseFlows[ptId].begin());
        mReuseFlows[ptId].erase(flowId);
        mReuseFlowsLock.UnLock();
        return BIO_OK;
    }
    mReuseFlowsLock.UnLock();
    return BIO_NOT_EXISTS;
}

BResult WCacheManager::ProcBrokenSyncOldFlow(uint64_t flowId, uint64_t index, uint64_t offset, bool &needDestroy)
{
    auto wcache = GetWCache(flowId);
    if (UNLIKELY(wcache == nullptr)) {
        LOG_ERROR("WCache failed to get flow, flowId:" << flowId << ".");
        needDestroy = true;
        return BIO_OK;
    }

    while (!wcache->IsIoFinish()) {
        usleep(NO_20 * NO_10000);
    }

    if (wcache->GetIndex() == index && wcache->GetOffset() == offset) {
        needDestroy = false;
        LOG_DEBUG("Current node not need to destroy flow:" << wcache->GetFlowId() << ", pt:" <<
                                                           wcache->GetPtId() << ", ptV:" << wcache->GetPtv() << ".");
        return BIO_OK;
    }
    needDestroy = true;
    LOG_DEBUG("Current node flow:" << wcache->GetFlowId() << ", pt:" << wcache->GetPtId() << ", index:" <<
        wcache->GetIndex() << ", offset:" << wcache->GetOffset() << ", is not same to master node flow index:" <<
        index << ", offset:" << offset << ".");
    auto ret = DestroyWCache(wcache->GetProcId(), flowId, wcache->GetPtId(), wcache->GetPtv());
    return ret;
}

}
}
