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

#ifndef MIRROR_SERVER_H
#define MIRROR_SERVER_H

#include <utility>
#include <sys/mman.h>
#include "bio_ref.h"
#include "bio_err.h"
#include "net_engine.h"
#include "net_common.h"
#include "bio.h"
#include "message.h"
#include "cache.h"
#include "message_op.h"

namespace ock {
namespace bio {
constexpr uint16_t SERVER_BATCH_GET_THREAD_NUM = 128;
constexpr uint16_t SERVER_BATCH_GET_QUEUE_SIZE = 8192;

struct MemFreeHolder {
    uint32_t nodeId;
    uint64_t clientId;
};

struct  MemFreeHolderHash {
    size_t operator()(const MemFreeHolder& holder) const
    {
        return std::hash<uint64_t>()(static_cast<uint64_t>(holder.nodeId)) ^ std::hash<uint64_t>()(holder.clientId);
    }
};

struct MemFreeHolderEqual {
    bool operator()(const MemFreeHolder& holder1, const MemFreeHolder& holder2) const
    {
        return (holder1.nodeId == holder2.nodeId) && (holder1.clientId == holder2.clientId);
    }
};

struct DataMsgMemItem {
    int32_t memFd;
    uint64_t offset;
    uint64_t length;
    uint8_t *address;
    DataMsgMemItem(int32_t fd, uint64_t off, uint64_t len, uint8_t *addr)
        : memFd(fd), offset(off), length(len), address(addr) {}
};

class MirrorServer;
using MirrorServerPtr = Ref<MirrorServer>;
class MirrorServer {
public:
    MirrorServer() = default;
    ~MirrorServer() = default;

    BResult Initialize();

    inline bool Ready()
    {
        std::lock_guard<std::mutex> lock(mStartLock);
        return mStarted;
    }

    inline static MirrorServerPtr &Instance()
    {
        static auto instance = MakeRef<MirrorServer>();
        return instance;
    }

    inline void SetStartWorker(bool value)
    {
        std::lock_guard<std::mutex> lock(mStartLock);
        mStarted = value;
    }

    BResult CreateFlow(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId, bool isDegrade);
    BResult CreateFlowMaster(uint64_t procId, uint16_t ptId, uint64_t ptv, CreateFlowResponse &flowInfo);
    BResult CreateFlowSlave(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId, bool isDegrade);
    BResult DestroyFlow(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId);
    BResult GetSlice(uint64_t flowId, uint64_t flowOffset, uint64_t flowIndex, uint64_t length, WCacheSlicePtr &slice);

    void QueryCacheQuota(QueryQuotaRequest &req, QueryQuotaResponse &rsp);
    BResult AllocCacheQuota(AllocQuotaRequest &req, AllocQuotaResponse &rsp);
    BResult FreeCacheQuota(FreeQuotaRequest &req);
    void QueryNodeView(QueryNodeViewRequest &req, QueryNodeViewResponse &rsp);
    void QueryPtView(QueryPtViewRequest &req, QueryPtViewResponse &rsp);

    void RecycleDataMsgMem(uint32_t pid);

    BResult Put(PutRequest &req, const WCacheSlicePtr &sliceP, ServiceContext &netCtx, uint32_t &ioStrategy);
    BResult GetConvergence(GetRequest &req, GetResponse &rsp);
    BResult ParseKeyAddr(const Key &key, uint16_t ptId, BatchKeyAddrInfo *info);
    BResult Get(GetRequest &req, GetResponse &rsp, ServiceContext &netCtx);
    BResult BatchSingleGet(GetKeyInfo &keyInfo, uint64_t &realLen, BatchGetRequest *req);
    BResult Delete(DeleteRequest &req);
    BResult AddDisk(AddDiskRequest &req);
    BResult AddDiskImpl(AddDiskRequest &req);
    BResult AddOldDiskImpl(const std::string &diskPath, uint16_t diskId);
    BResult AddNewDiskImpl(std::string &diskPath);
    BResult List(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);
    BResult Stat(StatRequest &req, ObjStat &objInfo);
    BResult BatchExist(BatchExistRequest *req, BatchExistResponse &rsp);
    BResult Load(LoadRequest &req);
    BResult NotifyUpdate(NotifyUpdateRequest &req);
    BResult CheckUpdateReady(CheckUpdateReadyRequest &req, CheckUpdateReadyResponse &rsp);
    BResult SyncData(SyncDataRequest &req);
    BResult GetCacheHitLocal(CacheHitResponse *rsp);
    BResult CalcCacheResourceLocal(CacheResourceResponse *rsp);
    BResult GetTracePointsLocal(GetTracePointsResponse *rsp);

    BResult GetFlowGlobEvictOffset(uint16_t ptId, uint64_t flowId, uint64_t &flowOffset);

    BResult SendSyncData(uint16_t ptId, uint16_t masterNodeId, uint64_t version);

    bool CheckMagic(RequestComm &reqComm);
    bool CheckAll(RequestComm &reqComm);

