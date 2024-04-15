/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef MIRROR_SERVER_H
#define MIRROR_SERVER_H

#include <utility>
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
struct PutSlowRecode {
    WCacheSlicePtr sliceP;
    std::atomic<uint32_t> receiveCnt;

    explicit PutSlowRecode(WCacheSlicePtr slice) : sliceP(slice), receiveCnt(0) {}
};

class MirrorServer;
using MirrorServerPtr = Ref<MirrorServer>;
class MirrorServer {
public:
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

    BResult CreateFlow(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId);
    BResult CreateFlowMaster(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t &flowId);
    BResult CreateFlowSlave(uint64_t procId, uint16_t ptId, uint64_t ptv, uint64_t flowId);
    BResult GetSlice(uint64_t flowId, uint64_t flowOffset, uint64_t flowIndex, uint64_t length, WCacheSlicePtr &slice);
    void QueryNodeView(QueryNodeViewRequest &req, QueryNodeViewResponse &rsp);
    void QueryPtView(QueryPtViewRequest &req, QueryPtViewResponse &rsp);

    BResult Put(PutRequest &req, const WCacheSlicePtr &sliceP, ServiceContext &netCtx);
    BResult Get(GetRequest &req, GetResponse &rsp, ServiceContext &netCtx);
    BResult Delete(DeleteRequest &req);
    BResult List(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);
    BResult Stat(StatRequest &req, ObjStat &objInfo);
    BResult Load(LoadRequest &req);
    BResult SyncData(SyncDataRequest &req);

    BResult GetFlowGlobEvictOffset(uint16_t ptId, uint64_t flowId, uint64_t &flowOffset);

    BResult SendSyncData(uint16_t ptId, uint16_t masterNodeId, uint64_t version);

    void ReplyDone(uint32_t opcode, int32_t ret, uint64_t ts);
    void Reply(ServiceContext &ctx, int32_t retCode, void *resp, uint32_t respSize, uint32_t opcode = BIO_OP_BUTT);
    MirrorServer() = default;
    ~MirrorServer() = default;
    DEFINE_REF_COUNT_FUNCTIONS

private:
    bool CheckAll(RequestComm &reqComm);
    void RegisterOpcode();
    void ReplyListResultLocal(ServiceContext &ctx, std::unordered_map<std::string, ObjStat> &objs);
    void ReplyListResultRemote(ServiceContext &ctx, ListRequest *req, std::unordered_map<std::string, ObjStat> &objs);

    BResult SendFlowGetEvictOffset(uint16_t ptId, uint64_t flowId, uint64_t &flowOffset);
    BResult GetEvictOffset(GetEvictRequest &req, uint64_t &flowOffset);

    int32_t HandleShmInit(ServiceContext &ctx);
    int32_t HandleQueryNodeInfo(ServiceContext &ctx);
    int32_t HandleQueryNodeInfoByPt(ServiceContext &ctx);
    int32_t HandleQueryNodeView(ServiceContext &ctx);
    int32_t HandleQueryPtView(ServiceContext &ctx);
    int32_t HandlePut(ServiceContext &ctx);
    int32_t HandleGet(ServiceContext &ctx);
    int32_t HandleDelete(ServiceContext &ctx);
    int32_t HandleStat(ServiceContext &ctx);
    int32_t HandleList(ServiceContext &ctx);
    int32_t HandleLoad(ServiceContext &ctx);
    int32_t HandleReportHb(ServiceContext &ctx);
    int32_t HandleCreateFlow(ServiceContext &ctx);
    int32_t HandleGetSlice(ServiceContext &ctx);
    int32_t HandleSyncData(ServiceContext &ctx);
    int32_t HandleGetEvictOffset(ServiceContext &ctx);
    int32_t HandleFreeMem(ServiceContext &ctx);

    BResult ReaderLocal(const SlicePtr &from, const SlicePtr &to);
    BResult ReaderRemoteEquals(PutRequest &req, std::vector<NetMrInfo> &lMrVec, std::vector<NetMrInfo> &rMrVec,
        ServiceContext &netCtx);
    BResult ReaderRemoteNotEquals(PutRequest &req, std::vector<NetMrInfo> &lMrVec, std::vector<NetMrInfo> &rMrVec,
        ServiceContext &netCtx);
    BResult ReaderRemote(const SlicePtr &from, const SlicePtr &to, PutRequest &req, ServiceContext &netCtx);

    void InitGetResponse(GetResponse &rsp);
    BResult WriterLocalSameProcess(const SlicePtr &from, const SlicePtr &to, uint32_t rKey);
    BResult WriterParseMrInfo(const SlicePtr &from, const SlicePtr &to, std::vector<NetMrInfo> &rMrVec,
        std::vector<NetMrInfo> &lMrVec, uint32_t rKey, bool &isAlloc);
    BResult WriterLocalDiffProcess(bool &isAlloc, std::vector<NetMrInfo> &lMrVec, GetResponse &rsp);
    BResult WriterRemote(bool isAlloc, std::vector<NetMrInfo> &lMrVec, std::vector<NetMrInfo> &rMrVec,
        ServiceContext &netCtx, GetRequest &req);

private:
    bool mStarted = false;
    std::mutex mStartLock;
    CacheSliceOperator mSliceOp;
    DEFINE_REF_COUNT_VARIABLE
};
}
}
#endif