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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <gtest/gtest.h>

#define private public
#include "bio_server.h"
#include "flow_manager.h"
#include "wcache_manager.h"
#undef private
#include "cache_flow.h"

using namespace ock::bio;

namespace {
constexpr uint64_t TEST_SEGMENT = 4096;
constexpr uint64_t TEST_META_CAPACITY = 1048576;

class TestWCacheGlobalEvict : public testing::Test {
protected:
    using Domain = WCacheManager::EvictDomain;

    void SetUp() override
    {
        auto &config = Config();
        originalConfig = config;
        config.hasDiskCache = true;
        config.diskCaps = { 100 * TEST_SEGMENT, 100 * TEST_SEGMENT };
        config.diskWriteRatio = 10;
        config.wcacheDiskEvictLevel = 0;
        config.memCap = 100 * TEST_SEGMENT;
        config.memWriteRatio = 10;
        config.wcacheMemEvictLevel = 0;
        originalDiskAllocator = FlowManager::mDiskAllocator;
        originalMemAllocator = FlowManager::mMemAllocator;
        originalCacheType = FlowManager::mGetCacheType;
        FlowManager::mDiskAllocator.free = [](uint32_t, uint64_t, uint64_t) {};
        FlowManager::mMemAllocator.free = [](uint64_t) {};
        FlowManager::mGetCacheType = [](uint64_t) { return FLOW_WCACHE; };
        originalUsed[0] = FlowManager::mUsedSize[FLOW_WCACHE][FLOW_MEMORY][0].exchange(0);
        originalUsed[1] = FlowManager::mUsedSize[FLOW_WCACHE][FLOW_DISK][0].exchange(0);
        originalUsed[2] = FlowManager::mUsedSize[FLOW_WCACHE][FLOW_DISK][1].exchange(0);
        uint64_t viewTime = 0;
        originalPtView = BioServer::Instance()->GetPtView(&viewTime);
        manager.mStandaloneMode = true;
        manager.mGlobalEvictReady = true;
        manager.mCacheIndex = MakeRef<WCacheIndex>();
    }

    void TearDown() override
    {
        manager.mRunning = false;
        for (auto &service : manager.mEvictService) {
            if (service != nullptr) {
                service->Stop();
                service = nullptr;
            }
        }
        manager.mWCacheManager.clear();
        manager.mCacheIndex = nullptr;
        FlowManager::mDiskAllocator = originalDiskAllocator;
        FlowManager::mMemAllocator = originalMemAllocator;
        FlowManager::mGetCacheType = originalCacheType;
        FlowManager::mUsedSize[FLOW_WCACHE][FLOW_MEMORY][0] = originalUsed[0];
        FlowManager::mUsedSize[FLOW_WCACHE][FLOW_DISK][0] = originalUsed[1];
        FlowManager::mUsedSize[FLOW_WCACHE][FLOW_DISK][1] = originalUsed[2];
        Config() = originalConfig;
        std::lock_guard<std::mutex> lock(BioServer::Instance()->mPtViewMutex);
        BioServer::Instance()->mPtView = originalPtView;
    }

    BioConfig::DaemonConfig &Config()
    {
        return const_cast<BioConfig::DaemonConfig &>(BioConfig::Instance()->GetDaemonConfig());
    }

    void SetPt(uint16_t ptId, uint64_t version, uint16_t diskId)
    {
        auto node = BioServer::Instance()->GetLocalNid().VNodeId();
        std::lock_guard<std::mutex> lock(BioServer::Instance()->mPtViewMutex);
        BioServer::Instance()->mPtView[ptId] = CmPtInfo(version, ptId, CM_PT_NORMAL, node, diskId,
            { {node, diskId, CM_COPY_RUNNING} });
    }

    std::string KeyFor(uint64_t id, uint32_t index) const
    {
        return "global-evict-test-" + std::to_string(id) + "-" + std::to_string(index);
    }

    // Real WCache/SliceRef/truncate paths, with synthetic metadata in RAM and no device I/O.
    uint64_t AddFlow(uint32_t slices, bool historical = false, uint16_t diskId = 0, uint64_t ptv = 1,
        bool publish = true)
    {
        uint16_t ptId = 7000 + ++nextId;
        uint64_t id = (static_cast<uint64_t>(CacheFlowIdManager::GenerateCacheFlowIdPrefix(
            ptId, ptv, WRITE_CACHE, 0)) << CACHE_FLOW_ID_PREFIX_SHIFT) | nextId;
        auto flow = MakeRef<WCache>(1, id, ptId, ptv, diskId, false);
        flow->mHasDiskCache = manager.mHasDiskCache;
        flow->mIndex = slices;
        flow->mOffset = slices * TEST_SEGMENT;
        flow->mCacheTiers[WCACHE_MEMORY] = MakeRef<WCacheTier>();
        auto tier = manager.mHasDiskCache ? MakeRef<WCacheTier>() : flow->mCacheTiers[WCACHE_MEMORY];
        auto tierType = manager.mHasDiskCache ? WCACHE_DISK : WCACHE_MEMORY;
        auto flowType = manager.mHasDiskCache ? FLOW_DISK : FLOW_MEMORY;
        if (manager.mHasDiskCache) {
            flow->mCacheTiers[WCACHE_DISK] = tier;
        }
        tier->type = tierType;
        tier->mFlowTruncateCursor = MakeRef<WFlowTruncateCursor>();
        buffers.emplace_back(new char[TEST_META_CAPACITY]());
        tier->mMetaFlow = MakeRef<Flow>(FLOW_META, FLOW_MEMORY, id, diskId, TEST_META_CAPACITY, 0);
        tier->mMetaFlow->mChunkList.push_back(reinterpret_cast<uint64_t>(buffers.back().get()));
        tier->mMetaFlow->mWrittenOffset = slices * sizeof(WFlowSliceMeta);
        tier->mMetaFlow->mPreLoadOffset = slices * sizeof(WFlowSliceMeta);
        tier->mMetaFlow->mSealed = true;
        // FLOW_META avoids unrelated quota accounting in the no-disk fixture.
        tier->mDataFlow = MakeRef<Flow>(FLOW_META, flowType, id, diskId, TEST_SEGMENT, 0);
        tier->mDataFlow->mWrittenOffset = slices * TEST_SEGMENT;
        tier->mDataFlow->mPreLoadOffset = slices * TEST_SEGMENT;
        tier->mDataFlow->mSealed = true;
        auto *metas = reinterpret_cast<WFlowSliceMeta *>(buffers.back().get());
        for (uint32_t i = 0; i < slices; ++i) {
            std::string key = KeyFor(id, i);
            std::memcpy(metas[i].key, key.c_str(), key.size() + 1);
            metas[i].magic = id;
            metas[i].offset = i * TEST_SEGMENT;
            metas[i].length = TEST_SEGMENT;
            tier->mDataFlow->mChunkList.push_back(i + 1);
            std::vector<FlowAddr> addrs;
            auto slice = MakeRef<WCacheSlice>(id, i * TEST_SEGMENT, i, TEST_SEGMENT, addrs, flowType);
            auto ref = MakeRef<WCacheSliceRef>(slice, publish ? SLICE_VALID : SLICE_PENDING);
            tier->AddEvictQueue(ref);
            if (publish) {
                manager.mCacheIndex->Insert(ptId, const_cast<char *>(key.c_str()), ref);
            }
        }
        FlowManager::mUsedSize[FLOW_WCACHE][flowType][diskId] += slices * TEST_SEGMENT;
        flow->RegOp([](uint16_t, uint16_t, bool &normal) { normal = true; },
            [](uint16_t, bool &master) { master = true; return BIO_OK; },
            [](uint16_t, uint64_t, uint64_t &offset) { offset = NO_MAX_VALUE64; return BIO_OK; },
            [this, id](uint16_t pt, const Key &key, WCacheSliceRefPtr ref, const UbsIoMetaEventBatchPtr &) {
                deletedFlows.push_back(id);
                return manager.mCacheIndex->Delete(pt, key, ref);
            }, [this](uint64_t, WCacheTierType) { ++retryCount; }, [](const UbsIoMetaEventBatchPtr &) {});
        if (historical) {
            flow->MarkReadOnly();
        }
        manager.mWCacheManager.emplace(id, flow);
        manager.mGlobalEvictQueues[Domain(tierType, diskId)];
        manager.EnqueueInactiveLocked(flow);
        SetPt(ptId, ptv, diskId);
        return id;
    }

