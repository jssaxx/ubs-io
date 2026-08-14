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

#include "mms_kv_client.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include "cm.h"
#include "mms_client.h"
#include "mms_client_log.h"
#include "mms_comm.h"
#include "mms_functions.h"
#include "mms_monotonic.h"
#include "mms_trace.h"
#include "securec.h"

namespace ock {
namespace mms {

static thread_local uint16_t g_groupIndex = NumaGroupIndex::Instance()->GetGroupIndex();
static constexpr uint32_t MAX_BATCH_GET_PARALLEL_TASKS = NO_8;
static std::atomic<uint32_t> g_batchGetParallelTasks{0};
static std::atomic<uint32_t> g_directRpcShardSeed{0};
static std::atomic<uint64_t> g_routeCacheIdSeed{0};

struct ClientDirectRpcState {
    NetEnginePtr engine;
    ServiceProtocol protocol{ServiceProtocol::UNKNOWN};
    uint16_t connCount{0};
    uint16_t groupNum{0};

    ~ClientDirectRpcState()
    {
        if (engine != nullptr) {
            engine->Stop();
        }
    }
};

struct ThreadRouteViewCache {
    const MmsKvClient *owner{nullptr};
    uint64_t cacheId{0};
    uint64_t generation{UINT64_MAX};
    std::shared_ptr<const ClientRouteView> route;
};

struct ThreadDirectRpcShard {
    const MmsKvClient *owner{nullptr};
    uint32_t shard{0};
};

static thread_local ThreadRouteViewCache g_routeViewCache;
static thread_local ThreadDirectRpcShard g_directRpcShard;

static void DirectRpcLog(int level, const char *msg)
{
    if (MmsClientLog::Instance() != nullptr) {
        MmsClientLog::Instance()->Log(level, msg);
    }
}

static std::string RepeatDirectRpcOption(const std::string &value, uint16_t count)
{
    std::string result;
    for (uint16_t index = 0; index < count; ++index) {
        if (!result.empty()) {
            result += ",";
        }
        result += value;
    }
    return result;
}

static std::string BuildDirectRpcCpuSet(uint16_t count)
{
    std::string result;
    for (uint16_t index = 0; index < count; ++index) {
        if (!result.empty()) {
            result += ",";
        }
        result += std::to_string(index) + "-" + std::to_string(index);
    }
    return result;
}

static bool TryAcquireBatchGetParallelTask()
{
    uint32_t current = g_batchGetParallelTasks.load(std::memory_order_relaxed);
    while (current < MAX_BATCH_GET_PARALLEL_TASKS) {
        if (g_batchGetParallelTasks.compare_exchange_weak(current, current + NO_1, std::memory_order_acquire,
                                                          std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

static void ReleaseBatchGetParallelTask()
{
    g_batchGetParallelTasks.fetch_sub(NO_1, std::memory_order_release);
}

struct SendResultContext {
    uint32_t itemIndex;
    BResult failedRet;
    bool withValue;
};

static uint32_t FillPutItemResultsAfterSend(PutItems *itemList, const std::vector<IOCtxItem> &ctxItems,
                                            const SendResultContext &context)
{
    uint32_t itemIndex = context.itemIndex;
    uint32_t ctxItemNum = static_cast<uint32_t>(ctxItems.size());
    for (uint32_t ctxIndex = 0; ctxIndex < ctxItemNum; ctxIndex++) {
        auto *req = reinterpret_cast<IoDataRequest *>(ctxItems[ctxIndex].buff);
        uint64_t offset = sizeof(IoDataRequest);
        for (uint32_t index = 0; index < req->num; index++) {
            auto *desc = reinterpret_cast<IoLocDesc *>(ctxItems[ctxIndex].buff + offset);
            BResult result = (desc->result == MMS_MAX) ? context.failedRet : desc->result;
            uintptr_t valueAddr = (desc->result == MMS_MAX) ? 0 : desc->valueAddr;
            *itemList[itemIndex].result = result;
            *itemList[itemIndex].valueAddr = reinterpret_cast<char *>(valueAddr);
            offset += sizeof(IoLocDesc) + desc->keyLen;
            if (context.withValue) {
                offset += desc->valueLen;
            }
            itemIndex++;
        }
    }
    return itemIndex;
}

template <typename ItemType>
static uint32_t FillNoValueItemResultsAfterSend(ItemType *itemList, const std::vector<IOCtxItem> &ctxItems,
                                                const SendResultContext &context)
{
    uint32_t itemIndex = context.itemIndex;
    uint32_t ctxItemNum = static_cast<uint32_t>(ctxItems.size());
    for (uint32_t ctxIndex = 0; ctxIndex < ctxItemNum; ctxIndex++) {
        auto *req = reinterpret_cast<IoDataRequest *>(ctxItems[ctxIndex].buff);
        uint64_t offset = sizeof(IoDataRequest);
        for (uint32_t index = 0; index < req->num; index++) {
            auto *desc = reinterpret_cast<IoLocDesc *>(ctxItems[ctxIndex].buff + offset);
            *itemList[itemIndex].result = (desc->result == MMS_MAX) ? context.failedRet : desc->result;
            offset += sizeof(IoLocDesc) + desc->keyLen;
            if (context.withValue) {
                offset += desc->valueLen;
            }
            itemIndex++;
        }
    }
    return itemIndex;
}

struct EncodeBufferCache {
    MmsMemAllocatorPtr allocator{nullptr};
    uintptr_t buff{0};
    uint64_t size{0};
    uint16_t numaId{0};
    bool borrowed{false};

    ~EncodeBufferCache()
    {
        Reset();
    }

    void Reset()
    {
        if (buff != 0 && allocator != nullptr) {
            allocator->MmsFree(buff);
        }
        allocator = nullptr;
        buff = 0;
        size = 0;
        numaId = 0;
        borrowed = false;
    }

    BResult Alloc(const MmsMemAllocatorPtr &curAllocator, uint64_t reqSize, uint16_t &outNumaId, uintptr_t &blockAddr)
    {
        if (UNLIKELY(curAllocator == nullptr)) {
            return MMS_ALLOC_FAIL;
        }
        if (borrowed) {
            return curAllocator->MmsAlloc(reqSize, outNumaId, blockAddr);
        }
        if (buff != 0 && (allocator != curAllocator || size < reqSize)) {
            Reset();
        }
        if (buff == 0) {
            auto ret = curAllocator->MmsAlloc(reqSize, numaId, buff);
            if (UNLIKELY(ret != MMS_OK)) {
                return ret;
            }
            allocator = curAllocator;
            size = reqSize;
        }
        borrowed = true;
        outNumaId = numaId;
        blockAddr = buff;
        return MMS_OK;
    }

    uint64_t BorrowedBuff() const
    {
        return borrowed ? buff : 0;
    }

    void Release()
    {
        borrowed = false;
    }
};

static thread_local EncodeBufferCache g_encodeBufferCache;

struct GetOneSideBufferCache {
    MmsMemAllocatorPtr allocator{nullptr};
    uintptr_t buff{0};
    uint64_t size{0};
    uint16_t numaId{0};
    bool direct{false};

    ~GetOneSideBufferCache()
    {
        Reset();
    }

    void Reset()
    {
        if (buff != 0 && allocator != nullptr) {
            if (direct) {
                allocator->MmsFreeDirect(buff);
            } else {
                allocator->MmsFree(buff);
            }
        }
        allocator = nullptr;
        buff = 0;
        size = 0;
        numaId = 0;
        direct = false;
    }

    BResult Reserve(const MmsMemAllocatorPtr &curAllocator, uint64_t reqSize, uintptr_t &blockAddr,
                    bool directAlloc = false)
    {
        if (UNLIKELY(curAllocator == nullptr || reqSize == 0)) {
            return MMS_ALLOC_FAIL;
        }
        if (buff != 0 && (allocator != curAllocator || size < reqSize || direct != directAlloc)) {
            Reset();
        }
        if (buff == 0) {
            auto ret = directAlloc ? curAllocator->MmsAllocDirect(reqSize, numaId, buff) :
                                     curAllocator->MmsAlloc(reqSize, numaId, buff);
            if (UNLIKELY(ret != MMS_OK)) {
                return ret;
            }
            allocator = curAllocator;
            size = reqSize;
            direct = directAlloc;
        }
        blockAddr = buff;
        return MMS_OK;
    }
};

struct BatchGetChunk {
    uint16_t targetNid{INVALID_NID};
    std::vector<uint32_t> itemIndexes;
    uint32_t responseCapacity{0};
};

class BatchGetWorker {
public:
    BatchGetWorker()
    {
        InitAffinity();
        mThread = std::thread([this]() { Run(); });
    }

    ~BatchGetWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mStopping = true;
        }
        mCondition.notify_one();
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    void Submit(std::function<BResult()> task)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mTask = std::move(task);
        mResult.store(MMS_OK, std::memory_order_relaxed);
        mDone.store(false, std::memory_order_relaxed);
        mHasTask = true;
        mCondition.notify_one();
    }

    BResult Wait()
    {
        while (!mDone.load(std::memory_order_acquire)) {
            CPU_RELAX();
        }
        return static_cast<BResult>(mResult.load(std::memory_order_relaxed));
    }

private:
    void InitAffinity()
    {
        cpu_set_t processAffinity;
        if (sched_getaffinity(getpid(), sizeof(processAffinity), &processAffinity) != 0) {
            return;
        }

        int currentCpu = sched_getcpu();
        if (currentCpu < 0) {
            return;
        }
        uint16_t currentNuma = NumaManager::Instance().GetCPUNumaNode(static_cast<uint32_t>(currentCpu));
        std::vector<uint16_t> numaCpus;
        uint32_t cpuNum = std::min<uint32_t>(GetDeviceCpuNum(), CPU_SETSIZE);
        for (uint32_t cpu = 0; cpu < cpuNum; ++cpu) {
            if (CPU_ISSET(cpu, &processAffinity) && NumaManager::Instance().GetCPUNumaNode(cpu) == currentNuma) {
                numaCpus.push_back(static_cast<uint16_t>(cpu));
            }
        }
        if (numaCpus.size() <= NO_1) {
            return;
        }

        auto current = std::find(numaCpus.begin(), numaCpus.end(), static_cast<uint16_t>(currentCpu));
        size_t currentIndex = (current == numaCpus.end()) ? 0 : static_cast<size_t>(current - numaCpus.begin());
        uint16_t workerCpu = numaCpus[(currentIndex + numaCpus.size() / NO_2) % numaCpus.size()];
        CPU_ZERO(&mAffinity);
        CPU_SET(workerCpu, &mAffinity);
        mHasAffinity = true;
    }

    void Run()
    {
        if (mHasAffinity) {
            (void)pthread_setaffinity_np(pthread_self(), sizeof(mAffinity), &mAffinity);
        }
        while (true) {
            std::function<BResult()> task;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mCondition.wait(lock, [this]() { return mStopping || mHasTask; });
                if (mStopping) {
                    return;
                }
                task = std::move(mTask);
                mHasTask = false;
            }
            mResult.store(task(), std::memory_order_relaxed);
            mDone.store(true, std::memory_order_release);
        }
    }

private:
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::thread mThread;
    std::function<BResult()> mTask;
    std::atomic<int32_t> mResult{MMS_OK};
    std::atomic<bool> mDone{true};
    cpu_set_t mAffinity{};
    bool mHasAffinity{false};
    bool mHasTask{false};
    bool mStopping{false};
};

static uint64_t BatchGetResponseMetaSize(uint32_t itemNum)
{
    return sizeof(BatchGetResponse) + static_cast<uint64_t>(itemNum) * sizeof(BatchGetItemResponse);
}

template <typename Item>
static uint64_t CalcValueEncodeLen(const Item *itemList, uint32_t itemNum)
{
    uint64_t len = sizeof(IoDataRequest);
    for (uint32_t index = 0; index < itemNum; ++index) {
        len += sizeof(IoLocDesc) + itemList[index].keyLen + NO_1 + itemList[index].valueLen;
    }
    return len;
}

static uint64_t CalcDeleteEncodeLen(const DeleteItems *itemList, uint32_t itemNum)
{
    uint64_t len = sizeof(IoDataRequest);
    for (uint32_t index = 0; index < itemNum; ++index) {
        len += sizeof(IoLocDesc) + itemList[index].keyLen + NO_1;
    }
    return len;
}

static uint32_t CalcEncodeBuffLen(uint32_t configuredLen, uint64_t requiredLen)
{
    uint64_t len = std::max<uint64_t>(configuredLen, requiredLen);
    if (UNLIKELY(len > UINT32_MAX)) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(len);
}

BResult MmsKvClient::Initialize(const KvClientPara &para)
{
    mCache = para.cache;
    mNetEngine = para.netEngine;
    mMemMgr = para.memMgr;
    mMemAllocator = para.memAllocator;
    mIoTimeOut = para.ioTimeOut;
    mMaxMsgBuffSize = para.maxMsgBuffSize;
    mClientGeneration = para.clientGeneration;
    mKeyRouteEnabled = para.keyRouteEnabled;
    mClientOptions = para.clientOptions;
    mRouteCacheId = g_routeCacheIdSeed.fetch_add(NO_1, std::memory_order_relaxed) + NO_1;

    UpdateLocalPtVersion(mPtVersion);
    if (mKeyRouteEnabled) {
        auto ret = RefreshRouteView();
        if (UNLIKELY(ret != MMS_OK)) {
            CLIENT_LOG_WARN("Refresh route view failed, ret:" << ret << ". Get will fallback to local proxy.");
        } else {
            ret = StartDirectRpc(LoadRouteView());
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_WARN("Start direct rpc failed, ret:" << ret << ". Get will fallback to local proxy.");
            }
        }
    }
    return MMS_OK;
}

BResult MmsKvClient::StartDirectRpc(const std::shared_ptr<const ClientRouteView> &route)
{
    if (UNLIKELY(route == nullptr || route->localNid >= MAX_NODES_NUM ||
                 route->rpcConnCount == 0 || route->rpcGroupNum == 0 ||
                 route->rpcGroupNum > MAX_GROUPS_NUM)) {
        return MMS_INVALID_PARAM;
    }

    auto protocol = static_cast<ServiceProtocol>(route->rpcProtocol);
    if (UNLIKELY(!NetEngine::IsRpcProtocolSupported(protocol))) {
        CLIENT_LOG_WARN("Direct rpc does not support protocol:" << route->rpcProtocol << ".");
        return MMS_INVALID_PARAM;
    }

    const auto &localNode = route->nodes[route->localNid];
    size_t ipLen = strnlen(localNode.ip, IP_SIZE);
    if (UNLIKELY(ipLen == 0 || ipLen >= IP_SIZE)) {
        CLIENT_LOG_ERROR("Invalid local rpc ip in route view, local nid:" << route->localNid << ".");
        return MMS_INVALID_PARAM;
    }

    NetMemList memList{};
    uint64_t ioCtxAddr = 0;
    uint64_t ioCtxSize = 0;
    auto ret = mMemMgr->GetAreaMemDesc(MMAP_AREA_IOCTX, ioCtxAddr, ioCtxSize);
    if (UNLIKELY(ret != MMS_OK || ioCtxAddr == 0 || ioCtxSize == 0)) {
        CLIENT_LOG_ERROR("Get client ioctx memory failed, ret:" << ret << ", size:" << ioCtxSize << ".");
        return ret == MMS_OK ? MMS_NOT_READY : ret;
    }
    memList.num = NO_1;
    memList.address[NO_0] = ioCtxAddr;
    memList.size[NO_0] = ioCtxSize;

    std::shared_ptr<const ClientDirectRpcState> state;
    {
        std::lock_guard<std::mutex> lock(mDirectRpcLock);
        state = LoadDirectRpcState();
        if (state != nullptr) {
            if (state->protocol != protocol || state->connCount != route->rpcConnCount ||
                state->groupNum != route->rpcGroupNum) {
                CLIENT_LOG_WARN("Direct rpc configuration changed, rebuild is required.");
                return MMS_NOT_READY;
            }
        } else {
            auto directEngine = MakeRef<NetEngine>();
            if (UNLIKELY(directEngine == nullptr)) {
                return MMS_ALLOC_FAIL;
            }

            NetEngineInitOptions initOptions;
            initOptions.timeoutSec = static_cast<int16_t>(mIoTimeOut);
            initOptions.logFunc = DirectRpcLog;
            initOptions.memList = memList;
            initOptions.startRequestExecutor = false;
            ret = directEngine->Initialize(initOptions);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Initialize direct rpc engine failed, ret:" << ret << ".");
                return ret;
            }

            auto brokenHandler = [](uint32_t nodeId, uint32_t pid) {
                CLIENT_LOG_WARN("Direct rpc channel broken, node id:" << nodeId << ", pid:" << pid << ".");
            };
            ret = directEngine->RegisterChannelBrokenHandler(brokenHandler);
            if (UNLIKELY(ret != MMS_OK)) {
                directEngine->Stop();
                return ret;
            }

            NetOptions netOptions;
            netOptions.ipMask = std::string(localNode.ip, ipLen) + "/32";
            netOptions.protocol = protocol;
            netOptions.connCount = route->rpcConnCount;
            netOptions.isBusyPolling = false;
            netOptions.workerGroups = RepeatDirectRpcOption("1", route->rpcGroupNum);
            netOptions.workerGroupsCpuSet = BuildDirectRpcCpuSet(route->rpcGroupNum);
            netOptions.workerGroupsNum = route->rpcGroupNum;
            netOptions.role = NET_CLIENT;
            netOptions.tlsEnable = mClientOptions.tlsEnable != 0;
            netOptions.certificationPath = mClientOptions.certificationPath;
            netOptions.caCerPath = mClientOptions.caCerPath;
            netOptions.caCrlPath = mClientOptions.caCrlPath;
            netOptions.privateKeyPath = mClientOptions.privateKeyPath;
            netOptions.privateKeyPasswordPath = mClientOptions.privateKeyPasswordPath;
            netOptions.decrypterLibPath = mClientOptions.decrypterLibPath;
            netOptions.opensslLibDir = mClientOptions.opensslLibDir;
            ret = directEngine->Start(netOptions);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Start direct rpc engine failed, ret:" << ret << ".");
                directEngine->Stop();
                return ret;
            }

            auto newState = std::make_shared<ClientDirectRpcState>();
            newState->engine = directEngine;
            newState->protocol = protocol;
            newState->connCount = route->rpcConnCount;
            newState->groupNum = route->rpcGroupNum;
            state = newState;
            std::atomic_store_explicit(&mDirectRpcState, state, std::memory_order_release);
        }
    }

