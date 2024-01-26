/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef MIRROR_CLIENT_H
#define MIRROR_CLIENT_H

#include <semaphore.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include "cm.h"
#include "bio_ref.h"
#include "message.h"
#include "bio.h"
#include "flow_instance.h"
#include "flow.h"
#include "slice.h"
#include "cache_slice.h"
#include "cache_slice_operator.h"

namespace ock {
namespace bio {
struct CacheAttr {
    uint64_t mTenantId;
    AffinityStrategy affinity;
    WriteStrategy strategy;
};

class MirrorClient {
public:
    struct MirrorPut {
        CacheAttr attr;
        const char *key;
        const char *value;
        uint64_t length;
        Bio::ObjLocation location;
    };

    struct MirrorGet {
        CacheAttr attr;
        const char *key;
        char *value;
        uint64_t offset;
        uint64_t length;
        Bio::ObjLocation location;
    };

    static constexpr uint32_t defaultMaxFlowSize = 1024;

    BResult Initialize();

    explicit MirrorClient(int32_t type) : mDeployType(type) {}
    ~MirrorClient() = default;

    uint16_t SelectingPt(uint64_t objectId, AffinityStrategy affinity);

    uint16_t ParseLocation(Bio::ObjLocation location);

    BResult Put(MirrorPut &param);

    BResult Get(MirrorGet &param, uint64_t &realLen);

    BResult DeleteKey(const char *key, const Bio::ObjLocation &location);

    BResult Load(const char *key, uint64_t offset, uint64_t length, const Bio::ObjLocation &location, const Bio::LoadCallback &callback, void *context);

    Bio::ObjStat StatObject(const char *key, const Bio::ObjLocation &location);

    DEFINE_REF_COUNT_FUNCTIONS

    std::vector<uint16_t> ListLocalAffinityPt();

    inline CmNodeId& GetLocalNodeInfo()
    {
        return mLocalNid;
    }

    inline std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp>& GetNodeView()
    {
        return mNodeView;
    }

    inline std::map<uint16_t, CmPtInfo>& GetPtView()
    {
        return mPtView;
    }

private:
    void Copy(const void *src, void *dst, const uint64_t len)
    {
        memcpy_s(dst, len, src, len);
    }

    BResult LoadOriginView();
    BResult LoadAffinityFlow();

    BResult GetPtEntry(uint16_t ptId, ock::bio::CmPtInfo &ptEntry);

    BResult AllocPutOffset(uint16_t ptId, uint64_t len, uint64_t &flowId, uint64_t &offset, uint64_t &index);

    BResult SendCreateFlowRequest(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType, uint64_t &flowId);
    BResult CreateFlowImpl(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType, uint64_t &flowId);
    BResult CreateFlow(uint16_t ptId);
    BResult DestroyFlow(uint16_t ptId);

    void ConstructPutReq(uint8_t *req, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t offset, uint64_t index,
        WCacheSlicePtr &slice) const;
    BResult Prepare(CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t offset, uint64_t index, PutRequest *&req);
    void PutRemote(PutRequest *req, CmPtInfo &ptEntry, std::vector<uint32_t> &index, RpcEngine::Callback &callback);
    void PutLocal(PutRequest *req, uint32_t localIdx, RpcEngine::Callback &callback) const;
    BResult SendPutRequest(CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t offset, uint64_t index);

    BResult GetMaster(GetRequest &req, uint16_t masterNid, char *value, uint64_t &realLen);
    BResult SendGetRequest(CmPtInfo &ptEntry, GetRequest &req, char *value, uint64_t &realLen);

    void DeleteRemote(DeleteRequest &req, CmPtInfo &ptEntry, uint32_t index, RpcEngine::Callback &callback);
    void DeleteLocal(DeleteRequest &req, RpcEngine::Callback &callback) const;
    BResult SendDeleteRequest(CmPtInfo &ptEntry, DeleteRequest &req);

    BResult StatRemote(uint16_t dstNid, StatRequest &req, Bio::ObjStat &objInfo);
    BResult StatLocal(StatRequest &req, Bio::ObjStat &objInfo) const;
    BResult SendStatRequest(CmPtInfo &ptEntry, StatRequest &req, Bio::ObjStat &objInfo);

    BResult LoadMaster(LoadRequest &req, uint16_t masterNid, const Bio::LoadCallback &callback, void *context);
    BResult SendLoadRequest(CmPtInfo &ptEntry, LoadRequest &req, const Bio::LoadCallback &callback, void *context);

    inline BResult Insert(uint16_t ptId, uint64_t flowId)
    {
        std::lock_guard<std::mutex> guard(mLock);
        if (UNLIKELY(mFlowMap.size() > defaultMaxFlowSize)) {
            return BIO_ERR;
        }
        auto it = mFlowMap.find(flowId);
        if (UNLIKELY(it != mFlowMap.end())) {
            return BIO_OK;
        }
        mFlowMap[ptId] = new FlowInstance(flowId);
        return BIO_OK;
    }

    inline void Delete(uint16_t ptId)
    {
        std::lock_guard<std::mutex> guard(mLock);
        auto it = mFlowMap.find(ptId);
        if (UNLIKELY(it == mFlowMap.end())) {
            return;
        }
        delete it->second;
        mFlowMap.erase(it);
    }

    inline FlowInstance *Query(uint16_t ptId)
    {
        std::lock_guard<std::mutex> guard(mLock);
        auto it = mFlowMap.find(ptId);
        if (LIKELY(it != mFlowMap.end())) {
            return it->second;
        }
        return nullptr;
    }

private:
    std::unordered_map<uint16_t, FlowInstance *> mFlowMap;
    std::mutex mLock;
    int32_t mDeployType = 1;
    CmNodeId mLocalNid;
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> mNodeView;
    std::map<uint16_t, CmPtInfo> mPtView;
    CacheSliceOperator mSliceOp;
    DEFINE_REF_COUNT_VARIABLE
};

using MirrorClientPtr = Ref<MirrorClient>;
}
}
#endif