    size_t Queued(uint64_t id, WCacheTierType type = WCACHE_DISK)
    {
        return manager.mWCacheManager.at(id)->mCacheTiers[type]->mEvictSliceQueue.size();
    }

    void Run(uint16_t diskId = 0)
    {
        manager.RunGlobalEvict(Domain(manager.mHasDiskCache ? WCACHE_DISK : WCACHE_MEMORY, diskId));
    }

    FlowPtr MemoryFlow(uint64_t id, uint64_t capacity)
    {
        buffers.emplace_back(new char[capacity]());
        auto flow = MakeRef<Flow>(FLOW_META, FLOW_MEMORY, id, 0, capacity, 0);
        flow->mChunkList.push_back(reinterpret_cast<uint64_t>(buffers.back().get()));
        flow->mWrittenOffset = capacity;
        flow->mPreLoadOffset = capacity;
        flow->mSealed = true;
        return flow;
    }

    WCachePtr MemoryWriter(uint32_t slots = 3)
    {
        Config().enableCrc = false;
        auto flow = manager.mWCacheManager.at(AddFlow(0));
        auto tier = flow->mCacheTiers[WCACHE_MEMORY];
        tier->type = WCACHE_MEMORY;
        tier->mMetaFlow = MemoryFlow(flow->GetFlowId(), TEST_META_CAPACITY);
        tier->mDataFlow = MemoryFlow(flow->GetFlowId(), slots * TEST_SEGMENT);
        tier->mFlowTruncateCursor = MakeRef<WFlowTruncateCursor>();
        flow->mScheduleEvictCallback = [](WCacheTierType) {};
        flow->mPublishIndexCallback = [this](uint16_t pt, const Key &key, const WCacheSliceRefPtr &ref) {
            return manager.mCacheIndex->Insert(pt, key, ref);
        };
        return flow;
    }

    BResult PutValue(const WCachePtr &flow, const char *key, uint64_t index, char value,
        WCacheSliceRefPtr &ref, RealIoStrategy strategy = WRITE_MEM_BACK)
    {
        std::vector<char> data(TEST_SEGMENT, value);
        std::vector<FlowAddr> addrs{FlowAddr(reinterpret_cast<uint64_t>(data.data()), 0, TEST_SEGMENT)};
        auto slice = MakeRef<WCacheSlice>(flow->GetFlowId(), index * TEST_SEGMENT, index,
            TEST_SEGMENT, addrs, FLOW_MEMORY);
        CacheAttr attr(strategy, 0, LOCAL_AFFINITY, WRITE_BACK);
        return flow->Put(const_cast<char *>(key), slice, [](const SlicePtr &from, const SlicePtr &to) {
            CacheSliceOperator op;
            return op.Copy(from, to);
        }, ref, attr);
    }

    WFlowSliceMeta Meta(const WCachePtr &flow, WCacheTierType type, uint64_t index)
    {
        WCacheSlicePtr slice;
        EXPECT_EQ(flow->mCacheTiers[type]->GetMetaSlice(index, slice), BIO_OK);
        WFlowSliceMeta meta{};
        CacheSliceOperator op;
        EXPECT_EQ(op.Copy(slice.Get(), reinterpret_cast<char *>(&meta), sizeof(meta)), BIO_OK);
        return meta;
    }

    // Isolate the handover callback with actual metadata copies in RAM; no raw device access.
    void PrepareHandover(const WCachePtr &flow, const WCacheSliceRefPtr &ref,
        WCacheSlicePtr &memoryMeta, WCacheSlicePtr &diskMeta)
    {
        auto index = ref->GetSlice()->GetIndexInFlow();
        EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetMetaSlice(index, memoryMeta), BIO_OK);
        EXPECT_EQ(flow->mCacheTiers[WCACHE_DISK]->GetMetaSlice(index, diskMeta), BIO_OK);
        CacheSliceOperator op;
        EXPECT_EQ(op.Copy(memoryMeta.Get(), diskMeta.Get()), BIO_OK);
        EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetEvictSlice(), ref);
    }

    void Handover(const WCachePtr &flow, const WCacheSliceRefPtr &ref,
        const WCacheSlicePtr &memoryMeta, const WCacheSlicePtr &diskMeta)
    {
        auto old = ref->GetSlice();
        std::vector<FlowAddr> addrs;
        auto disk = MakeRef<WCacheSlice>(old->GetFlowId(), old->GetOffsetInFlow(), old->GetIndexInFlow(),
            old->GetLength(), addrs, FLOW_DISK);
        WCacheSliceRef::SetSliceCallback callback = [flow, ref, memoryMeta, diskMeta](const WCacheSlicePtr &oldSlice) {
            flow->CompleteMemToDisk(ref, oldSlice, memoryMeta, diskMeta);
        };
        ref->SetSlice(disk, callback);
    }

    WCacheManager manager;
    BioConfig::DaemonConfig originalConfig;
    DiskAllocator originalDiskAllocator;
    MemAllocator originalMemAllocator;
    GetCacheType originalCacheType;
    uint64_t originalUsed[3]{};
    std::map<uint16_t, CmPtInfo> originalPtView;
    std::vector<std::unique_ptr<char[]>> buffers;
    std::vector<uint64_t> deletedFlows;
    uint16_t nextId = 0;
    uint32_t retryCount = 0;
};

