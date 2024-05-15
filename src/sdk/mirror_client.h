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
#include "bio_qos.h"

namespace ock {
namespace bio {
constexpr uint16_t BIO_INIT_TIMEOUT_TIME = 90;
constexpr uint16_t BIO_IO_TIMEOUT_TIME = 60;
constexpr uint16_t BIO_IO_INTERAL_TIME = 2;
using UpdateView = std::function<void(void)>;

enum WorkerScene : uint32_t {
    SCENE_NONE = 0,
    SCENE_BIGDATA = 1
};

struct IoStratege {
    std::atomic<uint64_t> expired;
    std::atomic<uint32_t> stratege;
};

class MirrorClient {
public:
    struct MirrorPut {
        CacheAttr attr;
        char *key;
        char *value;
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

    static constexpr uint32_t DEFAULT_MAX_FLOW_SIZE = 1024;

    BResult Initialize(UpdateView updateView, uint32_t scene);
    BResult Start();

    explicit MirrorClient(WorkerMode mode) : mMode(mode), mCurNodeTimes(0), mCurPtTimes(0), mNetProtocol(0) {}
    ~MirrorClient()
    {
        delete[] mPtHit;
    }

    std::vector<uint64_t> ShowPtHit();
    void StatisticPtHit(uint16_t ptId);
    uint16_t SelectingPt(uint64_t objectId, AffinityStrategy affinity);

    inline uint16_t ParseLocation(ObjLocation location)
    {
        return static_cast<uint16_t>(location.location[0]);
    }

    BResult Put(MirrorPut &param);

    BResult Put(MirrorPut &param, CacheSpaceInfo &spaceInfo);

    BResult Get(MirrorGet &param, uint64_t &realLen);

    BResult DeleteKey(const char *key, const ObjLocation &location);

    BResult Load(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
        const Bio::LoadCallback &callback, void *context);

    BResult ListAll(const char *prefix, std::unordered_map<std::string, ObjStat> &objs);

    BResult StatObject(const char *key, const ObjLocation &location, ObjStat &stat);

    BResult GetFileLocation(uint16_t masterPtId, uint16_t slavePtId, FileLocationQueryRsp &fileLocationQueryRsp);

    BResult AllocSpace(MirrorClient::MirrorPut &param, CacheSpaceInfo &spaceInfo);

    BResult NotifyUpdate(bool &flag);

    BResult CheckUpdateReady();

    DEFINE_REF_COUNT_FUNCTIONS

    std::vector<uint16_t> ListLocalAffinityPt();

    inline uint16_t GetNetProtocol()
    {
        return mNetProtocol;
    }

    inline CmNodeId &GetLocalNodeInfo()
    {
        return mLocalNid;
    }