    EnsureDirectConnections(route, state);
    return MMS_OK;
}

BResult MmsKvClient::EnsureDirectConnection(const std::shared_ptr<const ClientRouteView> &route, uint16_t targetNid,
                                            const std::shared_ptr<const ClientDirectRpcState> &state,
                                            uint16_t groupIndex)
{
    if (UNLIKELY(route == nullptr || state == nullptr || state->engine == nullptr ||
                 targetNid >= MAX_NODES_NUM || groupIndex >= state->groupNum)) {
        return MMS_NOT_READY;
    }

    if (state->engine->CheckConnect(targetNid, groupIndex) == MMS_OK) {
        return MMS_OK;
    }

    const auto &node = route->nodes[targetNid];
    size_t ipLen = strnlen(node.ip, IP_SIZE);
    if (UNLIKELY(node.nodeId != targetNid || node.status != CM_NODE_NORMAL || node.port == 0 ||
                 ipLen == 0 || ipLen >= IP_SIZE)) {
        return MMS_NOT_READY;
    }

    std::lock_guard<std::mutex> lock(mDirectConnectLocks[targetNid]);
    if (LoadDirectRpcState() != state) {
        return MMS_NET_RETRY;
    }
    if (state->engine->CheckConnect(targetNid, groupIndex) == MMS_OK) {
        return MMS_OK;
    }

    ConnectInfo info({route->localNid, static_cast<uint32_t>(getpid()), targetNid,
                      std::string(node.ip, ipLen), node.port, NO_1});
    info.isSelfPoll = true;
    auto ret = state->engine->SyncConnect(info);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_WARN("Connect direct rpc node failed, ret:" << ret << ", node id:" << targetNid
                                                               << ", ip:" << node.ip << ".");
    } else {
        CLIENT_LOG_INFO("Connect direct rpc node success, node id:" << targetNid << ", ip:" << node.ip << ".");
    }
    return ret;
}