TEST_F(TestWCacheGlobalEvict, historical_head_stops_at_watermark_and_resumes_before_next_flow)
{
    auto first = AddFlow(5, true);
    auto second = AddFlow(3, true);
    auto active = AddFlow(2);
    Config().wcacheDiskEvictLevel = 8;
    Run();
    EXPECT_EQ(Queued(first), 3U);
    EXPECT_EQ(Queued(second), 3U);
    EXPECT_EQ(Queued(active), 2U);
    EXPECT_EQ(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).inactiveFlows.front(), first);
    EXPECT_FALSE(manager.mWCacheManager.at(first)->mIsForced.load());
    EXPECT_EQ(FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_DISK, 0), 8 * TEST_SEGMENT);
    Run();
    EXPECT_EQ(Queued(first), 3U);
    Config().wcacheDiskEvictLevel = 5;
    Run();
    EXPECT_EQ(manager.mWCacheManager.count(first), 0U);
    EXPECT_EQ(Queued(second), 3U);
    EXPECT_EQ(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).inactiveFlows.front(), second);
}

TEST_F(TestWCacheGlobalEvict, active_flows_rotate_in_bounded_batches)
{
    auto first = AddFlow(40);
    auto second = AddFlow(40);
    Run();
    EXPECT_EQ(Queued(first), 8U);
    EXPECT_EQ(Queued(second), 40U);
    Run();
    EXPECT_EQ(Queued(first), 8U);
    EXPECT_EQ(Queued(second), 8U);
    Run();
    EXPECT_EQ(Queued(first), 0U);
    EXPECT_EQ(Queued(second), 8U);
    Run();
    EXPECT_EQ(Queued(second), 0U);
    EXPECT_EQ(deletedFlows.size(), 80U);
}

TEST_F(TestWCacheGlobalEvict, failed_head_is_retried_without_eviction_of_later_flows)
{
    auto first = AddFlow(2, true);
    auto second = AddFlow(2, true);
    manager.mWCacheManager.at(first)->mLocRole = [](uint16_t, bool &) { return BIO_ERR; };
    Run();
    EXPECT_EQ(Queued(first), 2U);
    EXPECT_EQ(Queued(second), 2U);
    EXPECT_TRUE(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).pending);
    EXPECT_FALSE(manager.mWCacheManager.at(first)->mEvictRef[WCACHE_DISK].load());
    EXPECT_EQ(manager.mWCacheManager.at(first)->GetFlyIo(), 0U);
    EXPECT_TRUE(manager.mWCacheManager.at(first)->IsEvictIoFinish());
    manager.mWCacheManager.at(first)->mLocRole = [](uint16_t, bool &master) { master = true; return BIO_OK; };
    Config().wcacheDiskEvictLevel = 3;
    Run();
    EXPECT_EQ(Queued(first), 1U);
    EXPECT_EQ(Queued(second), 2U);
}

TEST_F(TestWCacheGlobalEvict, delayed_reader_keeps_empty_head_until_callback_and_retirement_finish)
{
    auto first = AddFlow(1, true);
    auto second = AddFlow(1, true);
    auto key = KeyFor(first, 0);
    auto reader = manager.mCacheIndex->Aquire(manager.mWCacheManager.at(first)->GetPtId(),
        const_cast<char *>(key.c_str()));
    ASSERT_NE(reader, nullptr);
    Run();
    EXPECT_EQ(Queued(first), 0U);
    Run();
    EXPECT_EQ(Queued(second), 1U);
    EXPECT_EQ(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).inactiveFlows.front(), first);
    reader->Release();
    Run();
    EXPECT_EQ(manager.mWCacheManager.count(first), 0U);
    EXPECT_EQ(manager.mWCacheManager.count(second), 0U);
    EXPECT_EQ(deletedFlows, std::vector<uint64_t>({first, second}));
}

TEST_F(TestWCacheGlobalEvict, full_pt_version_is_checked_and_recovered_flow_stays_historical)
{
    auto active = AddFlow(1);
    auto recovered = AddFlow(1, true);
    auto pt = manager.mWCacheManager.at(active)->GetPtId();
    SetPt(pt, 2049, 0);
    uint64_t time = 0;
    auto view = BioServer::Instance()->GetPtView(&time);
    EXPECT_FALSE(manager.IsCurrentFlow(manager.mWCacheManager.at(active), view));
    EXPECT_FALSE(manager.IsCurrentFlow(manager.mWCacheManager.at(recovered), view));
    auto &queue = manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).inactiveFlows;
    EXPECT_EQ(queue, std::deque<uint64_t>({recovered}));
    EXPECT_EQ(manager.MarkPtFlowsReadOnly(pt, 1, 0), 1U);
    EXPECT_FALSE(manager.mWCacheManager.at(active)->IsWritable());
    EXPECT_EQ(queue, std::deque<uint64_t>({recovered, active}));
    EXPECT_EQ(manager.MarkPtFlowsReadOnly(pt, 1, 0), 0U);
    EXPECT_EQ(queue.size(), 2U);
}

TEST_F(TestWCacheGlobalEvict, eviction_round_does_not_reclassify_flows_from_pt_snapshot)
{
    auto flow = AddFlow(1, false, 0, 2);
    auto pt = manager.mWCacheManager.at(flow)->GetPtId();
    SetPt(pt, 1, 0);
    Run();
    EXPECT_TRUE(manager.mWCacheManager.at(flow)->IsWritable());
    EXPECT_EQ(Queued(flow), 1U);
    SetPt(pt, 3, 0);
    Run();
    EXPECT_TRUE(manager.mWCacheManager.at(flow)->IsWritable());
    EXPECT_EQ(Queued(flow), 1U);
    EXPECT_TRUE(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).inactiveFlows.empty());

    EXPECT_EQ(manager.MarkPtFlowsReadOnly(pt, 2, 0), 1U);
    EXPECT_EQ(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).inactiveFlows,
        std::deque<uint64_t>({flow}));
    Run();
    EXPECT_EQ(manager.mWCacheManager.count(flow), 0U);
}