    void ReplyListResultLocal(ServiceContext &ctx, std::unordered_map<std::string, ObjStat> &objs, ListRequest &req);
    void ReplyListResultRemote(ServiceContext &ctx, ListRequest *req, std::unordered_map<std::string, ObjStat> &objs);

    BResult ReaderRemote(const SlicePtr &from, const SlicePtr &to, PutRequest &req, ServiceContext &netCtx);
    BResult WriterParseMrInfo(const SlicePtr &from, const SlicePtr &to, std::vector<NetMrInfo> &rMrVec,
        std::vector<NetMrInfo> &lMrVec, uint32_t rKey, bool &isAlloc);
    BResult WriterLocalDiffProcess(bool &isAlloc, std::vector<NetMrInfo> &lMrVec, GetResponse &rsp, GetRequest &req);
    BResult WriterRemote(bool isAlloc, std::vector<NetMrInfo> &lMrVec, std::vector<NetMrInfo> &rMrVec,
        ServiceContext &netCtx, GetRequest &req);
    BResult BatchSingleWriterRemote(bool isAlloc, std::vector<NetMrInfo> &lMrVec,
                                    std::vector<NetMrInfo> &rMrVec, BatchGetRequest *req);
    TraceDatabase GetTraceData();

    int32_t MirrorServerShmInit(ServiceContext &ctx, ShmInitRequest *req);
    int32_t MirrorServerQueryNodeInfo(ServiceContext &ctx, GetLocalNidRequest *req);
    int32_t MirrorServerQueryNodeInfoByPt(ServiceContext &ctx, FileLocationQueryReq *req);
    int32_t MirrorServerQueryQuota(ServiceContext &ctx, QueryQuotaRequest *req);
    int32_t MirrorServerAllocQuota(ServiceContext &ctx, AllocQuotaRequest *req);
    int32_t MirrorServerFreeQuota(ServiceContext &ctx, FreeQuotaRequest *req);
    int32_t MirrorServerQueryNodeView(ServiceContext &ctx, QueryNodeViewRequest *req);
    int32_t MirrorServerQueryPtView(ServiceContext &ctx, QueryPtViewRequest *req);
    int32_t MirrorServerPut(ServiceContext &ctx, PutRequest *req);
    int32_t MirrorServerBatchParseKeyAddr(ServiceContext &ctx, BatchParseKeyAddrRequest *req);
    int32_t MirrorServerGet(ServiceContext &ctx, GetRequest *req);
    int32_t MirrorServerBatchGet(ServiceContext &ctx, BatchGetRequest *req);
    int32_t MirrorServerDelete(ServiceContext &ctx, DeleteRequest *req);
    int32_t MirrorServerAddDisk(ServiceContext &ctx, AddDiskRequest *req);
    int32_t MirrorServerStat(ServiceContext &ctx, StatRequest *req);
    int32_t MirrorServerBtachExist(ServiceContext &ctx, BatchExistRequest *req);
    int32_t MirrorServerNotifyUpdate(ServiceContext &ctx, NotifyUpdateRequest *req);
    int32_t MirrorServerCheckUpdateReady(ServiceContext &ctx, CheckUpdateReadyRequest *req);
    int32_t MirrorServerCheckRemoteUpdateReady(ServiceContext &ctx, CheckRemoteUpdateReadyRequest *req);
    int32_t MirrorServerList(ServiceContext &ctx, ListRequest *req);
    int32_t MirrorServerLoad(ServiceContext &ctx, LoadRequest *req);
    int32_t MirrorServerCreateFlow(ServiceContext &ctx, CreateFlowRequest *req);
    int32_t MirrorServerDestroyFlow(ServiceContext &ctx, DestroyFlowRequest *req);
    int32_t MirrorServerCreateDataMsgMemPool(ServiceContext &ctx, CreateDataMsgMemPoolRequest *req);
    int32_t MirrorServerGetSlice(ServiceContext &ctx, GetSliceRequest *req);
    int32_t MirrorServerSyncData(ServiceContext &ctx, SyncDataRequest *req);
    int32_t MirrorServerGetEvictOffset(ServiceContext &ctx, GetEvictRequest *req);
    int32_t MirrorServerFreeMem(ServiceContext &ctx, FreeMemRequest *req);
    int32_t MirrorServerGetUnderFsConfig(ServiceContext &ctx, GetUnderFsConfigRequest *req);
    int32_t MirrorServerProcBrokenSyncFlow(ServiceContext &ctx, ProcFlowSyncRequest *req);
    int32_t MirrorServerGetCacheHit(ServiceContext &ctx);
    int32_t MirrorServerQueryCacheResource(ServiceContext &ctx);
    int32_t MirrorServerGetTracePoints(ServiceContext &ctx);

