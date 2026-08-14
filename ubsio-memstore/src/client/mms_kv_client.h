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

#ifndef MMS_KV_CLIENT_H
#define MMS_KV_CLIENT_H

#include <semaphore.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include "mms.h"
#include "mms_cache.h"
#include "mms_lock.h"
#include "mms_mem_allocator.h"
#include "mms_mem_mgr.h"
#include "mms_message.h"
#include "mms_ref.h"
#include "net_engine.h"

namespace ock {
namespace mms {

struct KvClientPara {
    CachePtr cache;
    NetEnginePtr netEngine;
    MmsMemMgrPtr memMgr;
    MmsMemAllocatorPtr memAllocator;
    uint32_t ioTimeOut;
    uint32_t maxMsgBuffSize;
    uint64_t clientGeneration;
    bool keyRouteEnabled;
    MmsOptions clientOptions;
};

struct ClientRouteView {
    uint16_t localNid{INVALID_NID};
    uint16_t ptNum{0};
    uint16_t replicaNum{0};
    uint16_t rpcProtocol{0};
    uint16_t rpcConnCount{0};
    uint16_t rpcGroupNum{0};
    uint64_t ptVersion{0};
    std::array<RouteViewNodeInfo, MAX_NODES_NUM> nodes{};
    std::array<RouteViewPtInfo, MMS_ROUTE_MAX_PT_NUM> pts{};
};

struct ClientDirectRpcState;

class MmsKvClient;
using MmsKvClientPtr = Ref<MmsKvClient>;
class MmsKvClient {
public:
    BResult Initialize(const KvClientPara &para);
    BResult Rebuild(void);
    void Exit(void);

    MmsKvClient() = default;

    BResult SendSingleReq(IoCtrlRequest &req);

    BResult HandleSendReqs(uint16_t numaId, MmsOpCode opCode, std::vector<IOCtxItem> &ctxItems,
                           uint64_t keepBuff = 0, bool freeBlocks = true);

    BResult MmsPut(PutItems *itemList, uint32_t itemNum);

    BResult MmsGet(GetItems *itemList, uint32_t itemNum);

    BResult MmsUpdate(UpdateItems *itemList, uint32_t itemNum);

    BResult MmsDelete(DeleteItems *itemList, uint32_t itemNum);

    BResult MmsReplace(ReplaceItems *itemList, uint32_t itemNum);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult FailHandle(BResult lastRet, MmsOpCode opCode, IoCtrlRequest &req, BResult &rsp);
    void FreeBlocks(std::vector<IOCtxItem> &ctxItems, uint64_t keepBuff = 0);
    void HandleUpdatePtVersion(uint64_t ptVersion);
    BResult UpdateClientPtVersion();
    BResult RefreshRouteView(const std::shared_ptr<const ClientRouteView> &observedRoute = nullptr);
    const std::shared_ptr<const ClientRouteView> &LoadRouteView() const;
    std::shared_ptr<const ClientRouteView> LoadPublishedRouteView() const;
    BResult ResolveGetRoute(const std::shared_ptr<const ClientRouteView> &route, const char *key, uint16_t keyLen,
                            uint16_t &localNid, uint16_t &targetNid, uint16_t &ptId, uint64_t &ptv,
                            bool &localOwner);
    BResult ResolveGetRouteWithRefresh(const char *key, uint16_t keyLen, uint16_t &localNid, uint16_t &targetNid,
                                       uint16_t &ptId, uint64_t &ptv, bool &localOwner);
    BResult SendGetReq(GetItems &item);
    BResult MmsGetSequential(GetItems *itemList, uint32_t itemNum);
    BResult MmsGetBatch(GetItems *itemList, uint32_t itemNum);
    BResult SendBatchGet(const std::shared_ptr<const ClientRouteView> &route, uint16_t localNid,
                         uint16_t targetNid, uint64_t ptv, GetItems *itemList,
                         const std::vector<uint32_t> &itemIndexes, uintptr_t responseBuff,
                         uint32_t responseCapacity);
    BResult SendGetByProxy(GetItems &item, GetValueRequest &req);
    BResult SendGetDirect(GetItems &item, GetValueRequest req, uint16_t targetNid,
                          const std::shared_ptr<const ClientDirectRpcState> &state, uint16_t groupIndex,
                          uint64_t ptVersion);
    BResult StartDirectRpc(const std::shared_ptr<const ClientRouteView> &route);
    void StopDirectRpc();
    std::shared_ptr<const ClientDirectRpcState> LoadDirectRpcState() const;
    void EnsureDirectConnections(const std::shared_ptr<const ClientRouteView> &route,
                                 const std::shared_ptr<const ClientDirectRpcState> &state);
    BResult EnsureDirectConnection(const std::shared_ptr<const ClientRouteView> &route, uint16_t targetNid,
                                   const std::shared_ptr<const ClientDirectRpcState> &state, uint16_t groupIndex);
    uint16_t SelectDirectRpcGroup(const std::shared_ptr<const ClientDirectRpcState> &state) const;
private:
    uint32_t mIoTimeOut = NO_60;
    std::atomic<uint64_t> mPtVersion{NO_1};
    uint32_t mMaxMsgBuffSize;
    uint64_t mClientGeneration{0};
    bool mKeyRouteEnabled{false};
    std::shared_ptr<const ClientRouteView> mRouteView;
    std::atomic<uint64_t> mRouteGeneration{0};
    uint64_t mRouteCacheId{0};
    std::mutex mRouteRefreshLock;

    CachePtr mCache{nullptr};
    NetEnginePtr mNetEngine{nullptr};
    MmsMemMgrPtr mMemMgr{nullptr};
    MmsMemAllocatorPtr mMemAllocator{nullptr};
    std::shared_ptr<const ClientDirectRpcState> mDirectRpcState;
    std::mutex mDirectRpcLock;
    std::array<std::mutex, MAX_NODES_NUM> mDirectConnectLocks;
    MmsOptions mClientOptions{};

    DEFINE_REF_COUNT_VARIABLE;
};
} // namespace mms
} // namespace ock
#endif