void MmsKvClient::EnsureDirectConnections(const std::shared_ptr<const ClientRouteView> &route,
                                          const std::shared_ptr<const ClientDirectRpcState> &state)
{
    if (route == nullptr || state == nullptr) {
        return;
    }
    for (uint16_t nodeId = 0; nodeId < MAX_NODES_NUM; ++nodeId) {
        const auto &node = route->nodes[nodeId];
        if (node.nodeId != nodeId || node.status != CM_NODE_NORMAL || node.ip[0] == '\0') {
            continue;
        }
        for (uint16_t groupIndex = 0; groupIndex < state->groupNum; ++groupIndex) {
            (void)EnsureDirectConnection(route, nodeId, state, groupIndex);
        }
    }
}

std::shared_ptr<const ClientDirectRpcState> MmsKvClient::LoadDirectRpcState() const
{
    return std::atomic_load_explicit(&mDirectRpcState, std::memory_order_acquire);
}

uint16_t MmsKvClient::SelectDirectRpcGroup(const std::shared_ptr<const ClientDirectRpcState> &state) const
{
    if (state == nullptr || state->groupNum == 0) {
        return NO_0;
    }
    if (g_directRpcShard.owner != this) {
        g_directRpcShard.owner = this;
        int cpuId = sched_getcpu();
        g_directRpcShard.shard = cpuId >= 0 ? static_cast<uint32_t>(cpuId) :
            g_directRpcShardSeed.fetch_add(NO_1, std::memory_order_relaxed);
    }
    return static_cast<uint16_t>(g_directRpcShard.shard % state->groupNum);
}

void MmsKvClient::StopDirectRpc()
{
    std::shared_ptr<const ClientDirectRpcState> state;
    {
        std::lock_guard<std::mutex> lock(mDirectRpcLock);
        state = std::atomic_exchange_explicit(&mDirectRpcState,
                                              std::shared_ptr<const ClientDirectRpcState>(),
                                              std::memory_order_acq_rel);
    }
    state.reset();
}

BResult MmsKvClient::Rebuild(void)
{
    if (!mKeyRouteEnabled) {
        return MMS_OK;
    }
    StopDirectRpc();
    auto ret = RefreshRouteView();
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }
    return StartDirectRpc(LoadRouteView());
}

void MmsKvClient::Exit(void)
{
    StopDirectRpc();
}

BResult MmsKvClient::RefreshRouteView(const std::shared_ptr<const ClientRouteView> &observedRoute)
{
    std::lock_guard<std::mutex> lock(mRouteRefreshLock);
    if (observedRoute != nullptr && LoadPublishedRouteView() != observedRoute) {
        return MMS_OK;
    }

    BasicRequest req = {{0, MMS_OP_C_GET_ROUTE_VIEW, 0, 0, 0}};
    RouteViewResponse rsp{};
    auto ret = mNetEngine->SyncCall<BasicRequest, RouteViewResponse>(INVALID_NID, 0, MMS_OP_C_GET_ROUTE_VIEW, req, rsp);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Get route view failed, ret:" << ret << ".");
        return ret;
    }

    if (UNLIKELY(rsp.ptNum == 0 || rsp.ptNum > MMS_ROUTE_MAX_PT_NUM || rsp.nodeNum > MAX_NODES_NUM)) {
        CLIENT_LOG_ERROR("Invalid route view, pt num:" << rsp.ptNum << ", node num:" << rsp.nodeNum << ".");
        return MMS_INVALID_PARAM;
    }

    auto route = std::make_shared<ClientRouteView>();
    route->localNid = rsp.localNid;
    route->ptNum = rsp.ptNum;
    route->replicaNum = rsp.replicaNum;
    route->rpcProtocol = rsp.rpcProtocol;
    route->rpcConnCount = rsp.rpcConnCount;
    route->rpcGroupNum = rsp.rpcGroupNum;
    route->ptVersion = rsp.ptVersion;
    for (uint16_t index = 0; index < rsp.nodeNum; index++) {
        if (rsp.nodes[index].nodeId < MAX_NODES_NUM) {
            route->nodes[rsp.nodes[index].nodeId] = rsp.nodes[index];
        }
    }
    for (uint16_t index = 0; index < rsp.ptNum; index++) {
        if (rsp.pts[index].ptId < MMS_ROUTE_MAX_PT_NUM) {
            route->pts[rsp.pts[index].ptId] = rsp.pts[index];
        }
    }

    HandleUpdatePtVersion(rsp.ptVersion);
    std::shared_ptr<const ClientRouteView> publishedRoute = route;
    std::atomic_store_explicit(&mRouteView, publishedRoute, std::memory_order_release);
    mRouteGeneration.fetch_add(NO_1, std::memory_order_release);
    auto directState = LoadDirectRpcState();
    if (directState != nullptr) {
        auto protocol = static_cast<ServiceProtocol>(publishedRoute->rpcProtocol);
        if (directState->protocol != protocol || directState->connCount != publishedRoute->rpcConnCount ||
            directState->groupNum != publishedRoute->rpcGroupNum) {
            StopDirectRpc();
            auto startRet = StartDirectRpc(publishedRoute);
            if (UNLIKELY(startRet != MMS_OK)) {
                CLIENT_LOG_WARN("Rebuild direct rpc after route refresh failed, ret:" << startRet <<
                    ". Get will fallback to local proxy.");
            }
        } else {
            EnsureDirectConnections(publishedRoute, directState);
        }
    }

    return MMS_OK;
}

const std::shared_ptr<const ClientRouteView> &MmsKvClient::LoadRouteView() const
{
    uint64_t generation = mRouteGeneration.load(std::memory_order_acquire);
    if (g_routeViewCache.owner != this || g_routeViewCache.cacheId != mRouteCacheId ||
        g_routeViewCache.generation != generation) {
        g_routeViewCache.owner = this;
        g_routeViewCache.cacheId = mRouteCacheId;
        g_routeViewCache.route = LoadPublishedRouteView();
        g_routeViewCache.generation = generation;
    }
    return g_routeViewCache.route;
}