TEST_F(TestWCacheGlobalEvict, disk_migration_registers_old_flow_once_and_preserves_old_disk_queue)
{
    auto old = AddFlow(2);
    auto pt = manager.mWCacheManager.at(old)->GetPtId();
    EXPECT_EQ(manager.MarkPtFlowsReadOnly(pt, 1, 0), 1U);
    SetPt(pt, 2, 1);
    EXPECT_EQ(manager.MarkPtFlowsReadOnly(pt, 1, 0), 0U);
    EXPECT_EQ(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).inactiveFlows,
        std::deque<uint64_t>({old}));
    Config().wcacheDiskEvictLevel = 1;
    Run();
    EXPECT_EQ(Queued(old), 1U);
}

TEST_F(TestWCacheGlobalEvict, disk_watermark_is_not_diluted_by_another_disk)
{
    auto first = AddFlow(2, true, 0);
    auto second = AddFlow(2, true, 1);
    Config().diskCaps[1] = 10000 * TEST_SEGMENT;
    Config().wcacheDiskEvictLevel = 1;
    Run(0);
    Run(1);
    EXPECT_EQ(Queued(first), 1U);
    EXPECT_EQ(Queued(second), 2U);
}

TEST_F(TestWCacheGlobalEvict, fault_fences_admitted_batch_and_removes_history_queue_entries)
{
    auto failed = AddFlow(1, true, 0);
    auto healthy = AddFlow(1, true, 1);
    uint64_t time = 0;
    auto task = manager.SelectEvictFlowLocked(Domain(WCACHE_DISK, 0),
        BioServer::Instance()->GetPtView(&time));
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->GetFlyIo(), 0U);
    EXPECT_FALSE(task->IsEvictIoFinish());
    std::unordered_map<uint16_t, std::list<WCachePtr>> flows;
    std::unordered_map<uint16_t, std::unordered_set<uint64_t>> ids;
    manager.CollectFaultedFlows(0, flows, ids);
    EXPECT_TRUE(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).inactiveFlows.empty());
    EXPECT_EQ(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 1)).inactiveFlows.front(), healthy);
    EXPECT_FALSE(task->BeginEvictBatch(WCACHE_MEMORY));
    EXPECT_FALSE(task->BeginEvictBatch(WCACHE_DISK));
    // The legacy retry gate may clear the worker flag, but cannot release the admitted batch.
    task->RetryEvictTask(WCACHE_DISK);
    EXPECT_FALSE(task->mEvictRef[WCACHE_DISK].load());
    EXPECT_FALSE(task->IsEvictIoFinish());
    EXPECT_EQ(manager.WaitFaultedFlowsIdle(0, flows.at(task->GetPtId())), BIO_INNER_RETRY);
    uint32_t count = 0;
    EXPECT_EQ(task->EvictBatch(WCACHE_DISK, 32, count), BIO_INNER_RETRY);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(task->GetFlyIo(), 0U);
    EXPECT_TRUE(task->IsEvictIoFinish());
    EXPECT_EQ(manager.WaitFaultedFlowsIdle(0, flows.at(task->GetPtId())), BIO_OK);
    EXPECT_EQ(Queued(failed), 1U);
    // Synthetic flows are not registered with the real BDM/FlowManager.
    task->mStandaloneFault = false;
}

TEST_F(TestWCacheGlobalEvict, put_and_memory_disk_batch_lifetimes_are_independent)
{
    auto id = AddFlow(1);
    auto flow = manager.AcquireWCacheForPut(id);
    ASSERT_NE(flow, nullptr);
    EXPECT_EQ(flow->GetFlyIo(), 1U);
    ASSERT_TRUE(flow->BeginEvictBatch(WCACHE_MEMORY));
    EXPECT_FALSE(flow->BeginEvictBatch(WCACHE_MEMORY));
    EXPECT_EQ(flow->GetFlyIo(), 1U);
    EXPECT_FALSE(flow->IsEvictIoFinish());
    uint32_t count = 0;
    EXPECT_EQ(flow->EvictBatch(WCACHE_DISK, 0, count), BIO_INVALID_PARAM);
    EXPECT_FALSE(flow->IsEvictIoFinish());

    // An unrelated Put must not block eviction of this flow's already published slices.
    ASSERT_TRUE(flow->BeginEvictBatch(WCACHE_DISK));
    EXPECT_EQ(flow->GetFlyIo(), 1U);
    flow->DecFlyIo();
    EXPECT_TRUE(flow->IsIoFinish());
    EXPECT_EQ(flow->GetFlyIo(), 0U);
    EXPECT_EQ(flow->EvictBatch(WCACHE_MEMORY, 0, count), BIO_OK);
    EXPECT_FALSE(flow->IsEvictIoFinish());
    EXPECT_EQ(flow->EvictBatch(WCACHE_DISK, 0, count), BIO_OK);
    EXPECT_TRUE(flow->IsEvictIoFinish());
    EXPECT_EQ(flow->GetFlyIo(), 0U);
    EXPECT_EQ(flow->EvictBatch(WCACHE_DISK, 0, count), BIO_INVALID_PARAM);
    EXPECT_TRUE(flow->IsEvictIoFinish());
}