    int32_t HandleShmInit(ServiceContext &ctx);
    int32_t HandleQueryNodeInfo(ServiceContext &ctx);
    int32_t HandleQueryNodeInfoByPt(ServiceContext &ctx);
    int32_t HandleQueryQuota(ServiceContext &ctx);
    int32_t HandleAllocQuota(ServiceContext &ctx);
    int32_t HandleFreeQuota(ServiceContext &ctx);
    int32_t HandleQueryNodeView(ServiceContext &ctx);
    int32_t HandleQueryPtView(ServiceContext &ctx);
    int32_t HandlePut(ServiceContext &ctx);
    int32_t HandleBatchParseKeyAddr(ServiceContext &ctx);
    int32_t HandleGet(ServiceContext &ctx);
    int32_t HandleBatchGet(ServiceContext &ctx);
    int32_t HandleDelete(ServiceContext &ctx);
    int32_t HandleAddDisk(ServiceContext &ctx);
    int32_t HandleStat(ServiceContext &ctx);
    int32_t HandleBatchExist(ServiceContext &ctx);
    int32_t HandleNotifyUpdate(ServiceContext &ctx);
    int32_t HandleCheckUpdateReady(ServiceContext &ctx);
    int32_t HandleCheckRemoteUpdateReady(ServiceContext &ctx);
    int32_t HandleList(ServiceContext &ctx);
    int32_t HandleLoad(ServiceContext &ctx);
    int32_t HandleCreateFlow(ServiceContext &ctx);
    int32_t HandleDestroyFlow(ServiceContext &ctx);
    int32_t HandleCreateDataMsgMemPool(ServiceContext &ctx);
    int32_t HandleGetSlice(ServiceContext &ctx);
    int32_t HandleSyncData(ServiceContext &ctx);
    int32_t HandleGetEvictOffset(ServiceContext &ctx);
    int32_t HandleFreeMem(ServiceContext &ctx);
    int32_t HandleGetUnderFsConfig(ServiceContext &ctx);
    int32_t HandleProcBrokenSyncFlow(ServiceContext &ctx);
    int32_t HandleGetCacheHit(ServiceContext &ctx);
    int32_t HandleQueryCacheResource(ServiceContext &ctx);
    int32_t HandleGetTracePoints(ServiceContext &ctx);

    bool CheckUpdateReadyReq(CheckUpdateReadyRequest *req);
    bool CheckNotifyUpdateReq(NotifyUpdateRequest *req);
    bool CheckFreeMemReq(FreeMemRequest *req);
    bool CheckGetSliceReq(GetSliceRequest *req);

    inline uint64_t TransDataMsgMemAddr(pid_t holder, uint64_t offset)
    {
        std::lock_guard<std::mutex> lock(mDataMsgMemLock);
        auto iter = mDataMsgMemMgr.find(holder);
        if (iter == mDataMsgMemMgr.end()) {
            return 0;
        }
        return reinterpret_cast<uint64_t>(reinterpret_cast<uintptr_t>(iter->second.address + offset));
    }

    DEFINE_REF_COUNT_FUNCTIONS

private:
    void RegisterOpcodeStep2(NetEnginePtr &netEngine);
    void RegisterOpcode();
    BResult SendFlowGetEvictOffset(uint16_t ptId, uint64_t flowId, uint64_t &flowOffset);
    BResult GetEvictOffset(GetEvictRequest &req, uint64_t &flowOffset);
    BResult ReaderLocal(const SlicePtr &from, const SlicePtr &to, PutRequest &req);
    BResult ReaderRemoteEquals(PutRequest &req, std::vector<NetMrInfo> &lMrVec, std::vector<NetMrInfo> &rMrVec,
        ServiceContext &netCtx);
    BResult ReaderRemoteNotEquals(PutRequest &req, std::vector<NetMrInfo> &lMrVec, std::vector<NetMrInfo> &rMrVec,
        ServiceContext &netCtx);

    void InitGetResponse(GetResponse &rsp);
    BResult WriterLocalSameProcess(const SlicePtr &from, const SlicePtr &to, uint32_t rKey);
    uintptr_t ParseRealAddress(PutRequest *req);
    bool IsValidSliceAddress(WCacheSlicePtr &sliceP);

private:
    uint64_t mflowNum { 0 };
    ReadWriteLock flowNumLock;
    bool mStarted = false;
    std::mutex mStartLock;
    std::mutex mDiskViewMutex;
    CacheSliceOperator mSliceOp;

    std::mutex mDataMsgMemLock;
    std::unordered_map<pid_t, DataMsgMemItem> mDataMsgMemMgr;

    ReadWriteLock mLock;
    std::unordered_map<MemFreeHolder, std::vector<std::vector<NetMrInfo>>, MemFreeHolderHash,
        MemFreeHolderEqual> mHolders;
    ReadWriteLock mLockList;
    std::unordered_map<MemFreeHolder, std::vector<std::vector<NetMrInfo>>, MemFreeHolderHash,
        MemFreeHolderEqual> mHoldersList;

    ExecutorServicePtr mBatchGetExecutor{ nullptr };
    BioConfigPtr mBioConfig;
    DEFINE_REF_COUNT_VARIABLE
};
}
}
#endif