std::shared_ptr<const ClientRouteView> MmsKvClient::LoadPublishedRouteView() const
{
    return std::atomic_load_explicit(&mRouteView, std::memory_order_acquire);
}

BResult MmsKvClient::ResolveGetRoute(const std::shared_ptr<const ClientRouteView> &route, const char *key,
                                     uint16_t keyLen, uint16_t &localNid, uint16_t &targetNid, uint16_t &ptId,
                                     uint64_t &ptv, bool &localOwner)
{
    if (route == nullptr || route->ptNum == 0 || key == nullptr || keyLen == 0 || keyLen > MAX_KEY_LENGTH) {
        return MMS_INNER_RETRY;
    }

    localNid = route->localNid;
    ptId = Cm::HashKeyToPt(key, keyLen, route->ptNum);
    if (UNLIKELY(ptId >= MMS_ROUTE_MAX_PT_NUM)) {
        return MMS_INVALID_PARAM;
    }

    const auto &pt = route->pts[ptId];
    if (pt.ptId != ptId || pt.state != CM_PT_NORMAL || pt.copyNum == 0) {
        return MMS_INNER_RETRY;
    }

    targetNid = pt.masterNodeId;
    localOwner = (targetNid == localNid);
    if (UNLIKELY(!localOwner && (targetNid >= MAX_NODES_NUM || route->nodes[targetNid].status != 0))) {
        return MMS_INNER_RETRY;
    }
    ptv = route->ptVersion;
    return MMS_OK;
}

BResult MmsKvClient::ResolveGetRouteWithRefresh(const char *key, uint16_t keyLen, uint16_t &localNid,
                                                 uint16_t &targetNid, uint16_t &ptId, uint64_t &ptv,
                                                 bool &localOwner)
{
    const auto &route = LoadRouteView();
    auto ret = ResolveGetRoute(route, key, keyLen, localNid, targetNid, ptId, ptv, localOwner);
    if (LIKELY(ret == MMS_OK) || ret != MMS_INNER_RETRY) {
        return ret;
    }

    auto observedRoute = route;
    ret = RefreshRouteView(observedRoute);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }
    return ResolveGetRoute(LoadRouteView(), key, keyLen, localNid, targetNid, ptId, ptv, localOwner);
}

BResult MmsKvClient::SendGetByProxy(GetItems &item, GetValueRequest &req)
{
    bool useOneSide = item.length > MMS_TWOSIDE_IO_THRESHOLD;
    static thread_local GetOneSideBufferCache getOneSideCache;
    uintptr_t oneSideBuff = 0;
    if (useOneSide) {
        MMS_TRACE_START(SDK_TRACE_GET_PREPARE);
        auto ret = getOneSideCache.Reserve(mMemAllocator, item.length, oneSideBuff, true);
        if (UNLIKELY(ret != MMS_OK)) {
            MMS_TRACE_END(SDK_TRACE_GET_PREPARE, ret);
            CLIENT_LOG_ERROR("Alloc get one-side buffer failed, ret:" << ret << ", len:" << item.length << ".");
            return ret;
        }
        size_t responseOffset = 0;
        mMemMgr->Trans2Offset(MMAP_AREA_IOCTX, oneSideBuff, responseOffset);
        req.valueAddr = responseOffset;
        req.valueKey = {};
        req.flags = (req.flags & ~MMS_GET_FLAG_ONESIDE) | MMS_GET_FLAG_PROXY_BUFFER;
        MMS_TRACE_END(SDK_TRACE_GET_PREPARE, MMS_OK);
    } else {
        req.valueAddr = 0;
        req.valueKey = {};
        req.flags &= ~(MMS_GET_FLAG_ONESIDE | MMS_GET_FLAG_PROXY_BUFFER | MMS_GET_FLAG_PROXY_FORWARDED);
    }

    static thread_local std::vector<char> rspBuffer;
    uint64_t rspLen = 0;
    uint64_t rspCap = useOneSide ? sizeof(GetValueResponse) : sizeof(GetValueResponse) + item.length;
    if (UNLIKELY(rspBuffer.size() < rspCap)) {
        rspBuffer.resize(rspCap);
    }
    MMS_TRACE_START(SDK_TRACE_GET_REMOTE_CALL);
    auto ret = mNetEngine->SyncCall<GetValueRequest>(INVALID_NID, 0, MMS_OP_C_GET, req, rspBuffer.data(), rspCap,
                                                     rspLen);
    MMS_TRACE_END(SDK_TRACE_GET_REMOTE_CALL, ret);
    if (LIKELY(ret == MMS_OK)) {
        if (UNLIKELY(rspLen < sizeof(GetValueResponse))) {
            CLIENT_LOG_ERROR("Invalid get response, response len:" << rspLen << ".");
            return MMS_ERR;
        }
        auto *rsp = reinterpret_cast<GetValueResponse *>(rspBuffer.data());
        uint64_t expectRspLen = useOneSide ? sizeof(GetValueResponse) : sizeof(GetValueResponse) + rsp->realLength;
        if (UNLIKELY(rsp->realLength > item.length || rspLen < expectRspLen)) {
            CLIENT_LOG_ERROR("Invalid get response, response len:" << rspLen << ".");
            return MMS_ERR;
        }
        if (UNLIKELY(rsp->result != MMS_OK)) {
            if (rsp->result == MMS_NOT_EXISTS) {
                CLIENT_LOG_DEBUG("Get key not found, key:" << item.key << ".");
            }
            return static_cast<BResult>(rsp->result);
        }

        char *value = useOneSide ? reinterpret_cast<char *>(oneSideBuff) : rsp->value;
        if (*item.value == nullptr) {
            *item.value = value;
        } else {
            MMS_TRACE_START(SDK_TRACE_GET_COPY);
            ret = memcpy_s(*item.value, item.length, value, rsp->realLength);
            MMS_TRACE_END(SDK_TRACE_GET_COPY, ret);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Copy get response failed, ret:" << ret << ".");
                return MMS_ERR;
            }
        }
        *item.realLength = static_cast<uint32_t>(rsp->realLength);
    }
    return ret;
}

BResult MmsKvClient::SendGetDirect(GetItems &item, GetValueRequest req, uint16_t targetNid,
                                   const std::shared_ptr<const ClientDirectRpcState> &state, uint16_t groupIndex,
                                   uint64_t ptVersion)
{
    bool useOneSide = item.length > MMS_TWOSIDE_IO_THRESHOLD;
    if (UNLIKELY(state == nullptr || state->engine == nullptr || targetNid >= MAX_NODES_NUM ||
                 groupIndex >= state->groupNum)) {
        return MMS_NET_RETRY;
    }
    req.head.ptv = ptVersion;

    static thread_local GetOneSideBufferCache directGetBufferCache;
    uintptr_t oneSideBuff = 0;
    if (useOneSide) {
        MMS_TRACE_START(SDK_TRACE_GET_PREPARE);
        auto ret = directGetBufferCache.Reserve(mMemAllocator, item.length, oneSideBuff, true);
        if (UNLIKELY(ret != MMS_OK)) {
            MMS_TRACE_END(SDK_TRACE_GET_PREPARE, ret);
            CLIENT_LOG_ERROR("Alloc direct get buffer failed, ret:" << ret << ", len:" << item.length << ".");
            return ret;
        }
        ret = state->engine->GetRegisteredMemoryKey(oneSideBuff, item.length, req.valueKey);
        if (UNLIKELY(ret != MMS_OK)) {
            MMS_TRACE_END(SDK_TRACE_GET_PREPARE, ret);
            CLIENT_LOG_ERROR("Get direct get memory key failed, ret:" << ret << ", len:" << item.length << ".");
            return ret;
        }
        req.valueAddr = oneSideBuff;
        req.flags = (req.flags & ~(MMS_GET_FLAG_PROXY_BUFFER | MMS_GET_FLAG_PROXY_FORWARDED)) |
            MMS_GET_FLAG_ONESIDE | MMS_GET_FLAG_ROUTE_FORWARDED;
        MMS_TRACE_END(SDK_TRACE_GET_PREPARE, MMS_OK);
    } else {
        req.valueAddr = 0;
        req.valueKey = {};
        req.flags = (req.flags & ~(MMS_GET_FLAG_ONESIDE | MMS_GET_FLAG_PROXY_BUFFER |
            MMS_GET_FLAG_PROXY_FORWARDED)) | MMS_GET_FLAG_ROUTE_FORWARDED;
    }
    req.head.groupIndex = groupIndex;

    static thread_local std::vector<char> rspBuffer;
    uint64_t rspCap = useOneSide ? sizeof(GetValueResponse) : sizeof(GetValueResponse) + item.length;
    if (UNLIKELY(rspBuffer.size() < rspCap)) {
        rspBuffer.resize(rspCap);
    }
    uint64_t rspLen = 0;
    MMS_TRACE_START(SDK_TRACE_GET_DIRECT_CALL);
    auto ret = state->engine->SyncCall<GetValueRequest>(targetNid, groupIndex, MMS_OP_C_GET, req, rspBuffer.data(),
                                                        rspCap, rspLen);
    MMS_TRACE_END(SDK_TRACE_GET_DIRECT_CALL, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    if (UNLIKELY(rspLen < sizeof(GetValueResponse))) {
        CLIENT_LOG_ERROR("Invalid direct get response, response len:" << rspLen << ".");
        return MMS_ERR;
    }
    auto *rsp = reinterpret_cast<GetValueResponse *>(rspBuffer.data());
    uint64_t expectRspLen = useOneSide ? sizeof(GetValueResponse) : sizeof(GetValueResponse) + rsp->realLength;
    if (UNLIKELY(rsp->realLength > item.length || rspLen < expectRspLen)) {
        CLIENT_LOG_ERROR("Invalid direct get response, response len:" << rspLen << ".");
        return MMS_ERR;
    }
    if (UNLIKELY(rsp->result != MMS_OK)) {
        if (rsp->result == MMS_NOT_EXISTS) {
            CLIENT_LOG_DEBUG("Direct get key not found, key:" << item.key << ".");
        }
        return static_cast<BResult>(rsp->result);
    }

    char *value = useOneSide ? reinterpret_cast<char *>(oneSideBuff) : rsp->value;
    if (*item.value == nullptr) {
        *item.value = value;
    } else {
        MMS_TRACE_START(SDK_TRACE_GET_COPY);
        ret = memcpy_s(*item.value, item.length, value, rsp->realLength);
        MMS_TRACE_END(SDK_TRACE_GET_COPY, ret);
        if (UNLIKELY(ret != EOK)) {
            CLIENT_LOG_ERROR("Copy direct get response failed, ret:" << ret << ".");
            return MMS_ERR;
        }
    }
    *item.realLength = static_cast<uint32_t>(rsp->realLength);
    return MMS_OK;
}

BResult MmsKvClient::SendSingleReq(IoCtrlRequest &req)
{
    BResult rsp = MMS_OK;
    BResult ret = MMS_OK;
    MmsOpCode opCode = static_cast<MmsOpCode>(req.head.opcode);
    MMS_TRACE_START(NET_TRACE_SYNC_CALL);
    ret = mNetEngine->SyncCall<IoCtrlRequest, BResult>(INVALID_NID, g_groupIndex, opCode, req, rsp);
    MMS_TRACE_END(NET_TRACE_SYNC_CALL, ret);
    if (LIKELY(ret == MMS_OK && rsp == MMS_OK)) {
        return MMS_OK;
    }

    if (ret == MMS_OK) {
        ret = rsp;
    }
    ret = FailHandle(ret, opCode, req, rsp);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Send put request failed, ret:" << ret << ", opCode:" << opCode << ".");
    }

    return ret;
}