    inline BioQosPtr &GetQosPtr()
    {
        return mBioQos;
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

    BResult RebuildNodeView();

    BResult RebuildPtView();

    BResult GetPtEntry(uint16_t ptId, ock::bio::CmPtInfo &ptEntry);

private:
    uint16_t SelectingPtImpl(uint64_t objectId, AffinityStrategy affinity);
    BResult PreparePutWithSpace(MirrorPut &param, CmPtInfo &ptEntry, CacheSpaceInfo &spaceInfo, PutRequest *&req);
    BResult PutImpl(MirrorPut &param, uint64_t &updateQuota);
    BResult PutImpl(MirrorPut &param, CacheSpaceInfo &spaceInfo, uint64_t &updateQuota);

    BResult GetImpl(MirrorGet &param, uint64_t &realLen);

    BResult DeleteKeyImpl(const char *key, const ObjLocation &location);

    BResult LoadImpl(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
        const Bio::LoadCallback &callback, void *context);

    BResult ListAllImpl(const char *prefix, std::unordered_map<std::string, ObjStat> &objs);

    BResult StatObjectImpl(const char *key, const ObjLocation &location, ObjStat &stat);

    BResult SendNotifyUpdateRequest(bool &flag);

    BResult SendCheckUpdateReadyRequest();

    inline int32_t Copy(const void *src, void *dst, const uint64_t len)
    {
        return memcpy_s(dst, len, src, len);
    }

    BResult AllocSpaceImpl(MirrorClient::MirrorPut &param, CacheSpaceInfo &spaceInfo);
    BResult InitializeBioQos();
    BResult LoadOriginView();
    BResult LoadOriginViewImpl();
    BResult LoadAffinityFlow();

    void InitCallbackCtx(ClientCallbackCtx &cbCtx, uint32_t quota);
    uint32_t CalcPtQuota(CmPtInfo &ptEntry);

    BResult AllocPutOffset(uint16_t ptId, uint64_t ptv, uint64_t len, uint64_t &flowId, uint64_t &offset,
        uint64_t &index);
    BResult SendCreateFlowRequestRemote(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
        uint64_t &flowId, bool &isDegrade);
    BResult SendDestroyFlowRequestRemote(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint64_t flowId);
    BResult CreateFlowImpl(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint16_t opType,
        uint64_t &flowId, bool &isDegrade);
    BResult DestroyFlowImpl(uint16_t nodeId, CmPtInfo &ptEntry, uint16_t ptId, uint64_t flowId);
    BResult CreateFlow(uint16_t ptId);
    BResult DestroyFlow(uint16_t ptId, uint64_t flowId);

    void ConstructPutReq(PutRequest *req, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t flowOffset,
        uint64_t flowIndex, GetSliceResponse *rsp);
    void ConstructPutReq(PutRequest *req, CmPtInfo &ptEntry, MirrorPut &param, uint64_t flowId, uint64_t flowOffset,
        uint64_t flowIndex, NetMrInfo &mr);
    BResult DataCopy(const char *from, SliceAddrDesc *addr, uint64_t *offset, uint32_t addrNum);
    bool IsExistLocalCopy(CmPtInfo &ptEntry);
    BResult PrepareFromServer(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req);
    BResult PrepareFromClient(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req);
    BResult Prepare(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *&req);
    void PutRemote(PutRequest *req, CmPtInfo &ptEntry, std::vector<uint32_t> &index, Callback &callback);
    void PutLocal(PutRequest *req, uint32_t localIdx, Callback &callback) const;
    BResult SendPutRequestImpl(CmPtInfo &ptEntry, MirrorPut &param, PutRequest *req, uint64_t &updateQuota);
    BResult SendPutRequest(CmPtInfo &ptEntry, MirrorPut &param, uint64_t &updateQuota);

    BResult GetMasterRemote(GetRequest &req, uint16_t masterNid, char *value, uint64_t &realLen);
    BResult GetMaster(GetRequest &req, uint16_t masterNid, char *value, uint64_t &realLen);
    BResult SendGetRequest(CmPtInfo &ptEntry, GetRequest &req, char *value, uint64_t &realLen);

    void DeleteRemote(DeleteRequest &req, CmPtInfo &ptEntry, uint32_t index, Callback &callback);
    void DeleteLocal(DeleteRequest &req, Callback &callback) const;
    BResult SendDeleteRequest(CmPtInfo &ptEntry, DeleteRequest &req);

    BResult StatRemote(uint16_t dstNid, StatRequest &req, ObjStat &objInfo);
    BResult StatLocal(StatRequest &req, ObjStat &objInfo) const;
    BResult SendStatRequest(CmPtInfo &ptEntry, StatRequest &req, ObjStat &objInfo);

    BResult ListRemote(uint16_t nid, ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);
    BResult ListLocal(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);
    BResult SendListRequest(ListRequest &req, std::unordered_map<std::string, ObjStat> &objs);

    BResult LoadMaster(LoadRequest &req, uint16_t masterNid, const Bio::LoadCallback &callback, void *context);
    BResult SendLoadRequest(CmPtInfo &ptEntry, LoadRequest &req, const Bio::LoadCallback &callback, void *context);

    inline BResult Insert(uint16_t ptId, uint64_t ptv, uint64_t flowId, bool isDegrade)
    {
        mLock.LockWrite();
        if (UNLIKELY(mFlowMap.size() > DEFAULT_MAX_FLOW_SIZE)) {
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
            mFlowMap[ptId] = new FlowInstance(flowId, ptv, isDegrade);
            mLock.UnLock();
            return BIO_OK;
        }
        mFlowMap[ptId] = new FlowInstance(flowId, ptv, isDegrade);
        mLock.UnLock();
        return BIO_OK;
    }

    inline void Delete(uint16_t ptId, uint64_t flowId)
    {
        mLock.LockWrite();
        auto it = mFlowMap.find(ptId);
        if (UNLIKELY(it == mFlowMap.end())) {
            mLock.UnLock();
            return;
        }
        if (it->second->FlowId() != flowId) {
            mLock.UnLock();
            return;
        }
        delete it->second;
        mFlowMap.erase(it);
        mLock.UnLock();

        DestroyFlow(ptId, flowId);
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
    std::map<uint16_t, IoStratege *> mIoStratege;
    uint64_t mCurNodeTimes;
    uint64_t mCurPtTimes;
    uint16_t mNetProtocol;
    UpdateView mUpdateView { nullptr };
    WorkerScene mScene = SCENE_NONE;
    std::atomic<uint64_t> *mPtHit = nullptr;
    BioQosPtr mBioQos = nullptr;
    DEFINE_REF_COUNT_VARIABLE
};
using MirrorClientPtr = Ref<MirrorClient>;
}
}
#endif