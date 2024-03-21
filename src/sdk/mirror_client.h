/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef MIRROR_CLIENT_H
#define MIRROR_CLIENT_H

#include <semaphore.h>
#include <atomic>
#include <unordered_map>
#include "cm.h"
#include "bio_ref.h"
#include "bio_lock.h"
#include "message.h"
#include "bio.h"
#include "flow_instance.h"
#include "flow.h"
#include "slice.h"
#include "cache_def.h"
#include "cache_slice.h"
#include "cache_slice_operator.h"

namespace ock {
namespace bio {
constexpr uint16_t BIO_INIT_TIMEOUT_TIME = 90;
constexpr uint16_t BIO_IO_TIMEOUT_TIME = 60;
constexpr uint16_t BIO_IO_INTERAL_TIME = 2;
class MirrorClient {
public:
    struct MirrorPut {
        CacheAttr attr;
        const char *key;
        const char *value;
        uint64_t length;
        ObjLocation location;
        uint64_t flowId;
        uint64_t flowOffset;
        uint64_t flowIndex;
    };

    struct MirrorGet {
        CacheAttr attr;
        const char *key;
        char *value;
        uint64_t offset;
        uint64_t length;
        ObjLocation location;
    };

    static constexpr uint32_t defaultMaxFlowSize = 1024;
    static constexpr uint32_t SPLIT_PAGE_SIZE = 4 * 1024;

    BResult Initialize();
    BResult Start();

    explicit MirrorClient(WorkerMode mode) : mMode(mode) {}
    ~MirrorClient() = default;

    uint16_t SelectingPt(uint64_t objectId, AffinityStrategy affinity);

    inline uint16_t ParseLocation(ObjLocation location)
    {
        return static_cast<uint16_t>(location.location[0]);
    }

    BResult Put(MirrorPut &param);

    BResult Get(MirrorGet &param, uint64_t &realLen);

    BResult DeleteKey(const char *key, const ObjLocation &location);

    BResult Load(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
        const Bio::LoadCallback &callback, void *context);

    BResult ListAll(const char *prefix, std::unordered_map<std::string, ObjStat> &objs);

    BResult StatObject(const char *key, const ObjLocation &location, ObjStat &stat);

    DEFINE_REF_COUNT_FUNCTIONS;

    std::vector<uint16_t> ListLocalAffinityPt();

    inline uint16_t GetNetProtocol()
    {
        return mNetProtocol;
    }

    inline CmNodeId &GetLocalNodeInfo()
    {
        return mLocalNid;
    }

    inline bool CheckIsOnline(uint16_t nodeId, std::string &ip, uint16_t &port)
    {
        CmNodeId node(mLocalNid.groupId, nodeId);
        bool isOnline = false;
        mLock.LockRead();
        if (mNodeView.find(node) != mNodeView.end()) {
            if (mNodeView[node].status == CM_NODE_NORMAL) {
                isOnline = true;
                ip = mNodeView[node].ip;
                port = mNodeView[node].port;
            }
            mLock.UnLock();
            return isOnline;
        }
        mLock.UnLock();
        return false;
    }

    inline std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &GetNodeView()
    {
        return mNodeView;
    }

    inline std::map<uint16_t, CmPtInfo> &GetPtView()
    {
        return mPtView;
    }

    BResult RebuildNodeView(uint64_t &realNodeTimes);

    BResult RebuildPtView(uint64_t &realPtTimes);

private:
    BResult PutImpl(MirrorPut &param);

    BResult GetImpl(MirrorGet &param, uint64_t &realLen);

    BResult DeleteKeyImpl(const char *key, const ObjLocation &location);

    BResult LoadImpl(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
        const Bio::LoadCallback &callback, void *context);

    BResult ListAllImpl(const char *prefix, std::unordered_map<std::string, ObjStat> &objs);

    BResult StatObjectImpl(const char *key, const ObjLocation &location, ObjStat &stat);

    inline int32_t Copy(const void *src, void *dst, const uint64_t len)
    {
        return memcpy_s(dst, len, src, len);
    }

    BResult LoadOriginView();
    BResult LoadOriginViewImpl();
    BResult LoadAffinityFlow();

    void InitCallbackCtx(ClientCallbackCtx &cbCtx, uint32_t quota);
    BResult GetPtEntry(uint16_t ptId, ock::bio::CmPtInfo &ptEntry);
    uint32_t CalcPtQuota(CmPtInfo &ptEntry);

    BResult AllocPutOffset(uint16_t ptId, uint64_t ptv, uint64_t len, uint64_t &flowId,
        uint64_t &offset, uint64_t &index);
    BResult SendCreateFlowRequestRemote(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
        uint64_t &flowId);
    BResult CreateFlowImpl(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType, uint64_t &flowId);
    BResult CreateFlow(uint16_t ptId);
    BResult DestroyFlow(uint16_t ptId);