void MmsKvClient::FreeBlocks(std::vector<IOCtxItem> &ctxItems, uint64_t keepBuff)
{
    for (auto &item : ctxItems) {
        if (item.buff == keepBuff) {
            continue;
        }
        mMemAllocator->MmsFree(item.buff);
    }
}

BResult MmsKvClient::HandleSendReqs(uint16_t numaId, MmsOpCode opCode, std::vector<IOCtxItem> &ctxItems,
                                    uint64_t keepBuff, bool freeBlocks)
{
    BResult ret;
    uint64_t numaOffset;
    uint32_t ctxItemNum = static_cast<uint32_t>(ctxItems.size());
    for (uint32_t index = 0; index < ctxItemNum; index++) {
        auto &item = ctxItems[index];
        mMemMgr->Trans2Offset(MMAP_AREA_IOCTX, item.buff, numaOffset);
        IoCtrlRequest req = {{0, opCode, 0, 0, mPtVersion.load(std::memory_order_acquire)},
                             mClientGeneration,
                             numaId,
                             numaOffset,
                             item.reqLen};
        ret = SendSingleReq(req);
        if (UNLIKELY(ret != MMS_OK)) {
            CLIENT_LOG_ERROR("Send single request failed, ret:" << ret << ", opCode:" << opCode << ".");
            if (freeBlocks) {
                FreeBlocks(ctxItems, keepBuff);
            }
            return ret;
        }
    }

    if (freeBlocks) {
        FreeBlocks(ctxItems, keepBuff);
    }
    return MMS_OK;
}

BResult MmsKvClient::MmsPut(PutItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    uint16_t numaId = mMemAllocator->GetNumaId();
    auto cachedAllocFunc = [this](uint64_t size, uint16_t &allocNumaId, uintptr_t &blockAddr) {
        return g_encodeBufferCache.Alloc(mMemAllocator, size, allocNumaId, blockAddr);
    };

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        uint32_t encodeBuffLen = CalcEncodeBuffLen(mMaxMsgBuffSize,
            CalcValueEncodeLen(&itemList[curItemIndex], itemNum - curItemIndex));
        MMS_TRACE_START(SDK_TRACE_ENCODE_PUT);
        ret = EncodePutRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, cachedAllocFunc,
                               encodeBuffLen);
        MMS_TRACE_END(SDK_TRACE_ENCODE_PUT, ret);
        uint64_t keepBuff = g_encodeBufferCache.BorrowedBuff();
        if (LIKELY(ret == MMS_OK)) {
            MMS_TRACE_START(SDK_TRACE_PUT_SEND);
            ret = HandleSendReqs(numaId, MMS_OP_C_PUT, ctxItems, keepBuff, false);
            MMS_TRACE_END(SDK_TRACE_PUT_SEND, ret);
            if (LIKELY(ret == MMS_OK)) {
                MMS_TRACE_START(SDK_TRACE_PUT_FILL_RESULT);
                curItemIndex = FillPutItemResults(itemList, curItemIndex, ctxItems);
                MMS_TRACE_END(SDK_TRACE_PUT_FILL_RESULT, MMS_OK);
            }
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            continue;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            MMS_TRACE_START(SDK_TRACE_PUT_SEND);
            ret = HandleSendReqs(numaId, MMS_OP_C_PUT, ctxItems, keepBuff, false);
            MMS_TRACE_END(SDK_TRACE_PUT_SEND, ret);
            if (LIKELY(ret == MMS_OK)) {
                MMS_TRACE_START(SDK_TRACE_PUT_FILL_RESULT);
                curItemIndex = FillPutItemResults(itemList, curItemIndex, ctxItems);
                MMS_TRACE_END(SDK_TRACE_PUT_FILL_RESULT, MMS_OK);
            }
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            CLIENT_LOG_DEBUG("Send batch put success, total send:" << curItemIndex
                                                                   << ", current batch:" << ctxItems.size() << ".");
            continue;
        } else {
            CLIENT_LOG_ERROR("Encode put request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            return ret;
        }
    }

    return MMS_OK;
}

BResult MmsKvClient::MmsGet(GetItems *itemList, uint32_t itemNum)
{
    if (mKeyRouteEnabled && itemNum > NO_1) {
        auto ret = MmsGetBatch(itemList, itemNum);
        if (LIKELY(ret != MMS_NET_RETRY)) {
            return ret;
        }
        CLIENT_LOG_WARN("Batch get fallback to sequential requests, item num:" << itemNum << ".");
    }

    return MmsGetSequential(itemList, itemNum);
}

BResult MmsKvClient::MmsGetSequential(GetItems *itemList, uint32_t itemNum)
{
    for (uint16_t index = 0; index < itemNum; index++) {
        if (mKeyRouteEnabled) {
            auto ret = SendGetReq(itemList[index]);
            *itemList[index].result = ret;
            if (LIKELY(ret == MMS_OK)) {
                continue;
            }
            if (ret == MMS_NOT_EXISTS) {
                CLIENT_LOG_DEBUG("Get remote key not found, key:" << itemList[index].key << ".");
                if (itemNum > NO_1) {
                    continue;
                }
                return ret;
            }
            CLIENT_LOG_ERROR("Get remote failed, ret:" << ret << ", key:" << itemList[index].key << ".");
            return ret;
        }

        uint64_t realLength = 0;
        GetPara para = {itemList[index].key, itemList[index].keyLen, itemList[index].offset, itemList[index].length,
                        itemList[index].value, &realLength};
        auto ret = mCache->Get(para);
        *itemList[index].realLength = static_cast<uint32_t>(realLength);
        *itemList[index].result = ret;
        if (LIKELY(ret == MMS_OK)) {
            continue;
        }

        if (ret == MMS_NOT_EXISTS) {
            ret = SendGetReq(itemList[index]);
            *itemList[index].result = ret;
            if (ret == MMS_OK) {
                continue;
            }
        }

        if (ret == MMS_NOT_EXISTS) {
            CLIENT_LOG_DEBUG("Get key not found, key:" << itemList[index].key << ".");
            if (itemNum > NO_1) {
                continue;
            }
            return ret;
        }
        CLIENT_LOG_ERROR("Get cache failed, ret:" << ret << ", key:" << itemList[index].key << ".");
        return ret;
    }

    return MMS_OK;
}