TEST_F(TestWCacheGlobalEvict, memory_downflow_failure_releases_batch_before_retry)
{
    auto id = AddFlow(1);
    auto flow = manager.mWCacheManager.at(id);
    auto &memTier = flow->mCacheTiers[WCACHE_MEMORY];
    auto &diskTier = flow->mCacheTiers[WCACHE_DISK];
    memTier->type = WCACHE_MEMORY;
    memTier->mMetaFlow = diskTier->mMetaFlow; // Fixture metadata already resides in RAM.
    memTier->mDataFlow = MakeRef<Flow>(FLOW_META, FLOW_MEMORY, id, 0, TEST_SEGMENT, 0);
    buffers.emplace_back(new char[TEST_SEGMENT]());
    memTier->mDataFlow->mChunkList.push_back(reinterpret_cast<uint64_t>(buffers.back().get()));
    memTier->mDataFlow->mWrittenOffset = TEST_SEGMENT;
    memTier->mDataFlow->mPreLoadOffset = TEST_SEGMENT;
    memTier->mDataFlow->mSealed = true;
    std::vector<FlowAddr> addrs;
    auto slice = MakeRef<WCacheSlice>(id, 0, 0, TEST_SEGMENT, addrs, FLOW_MEMORY);
    auto ref = MakeRef<WCacheSliceRef>(slice, SLICE_PENDING);
    memTier->AddEvictQueue(ref);
    FlowManager::mUsedSize[FLOW_WCACHE][FLOW_MEMORY][0] = TEST_SEGMENT;
    // Fail the destination slice lookup before any device I/O.
    diskTier->mDataFlow->mTruncateOffset = TEST_SEGMENT;
    flow->mRetryCallback = [&](uint64_t flowId, WCacheTierType type) {
        WriteLocker<ReadWriteLock> lock(&manager.mWCacheManagerLock);
        EXPECT_FALSE(flow->IsEvictIoFinish());
        EXPECT_EQ(flow->GetFlyIo(), 0U);
        manager.mRetryManager[type].push_back(flowId);
    };

    for (uint32_t attempt = 0; attempt < 2; ++attempt) {
        EXPECT_TRUE(flow->IsEvictIoFinish());
        ASSERT_TRUE(flow->BeginEvictBatch(WCACHE_MEMORY));
        uint32_t count = 0;
        EXPECT_EQ(flow->EvictBatch(WCACHE_MEMORY, 32, count), BIO_ERR);
        EXPECT_EQ(count, 0U);
        EXPECT_TRUE(flow->IsEvictIoFinish());
        EXPECT_EQ(flow->GetFlyIo(), 0U);
        ASSERT_EQ(memTier->mEvictSliceQueue.size(), 1U);
        EXPECT_EQ(memTier->mEvictSliceQueue.front(), ref);
        EXPECT_EQ(manager.mRetryManager[WCACHE_MEMORY].size(), attempt + 1);
    }
    std::unordered_map<uint16_t, std::list<WCachePtr>> flows;
    std::unordered_map<uint16_t, std::unordered_set<uint64_t>> ids;
    manager.CollectFaultedFlows(0, flows, ids);
    EXPECT_FALSE(flow->BeginEvictBatch(WCACHE_MEMORY));
    // Pending retry data does not prevent fault cleanup from observing idle workers.
    EXPECT_EQ(manager.WaitFaultedFlowsIdle(0, flows.at(flow->GetPtId())), BIO_OK);
    EXPECT_EQ(manager.UnregisterFaultedFlows(flow->GetPtId(), flows.at(flow->GetPtId())), BIO_OK);
    EXPECT_TRUE(manager.mRetryManager[WCACHE_MEMORY].empty());
    EXPECT_TRUE(flow->IsEvictIoFinish());
    flow->mStandaloneFault = false;
}

TEST_F(TestWCacheGlobalEvict, final_eviction_retries_pending_head_until_index_publication)
{
    for (bool hasDisk : { true, false }) {
        manager.mHasDiskCache = hasDisk;
        Config().hasDiskCache = hasDisk;
        auto type = hasDisk ? WCACHE_DISK : WCACHE_MEMORY;
        auto id = AddFlow(2, false, 0, 1, false);
        auto flow = manager.mWCacheManager.at(id);
        auto head = flow->mCacheTiers[type]->mEvictSliceQueue.front();
        auto key = KeyFor(id, 0);
        EXPECT_FALSE(head->Aquire());
        Run();
        EXPECT_EQ(Queued(id, type), 2U);
        EXPECT_EQ(flow->mCacheTiers[type]->mEvictSliceQueue.front(), head);
        EXPECT_TRUE(manager.mGlobalEvictQueues.at(Domain(type, 0)).pending);
        EXPECT_TRUE(flow->IsEvictIoFinish());
        EXPECT_EQ(manager.mCacheIndex->Insert(flow->GetPtId(), const_cast<char *>(key.c_str()), head), BIO_OK);
        EXPECT_EQ(head->GetState(), SLICE_VALID);
        EXPECT_TRUE(manager.Exist(flow->GetPtId(), const_cast<char *>(key.c_str())));
        Run();
        EXPECT_EQ(Queued(id, type), 1U); // The next pending slice still blocks the head.
        EXPECT_FALSE(manager.Exist(flow->GetPtId(), const_cast<char *>(key.c_str())));
    }
}

TEST_F(TestWCacheGlobalEvict, duplicate_publication_keeps_original_index_and_invalidates_new_slice)
{
    auto id = AddFlow(1);
    auto flow = manager.mWCacheManager.at(id);
    auto original = flow->mCacheTiers[WCACHE_DISK]->mEvictSliceQueue.front();
    auto duplicate = MakeRef<WCacheSliceRef>(original->GetSlice(), SLICE_PENDING);
    auto key = KeyFor(id, 0);
    EXPECT_EQ(manager.mCacheIndex->Insert(flow->GetPtId(), const_cast<char *>(key.c_str()), duplicate), BIO_OK);
    EXPECT_EQ(duplicate->GetState(), SLICE_INVALID);
    auto indexed = manager.mCacheIndex->Aquire(flow->GetPtId(), const_cast<char *>(key.c_str()));
    ASSERT_NE(indexed, nullptr);
    EXPECT_EQ(indexed, original);
    indexed->Release();
}

TEST_F(TestWCacheGlobalEvict, front_disk_operation_retries_unpublished_slice)
{
    auto id = AddFlow(1, false, 0, 1, false);
    auto flow = manager.mWCacheManager.at(id);
    auto ref = flow->mCacheTiers[WCACHE_DISK]->GetEvictSlice();
    auto batch = std::make_shared<UbsIoMetaEventBatch>();
    EXPECT_EQ(flow->EvictFromDiskToUnderFs(ref, true, true, batch), BIO_INNER_RETRY);
    EXPECT_EQ(ref->GetState(), SLICE_PENDING);
    EXPECT_NE(ref->GetSlice(), nullptr);
    EXPECT_TRUE(deletedFlows.empty());
    auto *meta = reinterpret_cast<WFlowSliceMeta *>(buffers.back().get());
    EXPECT_EQ(meta->hasEvict, 0U);
    flow->mCacheTiers[WCACHE_DISK]->RetryEvictQueue(ref);
}