    void ConstructPutReq(PutRequest *req, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t flowOffset,
        uint64_t flowIndex, GetSliceResponse *rsp) const;
    void ConstructPutReq(PutRequest *req, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t flowOffset,
        uint64_t flowIndex, NetMrInfo &mr) const;
    BResult DataCopy(const char *from, SliceAddrDesc *addr, uint64_t *offset, uint32_t addrNum);
    bool IsExistLocalCopy(CmPtInfo &ptEntry);
    BResult PrepareFromServer(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req);
    BResult PrepareFromClient(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req);
    BResult Prepare(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req);
    void PutRemote(PutRequest *req, CmPtInfo &ptEntry, std::vector<uint32_t> &index, NetEngine::Callback &callback);
    void PutLocal(PutRequest *req, uint32_t localIdx, NetEngine::Callback &callback) const;
    BResult SendPutRequestFast(CmPtInfo &ptEntry, MirrorPut &param);
    void StructPutRequestSlow(MirrorPut &param, CmPtInfo &ptEntry, uint32_t splitCount, PutRequest *req);
    void PutRemoteSlow(MirrorPut &param, CmPtInfo &ptEntry, std::vector<uint32_t> &index, uint32_t splitCount,
        NetEngine::Callback &callback);
    void PutLocalSlow(MirrorPut &param, CmPtInfo &ptEntry, uint32_t localIdx, NetEngine::Callback &callback);
    BResult SendPutRequestSlow(CmPtInfo &ptEntry, MirrorPut &param);
    BResult SendPutRequest(CmPtInfo &ptEntry, MirrorPut &param);

    BResult GetMasterRemote(GetRequest &req, uint16_t masterNid, char *value, uint64_t &realLen);
    BResult GetMaster(GetRequest &req, uint16_t masterNid, char *value, uint64_t &realLen);
    BResult SendGetRequest(CmPtInfo &ptEntry, GetRequest &req, char *value, uint64_t &realLen);

    void DeleteRemote(DeleteRequest &req, CmPtInfo &ptEntry, uint32_t index, NetEngine::Callback &callback);
    void DeleteLocal(DeleteRequest &req, NetEngine::Callback &callback) const;
    BResult SendDeleteRequest(CmPtInfo &ptEntry, DeleteRequest &req);

    BResult StatRemote(uint16_t dstNid, StatRequest &req, ObjStat &objInfo);
    BResult StatLocal(StatRequest &req, ObjStat &objInfo) const;
    BResult SendStatRequest(CmPtInfo &ptEntry, StatRequest &req, ObjStat &objInfo);

    BResult ListRemote(uint16_t nid, ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);
    BResult ListLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);
    BResult SendListRequest(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);

    BResult LoadMaster(LoadRequest &req, uint16_t masterNid, const Bio::LoadCallback &callback, void *context);
    BResult SendLoadRequest(CmPtInfo &ptEntry, LoadRequest &req, const Bio::LoadCallback &callback, void *context);

    inline BResult Insert(uint16_t ptId, uint64_t ptv, uint64_t flowId)
    {
        mLock.LockWrite();
        if (UNLIKELY(mFlowMap.size() > defaultMaxFlowSize)) {
            mLock.UnLock();
            return BIO_ERR;
        }
        auto it = mFlowMap.find(ptId);
        if (it != mFlowMap.end()) {
            FlowInstance *instance = it->second;
            if (instance->Version() == ptv) {
                mLock.UnLock();
                return BIO_OK;
            }
            mFlowMap.erase(it);
            delete instance;
            mFlowMap[ptId] = new FlowInstance(flowId, ptv);
            mLock.UnLock();
            return BIO_OK;
        }
        mFlowMap[ptId] = new FlowInstance(flowId, ptv);
        mLock.UnLock();
        return BIO_OK;
    }

    inline void Delete(uint16_t ptId)
    {
        mLock.LockWrite();
        auto it = mFlowMap.find(ptId);
        if (UNLIKELY(it == mFlowMap.end())) {
            mLock.UnLock();
            return;
        }
        delete it->second;
        mFlowMap.erase(it);
        mLock.UnLock();
    }

    inline FlowInstance *Query(uint16_t ptId)
    {
        mLock.LockRead();
        auto it = mFlowMap.find(ptId);
        if (LIKELY(it != mFlowMap.end())) {
            mLock.UnLock();
            return it->second;
        }
        mLock.UnLock();
        return nullptr;
    }

private:
    std::unordered_map<uint16_t, FlowInstance *> mFlowMap;
    ReadWriteLock mLock;
    WorkerMode mMode;
    CmNodeId mLocalNid;
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> mNodeView;
    std::map<uint16_t, CmPtInfo> mPtView;
    uint64_t mCurNodeTimes;
    uint64_t mCurPtTimes;
    uint16_t mNetProtocol;
    DEFINE_REF_COUNT_VARIABLE
};

using MirrorClientPtr = Ref<MirrorClient>;
}
}
#endif