static BResult BuildBatchGetRequest(const MmsMemMgrPtr &memMgr, uint16_t localNid, uint16_t targetNid,
                                    uint64_t ptv, uint64_t clientGeneration, GetItems *itemList,
                                    const std::vector<uint32_t> &itemIndexes, uintptr_t responseBuff,
                                    uint32_t responseCapacity, std::vector<char> &requestBuff)
{
    uint64_t requestLen = sizeof(BatchGetRequest) + itemIndexes.size() * sizeof(BatchGetItemRequest);
    if (UNLIKELY(itemIndexes.empty() || requestLen > MMS_TWOSIDE_IO_THRESHOLD || requestLen > UINT32_MAX ||
                 responseBuff == 0 || responseCapacity > MMS_ONESIDE_STAGING_SIZE)) {
        return MMS_INVALID_PARAM;
    }

    requestBuff.resize(static_cast<size_t>(requestLen));
    auto *req = reinterpret_cast<BatchGetRequest *>(requestBuff.data());
    *req = {};
    req->head = {localNid, MMS_OP_C_BATCH_GET, 0, 0, ptv};
    req->clientGeneration = clientGeneration;
    req->responseCapacity = responseCapacity;
    req->itemNum = static_cast<uint32_t>(itemIndexes.size());
    req->targetNid = targetNid;
    size_t responseOffset = 0;
    memMgr->Trans2Offset(MMAP_AREA_IOCTX, responseBuff, responseOffset);
    req->responseOffset = responseOffset;

    for (uint32_t index = 0; index < req->itemNum; ++index) {
        const auto &item = itemList[itemIndexes[index]];
        auto &reqItem = req->items[index];
        reqItem.offset = item.offset;
        reqItem.length = item.length;
        reqItem.keyLen = item.keyLen;
        reqItem.reserved = 0;
        auto ret = memcpy_s(reqItem.key, sizeof(reqItem.key), item.key, item.keyLen);
        if (UNLIKELY(ret != EOK)) {
            return MMS_INVALID_PARAM;
        }
        reqItem.key[item.keyLen] = '\0';
    }
    return MMS_OK;
}

static BResult ApplyBatchGetResponse(GetItems *itemList, const std::vector<uint32_t> &itemIndexes,
                                     uintptr_t responseBuff, uint32_t responseCapacity)
{
    auto *rsp = reinterpret_cast<BatchGetResponse *>(responseBuff);
    uint64_t metaSize = BatchGetResponseMetaSize(rsp->itemNum);
    if (UNLIKELY(rsp->itemNum != itemIndexes.size() || metaSize > responseCapacity ||
                 rsp->dataLength < metaSize || rsp->dataLength > responseCapacity)) {
        CLIENT_LOG_ERROR("Invalid batch get response, item num:" << rsp->itemNum << ", data len:" << rsp->dataLength
                                                                  << ", capacity:" << responseCapacity << ".");
        return MMS_ERR;
    }

    MMS_TRACE_START(SDK_TRACE_GET_BATCH_COPY);
    auto copyResult = [&]() -> BResult {
        BResult result = MMS_OK;
        for (uint32_t index = 0; index < rsp->itemNum; ++index) {
            auto &item = itemList[itemIndexes[index]];
            const auto &rspItem = rsp->items[index];
            *item.result = rspItem.result;
            *item.realLength = rspItem.realLength;
            if (rspItem.result == MMS_NOT_EXISTS) {
                continue;
            }
            if (UNLIKELY(rspItem.result != MMS_OK)) {
                result = static_cast<BResult>(rspItem.result);
                continue;
            }
            if (UNLIKELY(rspItem.realLength > item.length || rspItem.valueOffset > rsp->dataLength ||
                         rspItem.realLength > rsp->dataLength - rspItem.valueOffset)) {
                CLIENT_LOG_ERROR("Invalid batch get item response, index:" << index << ".");
                return MMS_ERR;
            }

            char *value = reinterpret_cast<char *>(responseBuff + rspItem.valueOffset);
            if (*item.value == nullptr) {
                *item.value = value;
                continue;
            }
            auto ret = memcpy_s(*item.value, item.length, value, rspItem.realLength);
            if (UNLIKELY(ret != EOK)) {
                return MMS_ERR;
            }
        }
        return result;
    }();
    MMS_TRACE_END(SDK_TRACE_GET_BATCH_COPY, copyResult);
    BResult result = copyResult;
    return result;
}

BResult MmsKvClient::SendBatchGet(const std::shared_ptr<const ClientRouteView> &route, uint16_t localNid,
                                  uint16_t targetNid, uint64_t ptv, GetItems *itemList,
                                  const std::vector<uint32_t> &itemIndexes, uintptr_t responseBuff,
                                  uint32_t responseCapacity)
{
    static thread_local std::vector<char> requestBuff;
    auto ret = BuildBatchGetRequest(mMemMgr, localNid, targetNid, ptv, mClientGeneration, itemList, itemIndexes,
                                    responseBuff, responseCapacity, requestBuff);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    auto directState = LoadDirectRpcState();
    uint16_t groupIndex = SelectDirectRpcGroup(directState);
    if (directState != nullptr && EnsureDirectConnection(route, targetNid, directState, groupIndex) == MMS_OK) {
        auto *directReq = reinterpret_cast<BatchGetRequest *>(requestBuff.data());
        ret = directState->engine->GetRegisteredMemoryKey(responseBuff, responseCapacity, directReq->valueKey);
        if (LIKELY(ret == MMS_OK)) {
            directReq->head.groupIndex = groupIndex;
            directReq->responseOffset = 0;
            directReq->valueAddr = responseBuff;
            directReq->flags |= MMS_BATCH_GET_FLAG_FORWARDED;
            BResult directRsp = MMS_OK;
            MMS_TRACE_START(SDK_TRACE_GET_BATCH_DIRECT_CALL);
            ret = directState->engine->SyncCall<BResult>(targetNid, groupIndex, MMS_OP_C_BATCH_GET, directReq,
                                                         static_cast<uint32_t>(requestBuff.size()), directRsp);
            BResult directRet = (ret == MMS_OK) ? directRsp : ret;
            MMS_TRACE_END(SDK_TRACE_GET_BATCH_DIRECT_CALL, directRet);
            if (LIKELY(directRet == MMS_OK)) {
                return ApplyBatchGetResponse(itemList, itemIndexes, responseBuff, responseCapacity);
            }
            if (directRet != MMS_NET_RETRY) {
                return directRet;
            }
        }

        ret = BuildBatchGetRequest(mMemMgr, localNid, targetNid, ptv, mClientGeneration, itemList, itemIndexes,
                                   responseBuff, responseCapacity, requestBuff);
        if (UNLIKELY(ret != MMS_OK)) {
            return ret;
        }
        CLIENT_LOG_WARN("Direct batch get unavailable, target nid:" << targetNid << ". Fallback to local proxy.");
    }

    BResult rspRet = MMS_OK;
    MMS_TRACE_START(SDK_TRACE_GET_REMOTE_BATCH);
    ret = mNetEngine->SyncCall<BResult>(INVALID_NID, g_groupIndex, MMS_OP_C_BATCH_GET, requestBuff.data(),
                                       static_cast<uint32_t>(requestBuff.size()), rspRet);
    BResult callRet = (ret == MMS_OK) ? rspRet : ret;
    MMS_TRACE_END(SDK_TRACE_GET_REMOTE_BATCH, callRet);
    return (callRet == MMS_OK) ?
        ApplyBatchGetResponse(itemList, itemIndexes, responseBuff, responseCapacity) : callRet;
}