TEST_F(TestWCacheGlobalEvict, memory_put_publishes_complete_data_before_queue_admission)
{
    auto flow = MemoryWriter();
    char key[] = "publish-before-enqueue";
    bool published = false;
    flow->mPublishIndexCallback = [&](uint16_t pt, const Key &putKey, const WCacheSliceRefPtr &ref) {
        EXPECT_TRUE(flow->mCacheTiers[WCACHE_MEMORY]->IsEmptyEvictSliceQueue());
        auto ret = manager.mCacheIndex->Insert(pt, putKey, ref);
        auto indexed = manager.mCacheIndex->Aquire(pt, putKey);
        EXPECT_EQ(indexed, ref);
        if (indexed != nullptr) {
            std::vector<char> value(TEST_SEGMENT);
            CacheSliceOperator op;
            EXPECT_EQ(op.Copy(indexed->GetSlice().Get(), value.data(), value.size()), BIO_OK);
            EXPECT_EQ(value, std::vector<char>(TEST_SEGMENT, 'A'));
            indexed->Release();
        }
        published = true;
        return ret;
    };
    flow->mScheduleEvictCallback = [&](WCacheTierType) { EXPECT_TRUE(published); };
    WCacheSliceRefPtr ref;
    ASSERT_EQ(PutValue(flow, key, 0, 'A', ref), BIO_OK);
    EXPECT_EQ(ref->GetState(), SLICE_VALID);
    EXPECT_EQ(Queued(flow->GetFlowId(), WCACHE_MEMORY), 1U);
}

TEST_F(TestWCacheGlobalEvict, duplicate_then_delete_and_rewrite_recovers_only_new_record)
{
    auto flow = MemoryWriter();
    char key[] = "duplicate-delete-rewrite";
    WCacheSliceRefPtr original, duplicate, replacement;
    ASSERT_EQ(PutValue(flow, key, 0, 'A', original), BIO_OK);
    ASSERT_EQ(PutValue(flow, key, 1, 'B', duplicate), BIO_OK);
    EXPECT_EQ(duplicate->GetState(), SLICE_INVALID);
    EXPECT_EQ(Meta(flow, WCACHE_MEMORY, 1).hasEvict, 1U);
    ASSERT_EQ(manager.Delete(flow->GetPtId(), key), BIO_OK);
    ASSERT_EQ(PutValue(flow, key, 2, 'C', replacement), BIO_OK);

    CacheSliceOperator op;
    auto disk = flow->mCacheTiers[WCACHE_DISK];
    for (uint64_t index = 0; index < 3; ++index) {
        WCacheSlicePtr memoryMeta, diskMeta;
        ASSERT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetMetaSlice(index, memoryMeta), BIO_OK);
        ASSERT_EQ(disk->GetMetaSlice(index, diskMeta), BIO_OK);
        ASSERT_EQ(op.Copy(memoryMeta.Get(), diskMeta.Get()), BIO_OK);
        disk->mDataFlow->mChunkList.push_back(index + 1);
    }
    disk->mDataFlow->mWrittenOffset = 3 * TEST_SEGMENT;
    disk->mDataFlow->mPreLoadOffset = 3 * TEST_SEGMENT;
    auto recoveredIndex = MakeRef<WCacheIndex>();
    uint32_t recovered = 0;
    ASSERT_EQ(flow->Recover([&](uint16_t pt, const Key &recoveredKey, const WCacheSliceRefPtr &ref) {
        ++recovered;
        return recoveredIndex->Insert(pt, recoveredKey, ref);
    }), BIO_OK);
    ASSERT_EQ(recovered, 1U);
    auto recoveredRef = recoveredIndex->Aquire(flow->GetPtId(), key);
    ASSERT_NE(recoveredRef, nullptr);
    EXPECT_EQ(recoveredRef->GetSlice()->GetIndexInFlow(), 2U);
    recoveredRef->Release();
}

TEST_F(TestWCacheGlobalEvict, failed_head_flush_keeps_current_memory_publication)
{
    auto flow = MemoryWriter();
    char currentKey[] = "current";
    WCacheSliceRefPtr head, current;
    ASSERT_EQ(PutValue(flow, "head", 0, 'A', head), BIO_OK);
    flow->mCacheTiers[WCACHE_DISK]->mDataFlow->mTruncateOffset = TEST_SEGMENT;
    EXPECT_NE(PutValue(flow, currentKey, 1, 'B', current, WRITE_DISK_BACK), BIO_OK);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->GetState(), SLICE_VALID);
    EXPECT_TRUE(manager.Exist(flow->GetPtId(), currentKey));
    EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->mEvictSliceQueue.front(), head);
    EXPECT_EQ(Queued(flow->GetFlowId(), WCACHE_MEMORY), 2U);
    EXPECT_EQ(flow->GetIndex(), 2U);
}

TEST_F(TestWCacheGlobalEvict, last_reader_propagates_delete_before_memory_reclamation)
{
    auto flow = MemoryWriter();
    char key[] = "late-delete";
    WCacheSliceRefPtr ref;
    ASSERT_EQ(PutValue(flow, key, 0, 'A', ref), BIO_OK);
    flow->mCacheTiers[WCACHE_DISK]->mDataFlow = MemoryFlow(flow->GetFlowId(), TEST_SEGMENT);
    ASSERT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetEvictSlice(), ref);
    ASSERT_TRUE(ref->Aquire());
    ASSERT_EQ(flow->EvictFromMemToDisk(ref, true), BIO_OK);
    ASSERT_EQ(manager.Delete(flow->GetPtId(), key), BIO_OK);
    EXPECT_EQ(Meta(flow, WCACHE_MEMORY, 0).hasEvict, 1U);
    EXPECT_EQ(Meta(flow, WCACHE_DISK, 0).hasEvict, 0U); // Accepted crash window before the last Release.
    EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetDataEvictOffset(), 0U);
    ref->Release();
    EXPECT_EQ(Meta(flow, WCACHE_DISK, 0).hasEvict, 1U);
    EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetDataEvictOffset(), TEST_SEGMENT);
    EXPECT_EQ(Queued(flow->GetFlowId()), 1U);
    EXPECT_EQ(retryCount, 0U);
}

TEST_F(TestWCacheGlobalEvict, marker_failure_does_not_block_reclamation_or_enqueue_retry)
{
    auto flow = MemoryWriter();
    char key[] = "best-effort-marker";
    WCacheSliceRefPtr ref;
    ASSERT_EQ(PutValue(flow, key, 0, 'A', ref), BIO_OK);
    WCacheSlicePtr memoryMeta, diskMeta;
    PrepareHandover(flow, ref, memoryMeta, diskMeta);
    ASSERT_TRUE(ref->Aquire());
    // A null target deterministically fails the marker write without device I/O.
    Handover(flow, ref, memoryMeta, nullptr);
    ASSERT_EQ(manager.Delete(flow->GetPtId(), key), BIO_OK);
    EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetDataEvictOffset(), 0U);
    EXPECT_NO_THROW(ref->Release());
    EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetDataEvictOffset(), TEST_SEGMENT);
    EXPECT_TRUE(flow->IsEmptyEvict(WCACHE_MEMORY));
    ASSERT_EQ(Queued(flow->GetFlowId()), 1U);
    EXPECT_EQ(flow->mCacheTiers[WCACHE_DISK]->mEvictSliceQueue.front(), ref);
    EXPECT_EQ(Queued(flow->GetFlowId(), WCACHE_MEMORY), 0U);
    // Stale SSD validity on a failed best-effort update is an explicitly accepted outcome.
    EXPECT_EQ(Meta(flow, WCACHE_DISK, 0).hasEvict, 0U);
    EXPECT_EQ(retryCount, 0U);
    EXPECT_TRUE(manager.mRetryManager[WCACHE_MEMORY].empty());
    manager.RetryGlobalEviction();
    EXPECT_EQ(Meta(flow, WCACHE_DISK, 0).hasEvict, 0U);
    EXPECT_EQ(Queued(flow->GetFlowId()), 1U);
}

