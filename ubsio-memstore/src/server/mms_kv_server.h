/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef MMS_KV_SERVER_H
#define MMS_KV_SERVER_H

#include <utility>
#include "mms_c.h"
#include "mms_ref.h"
#include "mms_err.h"
#include "mms_lock.h"
#include "mms_sequence.h"
#include "mms_message.h"
#include "net_engine.h"
#include "net_multicast_engine.h"
#include "mms_mem_mgr.h"
#include "mms_mem_allocator.h"
#include "mms_cache.h"
#include "cm.h"

namespace ock {
namespace mms {

using IoHandle = std::function<BResult(uint64_t userId, void *ioBuff, uint32_t ioLen)>;

class MmsKvServer;
using MmsKvServerPtr = Ref<MmsKvServer>;
class MmsKvServer {
public:
    MmsKvServer() = default;
    ~MmsKvServer() = default;

    BResult Initialize();

    inline static MmsKvServerPtr &Instance()
    {
        static auto instance = MakeRef<MmsKvServer>();
        return instance;
    }

    void FreeBlocks(std::vector<IOCtxItem> &ctxItems);
    BResult SendSingleReq(uint64_t userId, const IoHandle &handle, IOCtxItem &item);
    BResult HandleSendReqs(std::vector<IOCtxItem> &ctxItems, uint64_t userId, const IoHandle &handle);
    BResult PutLocal(void *ioBuff, uint32_t ioLen);
    BResult Put(uint64_t userId, PutItems *itemList, uint32_t itemNum);
    BResult Get(uint64_t userId, GetItems *itemList, uint32_t itemNum);
    BResult Update(uint64_t userId, UpdateItems *itemList, uint32_t itemNum);
    BResult Delete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum);
    BResult Replace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum);

    void NotifyServiceable(bool serviceable);
    void NotifyPtMigrate(uint16_t ptId);

    static thread_local std::vector<PutItems> itemListPut;
    static thread_local std::vector<UpdateItems> itemListUpdate;
    static thread_local std::vector<DeleteItems> itemListDelete;

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    void RegisterOpcode();
    BResult HandleBasic(ServiceContext &ctx);
    BResult HandleServiceable(ServiceContext &ctx);

    BResult HandlePut(ServiceContext &ctx);
    BResult HandlePutDefImpl(uint64_t userId, void *ioBuff, uint32_t ioLen);
    BResult HandlePutMultiImpl(uint64_t userId, void *ioBuff, uint32_t ioLen);
    BResult HandlePutRemote(ServiceContext &ctx);
    BResult HandlePutRemoteMulti(ServiceContext &ctx);

    void PutRemoteMulticast(const std::unordered_set<std::string> &remoteIps, void *ioBuff, uint32_t ioLen,
                            Callback &callback);
    void PutRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback);

    BResult HandleUpdate(ServiceContext &ctx);
    BResult HandleUpdateDefImpl(uint64_t userId, void *ioBuff, uint32_t ioLen);
    BResult HandleUpdateMultiImpl(uint64_t userId, void *ioBuff, uint32_t ioLen);
    BResult HandleUpdateRemote(ServiceContext &ctx);
    BResult HandleUpdateRemoteMulti(ServiceContext &ctx);

    void UpdateRemoteMulticast(const std::unordered_set<std::string> &remoteIps, void *ioBuff, uint32_t ioLen,
                               Callback &callback);
    void UpdateRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback);
    BResult UpdateLocal(void *ioBuff, uint32_t ioLen);

    BResult HandleDelete(ServiceContext &ctx);
    BResult HandleDeleteDefImpl(uint64_t userId, void *ioBuff, uint32_t ioLen);
    BResult HandleDeleteMultiImpl(uint64_t userId, void *ioBuff, uint32_t ioLen);
    BResult HandleDeleteRemote(ServiceContext &ctx);
    BResult HandleDeleteRemoteMulti(ServiceContext &ctx);

    void DeleteRemoteMulticast(const std::unordered_set<std::string> &remoteIps, void *ioBuff, uint32_t ioLen,
                               Callback &callback);
    void DeleteRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback);
    BResult DeleteLocal(void *ioBuff, uint32_t ioLen);

    BResult HandleReplace(ServiceContext &ctx);
    BResult HandleReplaceDefImpl(uint64_t userId, void *ioBuff, uint32_t ioLen);
    BResult HandleReplaceMultiImpl(uint64_t userId, void *ioBuff, uint32_t ioLen);
    BResult HandleReplaceRemote(ServiceContext &ctx);
    BResult HandleReplaceRemoteMulti(ServiceContext &ctx);

    void ReplaceRemoteMulticast(const std::unordered_set<std::string> &remoteIps, void *ioBuff, uint32_t ioLen,
                                Callback &callback);
    void ReplaceRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback);
    BResult ReplaceLocal(void *ioBuff, uint32_t ioLen);

    BResult FailHandle(BResult lastRet, uint64_t userId, void *ioBuff, uint32_t ioLen, const IoHandle &handle);

    BResult NotifyPtMigrateImpl(uint16_t ptId);
    BResult GetSeqNoList(uint64_t seqList[], uint32_t &seqNum, uint16_t ptId, uint64_t ptv,
                         uint16_t groupIndex, uint16_t nid);
    BResult MergeSeqNoList(uint64_t negoSeqList[], uint16_t negoLocList[], uint32_t &negoSeqNum,
                           uint64_t seqList[], uint32_t seqNum, uint16_t remoteId);
    BResult SyncSeqNoData(uint64_t negoSeqNo, uint16_t negoLocId, uint64_t seqList[], uint32_t seqNum,
                          uint16_t remoteId, CmPtInfo &ptInfo, uint16_t groupIndex);
    BResult GetSeqNoData(uint64_t negoSeqNo, uint16_t negoLocId, CmPtInfo &ptInfo, uint16_t groupIndex,
                         void **data, uint32_t &len);
    BResult PutSeqNoData(uint64_t negoSeqNo, uint16_t negoLocId, CmPtInfo &ptInfo, uint16_t groupIndex,
                         void *data, uint32_t len);
    BResult HandleGetSeqNoList(ServiceContext &ctx);
    BResult HandleGetSeqNoData(ServiceContext &ctx);
    BResult HandleUpdatePtVersion(ServiceContext &ctx);

private:
    bool mStarted = false;
    std::mutex mStartLock;

    MmsSequencePtr mSequence = nullptr;
    NetEnginePtr mNetEngine = nullptr;
    NetMulticastEnginePtr mMulticastEngine = nullptr;
    MmsMemMgrPtr mMemMgr = nullptr;
    MmsMemAllocatorPtr mMemAllocator = nullptr;
    CachePtr mCache = nullptr;
    CmPtr mCm = nullptr;
    uint32_t mIoTimeOut = NO_60;
    uint32_t mIoCtxBuffLen;

    static uint32_t mMaxPutItemNum;
    static uint32_t mMaxUpdateItemNum;
    static uint32_t mMaxDeleteItemNum;

    bool mMulticast = false;
    std::atomic<bool> mServiceable{false};
    AllocFunc allocFunc = nullptr;

    DEFINE_REF_COUNT_VARIABLE;
};
} // mms
} // ock
#endif