BResult MmsKvClient::MmsGetBatch(GetItems *itemList, uint32_t itemNum)
{
    static thread_local std::array<std::vector<uint32_t>, MAX_NODES_NUM> remoteItems;
    static thread_local std::vector<uint32_t> localItems;
    for (auto &items : remoteItems) {
        items.clear();
    }
    localItems.clear();
    localItems.reserve(itemNum);
    auto route = LoadRouteView();
    BResult routeRet = MMS_INNER_RETRY;
    MMS_TRACE_START(SDK_TRACE_GET_BATCH_ROUTE);
    for (uint16_t attempt = 0; attempt < NO_2; ++attempt) {
        for (auto &items : remoteItems) {
            items.clear();
        }
        localItems.clear();
        if (route == nullptr || route->ptNum == 0) {
            routeRet = MMS_INNER_RETRY;
        } else {
            routeRet = MMS_OK;
            for (uint32_t index = 0; index < itemNum; ++index) {
                uint16_t localNid = INVALID_NID;
                uint16_t targetNid = INVALID_NID;
                uint16_t ptId = 0;
                uint64_t ptv = 0;
                bool localOwner = false;
                routeRet = ResolveGetRoute(route, itemList[index].key, itemList[index].keyLen, localNid, targetNid,
                                           ptId, ptv, localOwner);
                if (UNLIKELY(routeRet != MMS_OK)) {
                    break;
                }
                if (localOwner) {
                    localItems.push_back(index);
                    continue;
                }
                remoteItems[targetNid].push_back(index);
            }
        }
        if (LIKELY(routeRet == MMS_OK) || routeRet != MMS_INNER_RETRY || attempt != 0) {
            break;
        }
        auto refreshRet = RefreshRouteView(route);
        if (UNLIKELY(refreshRet != MMS_OK)) {
            routeRet = refreshRet;
            break;
        }
        route = LoadRouteView();
    }
    MMS_TRACE_END(SDK_TRACE_GET_BATCH_ROUTE, routeRet);
    if (UNLIKELY(routeRet != MMS_OK)) {
        return MMS_NET_RETRY;
    }

    auto *localItemIndexes = &localItems;
    auto readLocalItems = [this, itemList, localItemIndexes]() {
        BResult result = MMS_OK;
        MMS_TRACE_START(SDK_TRACE_GET_LOCAL);
        for (auto index : *localItemIndexes) {
            auto &item = itemList[index];
            uint64_t realLength = 0;
            GetPara para = {item.key, item.keyLen, item.offset, item.length, item.value, &realLength};
            auto ret = mCache->Get(para);
            *item.realLength = static_cast<uint32_t>(realLength);
            *item.result = ret;
            if (UNLIKELY(ret != MMS_OK && ret != MMS_NOT_EXISTS)) {
                result = ret;
            }
        }
        MMS_TRACE_END(SDK_TRACE_GET_LOCAL, result);
        return result;
    };

    MMS_TRACE_START(SDK_TRACE_GET_BATCH_PREPARE);
    static thread_local std::vector<BatchGetChunk> chunks;
    chunks.clear();
    chunks.reserve(MAX_NODES_NUM);
    uint64_t totalResponseCapacity = 0;
    bool hasNullOutput = false;
    for (uint16_t targetNid = 0; targetNid < MAX_NODES_NUM; ++targetNid) {
        BatchGetChunk chunk;
        chunk.targetNid = targetNid;
        uint64_t valueCapacity = 0;
        for (auto itemIndex : remoteItems[targetNid]) {
            auto &item = itemList[itemIndex];
            uint64_t nextMetaSize = BatchGetResponseMetaSize(static_cast<uint32_t>(chunk.itemIndexes.size() + NO_1));
            uint64_t nextResponseCapacity = nextMetaSize + valueCapacity + item.length;
            uint64_t nextRequestLen = sizeof(BatchGetRequest) +
                (chunk.itemIndexes.size() + NO_1) * sizeof(BatchGetItemRequest);
            if (!chunk.itemIndexes.empty() && (nextResponseCapacity > MMS_ONESIDE_STAGING_SIZE ||
                                                nextRequestLen > MMS_TWOSIDE_IO_THRESHOLD)) {
                chunk.responseCapacity = static_cast<uint32_t>(BatchGetResponseMetaSize(
                    static_cast<uint32_t>(chunk.itemIndexes.size())) + valueCapacity);
                totalResponseCapacity += chunk.responseCapacity;
                chunks.emplace_back(std::move(chunk));
                chunk = BatchGetChunk{};
                chunk.targetNid = targetNid;
                valueCapacity = 0;
                nextMetaSize = BatchGetResponseMetaSize(NO_1);
                nextResponseCapacity = nextMetaSize + item.length;
            }
            if (UNLIKELY(nextResponseCapacity > MMS_ONESIDE_STAGING_SIZE)) {
                MMS_TRACE_END(SDK_TRACE_GET_BATCH_PREPARE, MMS_NET_RETRY);
                return MMS_NET_RETRY;
            }
            chunk.itemIndexes.push_back(itemIndex);
            valueCapacity += item.length;
            hasNullOutput = hasNullOutput || (*item.value == nullptr);
        }
        if (!chunk.itemIndexes.empty()) {
            chunk.responseCapacity = static_cast<uint32_t>(BatchGetResponseMetaSize(
                static_cast<uint32_t>(chunk.itemIndexes.size())) + valueCapacity);
            totalResponseCapacity += chunk.responseCapacity;
            chunks.emplace_back(std::move(chunk));
        }
    }
    if (chunks.empty()) {
        MMS_TRACE_END(SDK_TRACE_GET_BATCH_PREPARE, MMS_OK);
        return readLocalItems();
    }
    if (UNLIKELY(totalResponseCapacity > MMS_ONESIDE_STAGING_SIZE && hasNullOutput)) {
        MMS_TRACE_END(SDK_TRACE_GET_BATCH_PREPARE, MMS_NET_RETRY);
        return MMS_NET_RETRY;
    }

    static thread_local GetOneSideBufferCache batchGetBufferCache;
    uint64_t reserveSize = std::min<uint64_t>(totalResponseCapacity, MMS_ONESIDE_STAGING_SIZE);
    uintptr_t responseBuff = 0;
    auto ret = batchGetBufferCache.Reserve(mMemAllocator, reserveSize, responseBuff, true);
    if (UNLIKELY(ret != MMS_OK)) {
        MMS_TRACE_END(SDK_TRACE_GET_BATCH_PREPARE, ret);
        return ret;
    }
    MMS_TRACE_END(SDK_TRACE_GET_BATCH_PREPARE, MMS_OK);

    auto *remoteChunks = &chunks;
    auto sendRemoteItems = [this, route, itemList, responseBuff, totalResponseCapacity,
                            remoteChunks]() -> BResult {
        uint64_t responseOffset = 0;
        for (const auto &chunk : *remoteChunks) {
            if (totalResponseCapacity > MMS_ONESIDE_STAGING_SIZE) {
                responseOffset = 0;
            }
            auto ret = SendBatchGet(route, route->localNid, chunk.targetNid, route->ptVersion, itemList,
                                    chunk.itemIndexes, responseBuff + responseOffset, chunk.responseCapacity);
            if (UNLIKELY(ret != MMS_OK)) {
                return ret;
            }
            responseOffset += chunk.responseCapacity;
        }
        return MMS_OK;
    };

    static thread_local std::unique_ptr<BatchGetWorker> batchGetWorker;
    bool parallelGet = !localItems.empty() && TryAcquireBatchGetParallelTask();
    if (parallelGet) {
        if (batchGetWorker == nullptr) {
            batchGetWorker.reset(new (std::nothrow) BatchGetWorker());
            if (UNLIKELY(batchGetWorker == nullptr)) {
                ReleaseBatchGetParallelTask();
                return MMS_ALLOC_FAIL;
            }
        }
        batchGetWorker->Submit(readLocalItems);
    }

    BResult localResult = MMS_OK;
    if (!parallelGet) {
        localResult = readLocalItems();
        if (UNLIKELY(localResult != MMS_OK)) {
            return localResult;
        }
    }

    BResult remoteResult = sendRemoteItems();
    if (parallelGet) {
        localResult = batchGetWorker->Wait();
        ReleaseBatchGetParallelTask();
    }

    if (remoteResult == MMS_NEED_UPDATE_PT_VERSION) {
        auto updateRet = UpdateClientPtVersion();
        return updateRet == MMS_OK ? MMS_NET_RETRY : updateRet;
    }
    if (UNLIKELY(localResult != MMS_OK)) {
        return localResult;
    }
    if (UNLIKELY(remoteResult != MMS_OK)) {
        return remoteResult;
    }

    return MMS_OK;
}