TEST_F(TestWCacheGlobalEvict, memory_marker_read_failure_still_finishes_handover)
{
    auto flow = MemoryWriter();
    WCacheSliceRefPtr ref;
    ASSERT_EQ(PutValue(flow, "unreadable-marker", 0, 'A', ref), BIO_OK);
    WCacheSlicePtr memoryMeta, diskMeta;
    PrepareHandover(flow, ref, memoryMeta, diskMeta);
    EXPECT_NO_THROW(Handover(flow, ref, nullptr, diskMeta));
    EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetDataEvictOffset(), TEST_SEGMENT);
    EXPECT_EQ(Queued(flow->GetFlowId()), 1U);
    EXPECT_EQ(retryCount, 0U);
}

TEST_F(TestWCacheGlobalEvict, completion_does_not_clear_newer_ssd_delete)
{
    auto flow = MemoryWriter();
    WCacheSliceRefPtr ref;
    ASSERT_EQ(PutValue(flow, "ssd-delete", 0, 'A', ref), BIO_OK);
    WCacheSlicePtr memoryMeta, diskMeta;
    PrepareHandover(flow, ref, memoryMeta, diskMeta);
    WFlowSliceMeta meta = Meta(flow, WCACHE_DISK, 0);
    meta.hasEvict = 1;
    CacheSliceOperator op;
    ASSERT_EQ(op.Copy(reinterpret_cast<const char *>(&meta), diskMeta.Get()), BIO_OK);
    EXPECT_EQ(Meta(flow, WCACHE_MEMORY, 0).hasEvict, 0U);
    Handover(flow, ref, memoryMeta, diskMeta);
    EXPECT_EQ(Meta(flow, WCACHE_DISK, 0).hasEvict, 1U);
}

TEST_F(TestWCacheGlobalEvict, faulted_handover_reclaims_memory_without_disk_marker_write)
{
    auto flow = MemoryWriter();
    char key[] = "fault-marker";
    WCacheSliceRefPtr ref;
    ASSERT_EQ(PutValue(flow, key, 0, 'A', ref), BIO_OK);
    WCacheSlicePtr memoryMeta, diskMeta;
    PrepareHandover(flow, ref, memoryMeta, diskMeta);
    ASSERT_TRUE(ref->Aquire());
    Handover(flow, ref, memoryMeta, nullptr);
    ASSERT_EQ(manager.Delete(flow->GetPtId(), key), BIO_OK);
    flow->SetStandaloneFault();
    ref->Release();
    EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetDataEvictOffset(), TEST_SEGMENT);
    EXPECT_EQ(Queued(flow->GetFlowId()), 0U);
    EXPECT_EQ(retryCount, 0U);
    flow->mStandaloneFault = false;
}

TEST_F(TestWCacheGlobalEvict, invalid_memory_tombstone_waits_for_existing_reader)
{
    auto flow = MemoryWriter();
    char key[] = "deleted-with-reader";
    WCacheSliceRefPtr ref;
    ASSERT_EQ(PutValue(flow, key, 0, 'A', ref), BIO_OK);
    ASSERT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetEvictSlice(), ref);
    flow->mCacheTiers[WCACHE_DISK]->mDataFlow = MemoryFlow(flow->GetFlowId(), TEST_SEGMENT);
    ASSERT_TRUE(ref->Aquire());
    ASSERT_EQ(manager.Delete(flow->GetPtId(), key), BIO_OK);
    ASSERT_EQ(flow->EvictMemoryTombstone(ref), BIO_OK);
    EXPECT_NE(ref->GetSlice(), nullptr);
    EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetDataEvictOffset(), 0U);
    EXPECT_EQ(Meta(flow, WCACHE_DISK, 0).hasEvict, 1U);
    ref->Release();
    EXPECT_EQ(ref->GetSlice(), nullptr);
    EXPECT_EQ(flow->mCacheTiers[WCACHE_MEMORY]->GetDataEvictOffset(), TEST_SEGMENT);
}

TEST_F(TestWCacheGlobalEvict, fault_wait_still_waits_for_put_after_batches_finish)
{
    auto id = AddFlow(1);
    auto flow = manager.AcquireWCacheForPut(id);
    ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->BeginEvictBatch(WCACHE_MEMORY));
    std::unordered_map<uint16_t, std::list<WCachePtr>> flows;
    std::unordered_map<uint16_t, std::unordered_set<uint64_t>> ids;
    manager.CollectFaultedFlows(0, flows, ids);
    EXPECT_EQ(manager.AcquireWCacheForPut(id), nullptr);
    uint32_t count = 0;
    EXPECT_EQ(flow->EvictBatch(WCACHE_MEMORY, 32, count), BIO_INNER_RETRY);
    EXPECT_TRUE(flow->IsEvictIoFinish());
    EXPECT_EQ(flow->GetFlyIo(), 1U);
    EXPECT_EQ(manager.WaitFaultedFlowsIdle(0, flows.at(flow->GetPtId())), BIO_INNER_RETRY);
    flow->DecFlyIo();
    EXPECT_EQ(manager.WaitFaultedFlowsIdle(0, flows.at(flow->GetPtId())), BIO_OK);
    flow->mStandaloneFault = false;
}

