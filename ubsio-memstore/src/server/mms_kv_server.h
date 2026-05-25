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

#ifndef MMS_KV_SERVER_H
#define MMS_KV_SERVER_H

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>
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

using IoHandle = std::function<BResult(void *ioBuff, uint32_t ioLen)>;

class MmsKvServer;
class MmsNotifyDispatcher;
struct NotifyEvent;
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
    BResult SendSingleReq(const IoHandle &handle, IOCtxItem &item);
    BResult HandleSendReqs(std::vector<IOCtxItem> &ctxItems, const IoHandle &handle, bool freeBlocks = true);
    BResult PutLocal(void *ioBuff, uint32_t ioLen, bool notifyDataChange = false);
    BResult Put(PutItems *itemList, uint32_t itemNum);
    BResult Get(GetItems *itemList, uint32_t itemNum);
    BResult Update(UpdateItems *itemList, uint32_t itemNum);
    BResult Delete(DeleteItems *itemList, uint32_t itemNum);
    BResult Replace(ReplaceItems *itemList, uint32_t itemNum);

    BResult GetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum);
    BResult GetValuesByRange(const char *start, const char *end, ValueInfo **valueInfoItems, uint64_t *itemNum);
    BResult BatchDeleteByRange(const char *start, const char *end);
    void FreeResources(ValueInfo **valueInfoItems, uint64_t itemNum);

    void NotifyServiceable(bool serviceable);
    void NotifyPtMigrate(uint16_t ptId);
    void RemoveNotifyClient(uint32_t pid);

    static thread_local std::vector<DecodePutItem> itemListPut;
    static thread_local std::vector<DecodeUpdateItem> itemListUpdate;
    static thread_local std::vector<DecodeDeleteItem> itemListDelete;

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    void RegisterOpcode();
    BResult HandleBasic(ServiceContext &ctx);
    BResult HandleServiceable(ServiceContext &ctx);
    BResult HandleNotifySubscribe(ServiceContext &ctx);

    BResult HandlePut(ServiceContext &ctx);
    BResult HandlePutDefImpl(void *ioBuff, uint32_t ioLen);
    BResult HandlePutMultiImpl(void *ioBuff, uint32_t ioLen);
    BResult HandlePutRemote(ServiceContext &ctx);
    BResult HandlePutRemoteMulti(ServiceContext &ctx);

    void SendRemoteMulticast(void *ioBuff, uint32_t ioLen, Callback &callback);
    void PutRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback);

    BResult HandleUpdate(ServiceContext &ctx);
    BResult HandleUpdateDefImpl(void *ioBuff, uint32_t ioLen);
    BResult HandleUpdateMultiImpl(void *ioBuff, uint32_t ioLen);
    BResult HandleUpdateRemote(ServiceContext &ctx);
    BResult HandleUpdateRemoteMulti(ServiceContext &ctx);

    void UpdateRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback);
    BResult UpdateLocal(void *ioBuff, uint32_t ioLen);

    BResult HandleDelete(ServiceContext &ctx);
    BResult HandleDeleteDefImpl(void *ioBuff, uint32_t ioLen);
    BResult HandleDeleteMultiImpl(void *ioBuff, uint32_t ioLen);
    BResult HandleDeleteRemote(ServiceContext &ctx);
    BResult HandleDeleteRemoteMulti(ServiceContext &ctx);

    void DeleteRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback);
    BResult DeleteLocal(void *ioBuff, uint32_t ioLen, bool notifyDataChange = false);

    BResult HandleRangeDeleteDefImpl(void *ioBuff, uint32_t ioLen);
    BResult HandleRangeDeleteMultiImpl(void *ioBuff, uint32_t ioLen);
    BResult HandleRangeDeleteRemote(ServiceContext &ctx);
    BResult HandleRangeDeleteRemoteMulti(ServiceContext &ctx);
    void RangeDeleteRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback);
    BResult RangeDeleteLocal(void *ioBuff, uint32_t ioLen);

    BResult HandleReplace(ServiceContext &ctx);
    BResult HandleReplaceDefImpl(void *ioBuff, uint32_t ioLen);
    BResult HandleReplaceMultiImpl(void *ioBuff, uint32_t ioLen);
    BResult HandleReplaceRemote(ServiceContext &ctx);
    BResult HandleReplaceRemoteMulti(ServiceContext &ctx);
    void ReplaceRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback);
    BResult ReplaceLocal(void *ioBuff, uint32_t ioLen);

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

    BResult HandlePrefixSearch(ServiceContext &ctx);
    BResult HandleRangeSearch(ServiceContext &ctx);
    BResult HandleRangeDelete(ServiceContext &ctx);
    BResult HandleSearch(ServiceContext &ctx, MmsOpCode opCode);
    BResult ReplySearchResult(ServiceContext &ctx, ValueInfo *valueInfoItems, uint64_t itemNum);
    void AddNotifyClient(uint32_t pid, uint32_t groupIndex);
    void NotifyRemoteClientBatch(const NotifyEvent *events, uint16_t eventNum);

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
    MmsNotifyDispatcher *mNotifyDispatcher = nullptr;
    bool mDataChangeCallbackSwitch = false;
    bool mArtQuerySwitch = false;
    bool mRemoteNotifyEnable = false;
    std::atomic<uint32_t> mNotifyClientPid{0};
    std::atomic<uint32_t> mNotifyClientGroupIndex{0};
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
