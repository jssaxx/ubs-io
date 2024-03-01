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

namespace ock {
namespace bio {
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

    BResult Put(PutRequest &req, const WCacheSlicePtr &sliceP);
    BResult Get(GetRequest &req, uint64_t &realLen);
    BResult Delete(DeleteRequest &req);
    BResult Stat(StatRequest &req, Bio::ObjStat &objInfo);
    BResult Load(LoadRequest &req);
    BResult SyncData(SyncDataRequest &req);

    BResult GetFlowGlobEvictOffset(uint16_t ptId, uint64_t flowId, bool &isMaster, uint64_t &flowOffset);

    BResult SendSyncData(uint16_t ptId, uint16_t masterNodeId, uint64_t version);

    MirrorServer() = default;
    ~MirrorServer() = default;
    DEFINE_REF_COUNT_FUNCTIONS

private:
    bool CheckAll(RequestComm &reqComm);
    void RegisterOpcode();
    void Reply(ServiceContext &ctx, int32_t retCode, void *resp, uint32_t respSize);

    BResult SendFlowGetEvictOffset(uint16_t ptId, uint64_t flowId, uint64_t &flowOffset);
    BResult GetEvictOffset(GetEvictRequest &req, uint64_t &flowOffset);

    int32_t HandleQueryNodeInfo(ServiceContext &ctx);
    int32_t HandleQueryNodeView(ServiceContext &ctx);
    int32_t HandleQueryPtView(ServiceContext &ctx);
    int32_t HandlePut(ServiceContext &ctx);
    int32_t HandleGet(ServiceContext &ctx);
    int32_t HandleDelete(ServiceContext &ctx);
    int32_t HandleStat(ServiceContext &ctx);
    int32_t HandleLoad(ServiceContext &ctx);
    int32_t HandleCreateFlow(ServiceContext &ctx);
    int32_t HandleGetSlice(ServiceContext &ctx);

    int32_t HandleSyncData(ServiceContext &ctx);
    int32_t HandleGetEvictOffset(ServiceContext &ctx);

private:
    bool mStarted = false;
    std::mutex mStartLock;
    CacheSliceOperator mSliceOp;
    DEFINE_REF_COUNT_VARIABLE
};
}
}
#endif