TEST_F(TestWCacheGlobalEvict, throwing_batch_returns_its_count_and_allows_retry)
{
    auto id = AddFlow(1);
    auto flow = manager.mWCacheManager.at(id);
    flow->mLocRole = [](uint16_t, bool &) -> BResult { throw std::bad_alloc(); };
    ASSERT_TRUE(flow->BeginEvictBatch(WCACHE_DISK));
    uint32_t count = 0;
    EXPECT_EQ(flow->EvictBatch(WCACHE_DISK, 32, count), BIO_ALLOC_FAIL);
    EXPECT_TRUE(flow->IsEvictIoFinish());
    EXPECT_EQ(flow->GetFlyIo(), 0U);
    flow->mLocRole = [](uint16_t, bool &) -> BResult { throw std::runtime_error("eviction test failure"); };
    ASSERT_TRUE(flow->BeginEvictBatch(WCACHE_DISK));
    EXPECT_EQ(flow->EvictBatch(WCACHE_DISK, 32, count), BIO_INNER_ERR);
    EXPECT_TRUE(flow->IsEvictIoFinish());
    EXPECT_EQ(flow->GetFlyIo(), 0U);
    flow->mLocRole = [](uint16_t, bool &master) { master = true; return BIO_OK; };
    Run();
    EXPECT_EQ(Queued(id), 0U);
    EXPECT_TRUE(flow->IsEvictIoFinish());
}

TEST_F(TestWCacheGlobalEvict, tombstones_consume_the_same_batch_budget)
{
    auto id = AddFlow(100, true);
    auto &flow = manager.mWCacheManager.at(id);
    for (const auto &ref : flow->mCacheTiers[WCACHE_DISK]->mEvictSliceQueue) {
        ref->SetState(SLICE_INVALID);
    }
    Run();
    EXPECT_EQ(Queued(id), 68U);
    EXPECT_EQ(FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_DISK, 0), 68 * TEST_SEGMENT);
}

TEST_F(TestWCacheGlobalEvict, no_disk_history_uses_memory_watermark_without_forcing_drain)
{
    manager.mHasDiskCache = false;
    Config().hasDiskCache = false;
    Config().wcacheMemEvictLevel = 3;
    auto first = AddFlow(3, true);
    auto second = AddFlow(2, true);
    Run();
    EXPECT_EQ(Queued(first, WCACHE_MEMORY), 1U);
    EXPECT_EQ(Queued(second, WCACHE_MEMORY), 2U);
    EXPECT_EQ(FlowManager::GetCacheUsedSize(FLOW_WCACHE, FLOW_MEMORY, 0), 3 * TEST_SEGMENT);
}

TEST_F(TestWCacheGlobalEvict, direct_underfs_pending_slice_waits_for_index_publication)
{
    manager.mHasDiskCache = false;
    Config().hasDiskCache = false;
    auto id = AddFlow(1, false, 0, 1, false);
    auto flow = manager.mWCacheManager.at(id);
    flow->mDirectUnderFs = true;
    auto ref = flow->mCacheTiers[WCACHE_MEMORY]->mEvictSliceQueue.front();

    EXPECT_EQ(flow->EvictFromMemToUnderFs(ref), BIO_INNER_RETRY);
    Run();
    EXPECT_EQ(Queued(id, WCACHE_MEMORY), 1U);
    EXPECT_EQ(ref->GetState(), SLICE_PENDING);
    EXPECT_NE(ref->GetSlice(), nullptr);
    EXPECT_TRUE(deletedFlows.empty());
    EXPECT_TRUE(flow->IsEvictIoFinish());
}

TEST_F(TestWCacheGlobalEvict, compact_metadata_truncation_uses_entry_size)
{
    manager.mHasDiskCache = false;
    Config().hasDiskCache = false;
    auto id = AddFlow(2);
    auto tier = manager.mWCacheManager.at(id)->mCacheTiers[WCACHE_MEMORY];
    tier->mMetaEntrySize = sizeof(WFlowCompactSliceMeta);
    tier->mMetaFlow->mWrittenOffset = 2 * tier->mMetaEntrySize;
    tier->mMetaFlow->mPreLoadOffset = 2 * tier->mMetaEntrySize;
    auto first = tier->GetEvictSlice();
    auto second = tier->GetEvictSlice();

    ASSERT_EQ(tier->Evict(second->GetSlice()), BIO_OK);
    EXPECT_EQ(tier->GetMetaEvictOffset(), 0U);
    ASSERT_EQ(tier->Evict(first->GetSlice()), BIO_OK);
    EXPECT_EQ(tier->GetMetaEvictOffset(), 2 * sizeof(WFlowCompactSliceMeta));
    EXPECT_EQ(tier->GetDataEvictOffset(), 2 * TEST_SEGMENT);
}

TEST_F(TestWCacheGlobalEvict, repeated_triggers_coalesce_while_a_disk_batch_is_running)
{
    auto id = AddFlow(2, true);
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    manager.mWCacheManager.at(id)->mLocRole = [&](uint16_t, bool &master) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
        master = true;
        return BIO_OK;
    };
    manager.mEvictService[WCACHE_DISK] = ExecutorService::Create(2, 128);
    ASSERT_TRUE(manager.mEvictService[WCACHE_DISK]->Start());
    manager.ScheduleGlobalEvict(WCACHE_DISK);
    bool started;
    {
        std::unique_lock<std::mutex> lock(mutex);
        started = cv.wait_for(lock, std::chrono::seconds(5), [&] { return entered; });
    }
    for (unsigned i = 0; i < 64; ++i) {
        manager.ScheduleGlobalEvict(WCACHE_DISK);
    }
    {
        ReadLocker<ReadWriteLock> lock(&manager.mWCacheManagerLock);
        EXPECT_TRUE(manager.mGlobalEvictQueues.at(Domain(WCACHE_DISK, 0)).scheduled);
        EXPECT_EQ(manager.mWCacheManager.at(id)->GetFlyIo(), 0U);
        EXPECT_FALSE(manager.mWCacheManager.at(id)->IsEvictIoFinish());
    }
    manager.mRunning = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
        cv.notify_all();
    }
    manager.mEvictService[WCACHE_DISK]->Stop();
    EXPECT_TRUE(started);
    EXPECT_EQ(deletedFlows.size(), 2U);
}

TEST(WCacheGlobalRecoveryCursor, nonzero_start_and_holes_do_not_block_contiguous_truncate)
{
    WFlowTruncateCursor cursor(100);
    cursor.MarkEvictedIndex(101);
    std::vector<FlowAddr> addrs;
    auto later = MakeRef<WCacheSlice>(1, 102 * TEST_SEGMENT, 102, TEST_SEGMENT, addrs, FLOW_DISK);
    auto first = MakeRef<WCacheSlice>(1, 100 * TEST_SEGMENT, 100, TEST_SEGMENT, addrs, FLOW_DISK);
    uint64_t index = 0;
    EXPECT_EQ(cursor.GetTruncateSlice(later, index), nullptr);
    EXPECT_EQ(index, 100U);
    EXPECT_EQ(cursor.GetTruncateSlice(first, index), later);
    EXPECT_EQ(index, 103U);
    EXPECT_EQ(cursor.GetTruncateSlice(first, index), nullptr);
}
}