BResult MmsKvClient::SendGetReq(GetItems &item)
{
    GetValueRequest req = {};
    req.head = {0, MMS_OP_C_GET, 0, 0, mPtVersion.load(std::memory_order_acquire)};
    req.clientGeneration = mClientGeneration;
    req.offset = item.offset;
    req.length = item.length;
    BResult ret = strncpy_s(req.key, MAX_KEY_SIZE, item.key, item.keyLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("string copy failed.");
        return MMS_ERR;
    }

    uint64_t startTime = Monotonic::TimeSec();
    do {
        bool fallbackProxy = true;
        uint16_t localNid = INVALID_NID;
        uint16_t targetNid = INVALID_NID;
        uint16_t ptId = 0;
        uint64_t ptv = mPtVersion.load(std::memory_order_acquire);
        bool localOwner = false;
        if (mKeyRouteEnabled) {
            MMS_TRACE_START(SDK_TRACE_GET_ROUTE);
            auto routeRet =
                ResolveGetRouteWithRefresh(item.key, item.keyLen, localNid, targetNid, ptId, ptv, localOwner);
            MMS_TRACE_END(SDK_TRACE_GET_ROUTE, routeRet);
            if (routeRet == MMS_OK) {
                req.head = {localNid, MMS_OP_C_GET, 0, ptId, ptv};
                if (localOwner) {
                    uint64_t realLength = 0;
                    GetPara para = {item.key, item.keyLen, item.offset, item.length, item.value, &realLength};
                    MMS_TRACE_START(SDK_TRACE_GET_LOCAL);
                    ret = mCache->Get(para);
                    MMS_TRACE_END(SDK_TRACE_GET_LOCAL, ret);
                    *item.realLength = static_cast<uint32_t>(realLength);
                    if (LIKELY(ret == MMS_OK)) {
                        return MMS_OK;
                    }
                    return ret;
                }
                const auto &route = LoadRouteView();
                auto directState = LoadDirectRpcState();
                uint16_t groupIndex = SelectDirectRpcGroup(directState);
                bool validDirectRoute = route != nullptr && ptId < route->ptNum &&
                    route->pts[ptId].ptId == ptId && route->pts[ptId].masterNodeId == targetNid;
                if (validDirectRoute &&
                    EnsureDirectConnection(route, targetNid, directState, groupIndex) == MMS_OK) {
                    ret = SendGetDirect(item, req, targetNid, directState, groupIndex, route->pts[ptId].version);
                    if (LIKELY(ret == MMS_OK)) {
                        return MMS_OK;
                    }
                    fallbackProxy = (ret == MMS_NET_RETRY);
                    if (fallbackProxy) {
                        CLIENT_LOG_WARN("Direct get unavailable, target nid:" << targetNid
                                                                              << ". Fallback to local proxy.");
                    }
                }
            } else {
                CLIENT_LOG_WARN("Resolve get route failed, ret:" << routeRet << ", key:" << item.key
                                                                 << ". Fallback to local proxy.");
                req.head = {0, MMS_OP_C_GET, 0, 0, mPtVersion.load(std::memory_order_acquire)};
            }
        }

        if (fallbackProxy) {
            ret = SendGetByProxy(item, req);
            if (LIKELY(ret == MMS_OK)) {
                return MMS_OK;
            }
        }

        if (ret == MMS_NEED_UPDATE_PT_VERSION) {
            BResult updateRet = UpdateClientPtVersion();
            if (UNLIKELY(updateRet != MMS_OK)) {
                return updateRet;
            }
        }

        bool isContinue = (ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY || ret == MMS_NET_RETRY ||
                           ret == MMS_CHECK_PT_FAIL || ret == MMS_NEED_UPDATE_PT_VERSION);
        if (!isContinue) {
            break;
        }

        sleep(IO_RETRY_INTERAL);
        uint64_t costTime = Monotonic::TimeSec() - startTime;
        if (costTime >= mIoTimeOut) {
            break;
        }
    } while (true);

    return ret;
}

BResult MmsKvClient::MmsUpdate(UpdateItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    uint16_t numaId = mMemAllocator->GetNumaId();
    auto cachedAllocFunc = [this](uint64_t size, uint16_t &allocNumaId, uintptr_t &blockAddr) {
        return g_encodeBufferCache.Alloc(mMemAllocator, size, allocNumaId, blockAddr);
    };

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        uint32_t encodeBuffLen = CalcEncodeBuffLen(mMaxMsgBuffSize,
            CalcValueEncodeLen(&itemList[curItemIndex], itemNum - curItemIndex));
        ret = EncodeUpdateRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, cachedAllocFunc,
                                  encodeBuffLen);
        uint64_t keepBuff = g_encodeBufferCache.BorrowedBuff();
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, MMS_OP_C_UPDATE, ctxItems, keepBuff, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillUpdateItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, MMS_OP_C_UPDATE, ctxItems, keepBuff, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillUpdateItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            CLIENT_LOG_DEBUG("Send batch update success, total send:" << curItemIndex
                                                                      << ", current batch:" << ctxItems.size() << ".");
        } else {
            CLIENT_LOG_ERROR("Encode update request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

BResult MmsKvClient::MmsDelete(DeleteItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    uint16_t numaId = mMemAllocator->GetNumaId();
    auto cachedAllocFunc = [this](uint64_t size, uint16_t &allocNumaId, uintptr_t &blockAddr) {
        return g_encodeBufferCache.Alloc(mMemAllocator, size, allocNumaId, blockAddr);
    };

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        uint32_t encodeBuffLen = CalcEncodeBuffLen(mMaxMsgBuffSize,
            CalcDeleteEncodeLen(&itemList[curItemIndex], itemNum - curItemIndex));
        ret = EncodeDeleteRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, cachedAllocFunc,
                                  encodeBuffLen);
        uint64_t keepBuff = g_encodeBufferCache.BorrowedBuff();
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, MMS_OP_C_DELETE, ctxItems, keepBuff, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillDeleteItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, false};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, MMS_OP_C_DELETE, ctxItems, keepBuff, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillDeleteItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, false};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            CLIENT_LOG_DEBUG("Send batch delete success, total send:" << curItemIndex
                                                                      << ", current batch:" << ctxItems.size() << ".");
        } else {
            CLIENT_LOG_ERROR("Encode delete request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

BResult MmsKvClient::MmsReplace(ReplaceItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    uint16_t numaId = mMemAllocator->GetNumaId();
    auto cachedAllocFunc = [this](uint64_t size, uint16_t &allocNumaId, uintptr_t &blockAddr) {
        return g_encodeBufferCache.Alloc(mMemAllocator, size, allocNumaId, blockAddr);
    };

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        uint32_t encodeBuffLen = CalcEncodeBuffLen(mMaxMsgBuffSize,
            CalcValueEncodeLen(&itemList[curItemIndex], itemNum - curItemIndex));
        ret = EncodeReplaceRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, cachedAllocFunc,
                                   encodeBuffLen);
        uint64_t keepBuff = g_encodeBufferCache.BorrowedBuff();
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, MMS_OP_C_REPLACE, ctxItems, keepBuff, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillReplaceItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, MMS_OP_C_REPLACE, ctxItems, keepBuff, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillReplaceItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            CLIENT_LOG_DEBUG("Send batch replace success, total send:" << curItemIndex
                                                                       << ", current batch:" << ctxItems.size() << ".");
        } else {
            CLIENT_LOG_ERROR("Encode replace request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems, keepBuff);
            g_encodeBufferCache.Release();
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

void MmsKvClient::HandleUpdatePtVersion(uint64_t ptVersion)
{
    mPtVersion.store(ptVersion, std::memory_order_release);
    UpdateLocalPtVersion(ptVersion);
    CLIENT_LOG_INFO("Update client pt version:" << ptVersion << ".");
}

BResult MmsKvClient::UpdateClientPtVersion()
{
    if (mKeyRouteEnabled) {
        return RefreshRouteView(LoadRouteView());
    }

    BResult ret = MMS_OK;
    UpdatePtVRsp rsp;
    BasicRequest req = {{0, MMS_OP_C_UPDATE_PT_VERSION, 0, 0, 0}};
    uint16_t retryCount = 0;

    do {
        ret = mNetEngine->SyncCall<BasicRequest, UpdatePtVRsp>(INVALID_NID, g_groupIndex, MMS_OP_C_UPDATE_PT_VERSION,
                                                               req, rsp);
        if (LIKELY(ret == MMS_OK)) {
            HandleUpdatePtVersion(rsp.ptVersion);
            break;
        }

        CLIENT_LOG_ERROR("Send request failed, ret:" << ret << ", retry count:" << ++retryCount << ".");
        if (retryCount > RETRY_COUNT) {
            CLIENT_LOG_ERROR("Send request failed after " << retryCount << " retries, exiting.");
            break;
        }

        sleep(RETRY_SLEEP);
        bool isContinue =
            (ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY || ret == MMS_NET_RETRY || ret == MMS_CHECK_PT_FAIL);
        if (!isContinue) {
            break;
        }
    } while (true);

    return ret;
}

BResult MmsKvClient::FailHandle(BResult lastRet, MmsOpCode opCode, IoCtrlRequest &req, BResult &rsp)
{
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = lastRet;

    do {
        if (ret == MMS_NEED_UPDATE_PT_VERSION) {
            BResult updateRet = UpdateClientPtVersion();
            if (UNLIKELY(updateRet != MMS_OK)) {
                return updateRet;
            }

            req.head.ptv = mPtVersion.load(std::memory_order_acquire);
        }

        bool isContinue = (ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY || ret == MMS_NET_RETRY ||
                           ret == MMS_CHECK_PT_FAIL || ret == MMS_NEED_UPDATE_PT_VERSION);
        if (!isContinue) {
            break;
        }

        sleep(IO_RETRY_INTERAL);

        uint64_t costTime = Monotonic::TimeSec() - startTime;
        if (costTime >= mIoTimeOut) {
            break;
        }

        ret = mNetEngine->SyncCall<IoCtrlRequest, BResult>(INVALID_NID, g_groupIndex, opCode, req, rsp);
        if (UNLIKELY(ret != MMS_OK)) {
            CLIENT_LOG_ERROR("Send request failed, ret:" << ret << ".");
        }
    } while (true);

    return ret;
}
} // namespace mms
} // namespace ock
