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

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

#include "mms_log.h"
#include "mms_comm.h"
#include "mms_functions.h"
#include "mms_config_instance.h"
#include "mms_server.h"
#include "mms_trace.h"
#include "mms_notify.h"
#include "securec.h"
#include "mms_kv_server.h"

namespace ock {
namespace mms {

static thread_local uint16_t g_groupIndex = NumaGroupIndex::Instance()->GetGroupIndex(); // one-time execution
static constexpr uint16_t IO_LOC_DESC_LEN = sizeof(IoLocDesc);
static constexpr uint64_t GET_RSP_BUFFER_SIZE = IO_SIZE_256K;
static constexpr uint32_t PT_GROUP_HASH_FACTOR = 0x9E3779B1U;
static thread_local std::array<char, GET_RSP_BUFFER_SIZE> g_getRspBuffer;

uint32_t MmsKvServer::mMaxPutItemNum = 0;
uint32_t MmsKvServer::mMaxUpdateItemNum = 0;
uint32_t MmsKvServer::mMaxDeleteItemNum = 0;

struct SendResultContext {
    uint32_t itemIndex;
    BResult failedRet;
    bool withValue;
};

struct IoItemResultResponse {
    uint32_t itemNum;
    int32_t results[0];
};

struct RemoteIoCbCtx {
    std::atomic<uint16_t> quota;
    std::atomic<int32_t> result;
    void *ioBuff;
    uint32_t ioLen;
    bool withValue;
    bool applyResults;

    RemoteIoCbCtx(uint16_t q, void *buff, uint32_t len, bool hasValue, bool apply)
        : quota(q), result(MMS_OK), ioBuff(buff), ioLen(len), withValue(hasValue), applyResults(apply)
    {}
};

struct RoutedPutTask {
    uint16_t ptId{0};
    std::vector<uint32_t> itemIndexes;
    std::vector<PutItems> items;
    std::vector<char *> valueAddrs;
    std::vector<IOCtxItem> ctxItems;
};

struct RoutedPutAsyncOp {
    RoutedPutTask *task{nullptr};
    IOCtxItem *ctxItem{nullptr};
    uint16_t ptId{0};
    uint16_t groupIndex{0};
    uint64_t seqNo{0};
    uint16_t remoteNum{0};
    std::array<uint16_t, MAX_NODES_NUM> remoteIds{};
    bool localCopy{false};
    bool sequenceApplied{false};
    bool oneSide{false};
    BResult result{MMS_OK};
    std::unique_ptr<RemoteIoCbCtx> callbackCtx;
    std::vector<OneSideIoRequest> oneSideReqs;
};

static constexpr uint32_t ROUTED_PUT_MAX_INFLIGHT = NO_32;

struct RoutedBatchGroups {
    std::array<std::vector<uint32_t>, MMS_ROUTE_MAX_PT_NUM> itemIndexes;
    std::vector<uint16_t> ptIds;

    void Reset()
    {
        for (auto ptId : ptIds) {
            itemIndexes[ptId].clear();
        }
        ptIds.clear();
    }
};

static thread_local RoutedBatchGroups g_routedBatchGroups;

static uint16_t SelectPtWorkerGroup(uint16_t ptId, uint16_t groupNum)
{
    if (groupNum <= NO_1) {
        return NO_0;
    }
    uint32_t mixed = static_cast<uint32_t>(ptId) * PT_GROUP_HASH_FACTOR;
    mixed ^= mixed >> 16;
    return static_cast<uint16_t>(mixed % groupNum);
}

struct ServerDirectBufferCache {
    MmsMemAllocatorPtr allocator{nullptr};
    uintptr_t buff{0};
    uint64_t size{0};
    uint16_t numaId{0};

    ~ServerDirectBufferCache()
    {
        Reset();
    }

    void Reset()
    {
        if (buff != 0 && allocator != nullptr) {
            allocator->MmsFreeDirect(buff);
        }
        allocator = nullptr;
        buff = 0;
        size = 0;
        numaId = 0;
    }

    BResult Reserve(const MmsMemAllocatorPtr &curAllocator, uint64_t reqSize, uintptr_t &blockAddr)
    {
        if (UNLIKELY(curAllocator == nullptr || reqSize == 0)) {
            return MMS_ALLOC_FAIL;
        }
        if (buff != 0 && (allocator != curAllocator || size < reqSize)) {
            Reset();
        }
        if (buff == 0) {
            auto ret = curAllocator->MmsAllocDirect(reqSize, numaId, buff);
            if (UNLIKELY(ret != MMS_OK)) {
                return ret;
            }
            allocator = curAllocator;
            size = reqSize;
        }
        blockAddr = buff;
        return MMS_OK;
    }
};

template<typename Item>
static BResult BuildRoutedPtGroups(const CmPtr &cm, const std::vector<Item> &items, uint32_t itemNum,
                                   RoutedBatchGroups &groups)
{
    groups.Reset();
    groups.ptIds.reserve(std::min<uint32_t>(itemNum, MMS_ROUTE_MAX_PT_NUM));

    static thread_local std::vector<uint16_t> ptIds;
    ptIds.resize(itemNum);
    auto ret = cm->ResolveBatchPtIdsFromItems(items.data(), itemNum, ptIds.data());
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    for (uint32_t index = 0; index < itemNum; ++index) {
        uint16_t ptId = ptIds[index];
        if (UNLIKELY(ptId >= MMS_ROUTE_MAX_PT_NUM)) {
            return MMS_INVALID_PARAM;
        }

        auto &itemIndexes = groups.itemIndexes[ptId];
        if (itemIndexes.empty()) {
            groups.ptIds.push_back(ptId);
        }
        itemIndexes.push_back(index);
    }
    return MMS_OK;
}

template<typename Visitor>
static BResult WalkIoItemDescs(void *ioBuff, uint32_t ioLen, bool withValue, Visitor &&visitor)
{
    if (UNLIKELY(ioBuff == nullptr || ioLen < sizeof(IoDataRequest))) {
        return MMS_INVALID_PARAM;
    }

    auto *req = static_cast<IoDataRequest *>(ioBuff);
    uint64_t offset = sizeof(IoDataRequest);
    for (uint32_t index = 0; index < req->num; ++index) {
        if (UNLIKELY(offset > ioLen || sizeof(IoLocDesc) > ioLen - offset)) {
            return MMS_INVALID_PARAM;
        }
        auto *desc = reinterpret_cast<IoLocDesc *>(static_cast<uint8_t *>(ioBuff) + offset);
        uint64_t itemLen = sizeof(IoLocDesc) + desc->keyLen + (withValue ? desc->valueLen : 0);
        if (UNLIKELY(itemLen > ioLen - offset)) {
            return MMS_INVALID_PARAM;
        }
        visitor(index, *desc);
        offset += itemLen;
    }
    return MMS_OK;
}

static BResult BuildIoItemResultResponse(void *ioBuff, uint32_t ioLen, bool withValue,
                                         std::vector<uint8_t> &response)
{
    if (UNLIKELY(ioBuff == nullptr || ioLen < sizeof(IoDataRequest))) {
        return MMS_INVALID_PARAM;
    }
    auto *req = static_cast<IoDataRequest *>(ioBuff);
    uint64_t responseLen = sizeof(IoItemResultResponse) + static_cast<uint64_t>(req->num) * sizeof(int32_t);
    if (UNLIKELY(responseLen > UINT32_MAX)) {
        return MMS_INVALID_PARAM;
    }
    response.resize(static_cast<size_t>(responseLen));
    auto *rsp = reinterpret_cast<IoItemResultResponse *>(response.data());
    rsp->itemNum = req->num;
    return WalkIoItemDescs(ioBuff, ioLen, withValue,
        [rsp](uint32_t index, IoLocDesc &desc) { rsp->results[index] = desc.result; });
}

static BResult ApplyIoItemResultResponse(void *ioBuff, uint32_t ioLen, bool withValue,
                                         const void *response, uint32_t responseLen)
{
    if (UNLIKELY(ioBuff == nullptr || response == nullptr || ioLen < sizeof(IoDataRequest) ||
                 responseLen < sizeof(IoItemResultResponse))) {
        return MMS_INVALID_PARAM;
    }
    auto *req = static_cast<IoDataRequest *>(ioBuff);
    auto *rsp = static_cast<const IoItemResultResponse *>(response);
    uint64_t expectedLen = sizeof(IoItemResultResponse) + static_cast<uint64_t>(rsp->itemNum) * sizeof(int32_t);
    if (UNLIKELY(rsp->itemNum != req->num || expectedLen != responseLen)) {
        return MMS_INVALID_PARAM;
    }
    return WalkIoItemDescs(ioBuff, ioLen, withValue,
        [rsp](uint32_t index, IoLocDesc &desc) { desc.result = rsp->results[index]; });
}

static void HandleRemoteIoCallback(void *ctx, void *resp, uint32_t len, int32_t result)
{
    auto *cbCtx = static_cast<RemoteIoCbCtx *>(ctx);
    int32_t callbackResult = result;
    if (cbCtx->applyResults && resp != nullptr) {
        auto applyRet = ApplyIoItemResultResponse(cbCtx->ioBuff, cbCtx->ioLen, cbCtx->withValue, resp, len);
        if (UNLIKELY(applyRet != MMS_OK && callbackResult == MMS_OK)) {
            callbackResult = applyRet;
        }
    } else if (cbCtx->applyResults && callbackResult == MMS_OK) {
        callbackResult = MMS_INNER_ERR;
    }
    if (UNLIKELY(callbackResult != MMS_OK)) {
        int32_t expected = MMS_OK;
        cbCtx->result.compare_exchange_strong(expected, callbackResult, std::memory_order_relaxed);
    }
    cbCtx->quota.fetch_sub(NO_1, std::memory_order_release);
}

template<typename Reply>
static void ReplyIoItemResults(Reply &&reply, BResult result, void *ioBuff, uint32_t ioLen, bool withValue)
{
    static thread_local std::vector<uint8_t> response;
    auto buildRet = BuildIoItemResultResponse(ioBuff, ioLen, withValue, response);
    if (UNLIKELY(buildRet != MMS_OK)) {
        reply((result == MMS_OK) ? buildRet : result, nullptr, 0);
        return;
    }
    reply(result, response.data(), static_cast<uint32_t>(response.size()));
}

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

BResult MmsKvServer::Initialize()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return MMS_OK;
    }

    uint32_t lev1Cap = static_cast<uint32_t>(MmsServer::Instance()->GetConfig()->GetCmConfig().nodeNum);
    const auto &config = MmsServer::Instance()->GetConfig();
    uint32_t lev2Cap = config->IsSingleNode()
        ? static_cast<uint32_t>(NO_1)
        : static_cast<uint32_t>(config->GetNetConfig().rpcWorkerGroupsNum);
    mSequence = MmsSequence::Instance();
    auto ret = mSequence->Initialize(lev1Cap, lev2Cap);
    if (ret != MMS_OK) {
        return ret;
    }
    bool sequence = MmsServer::Instance()->GetConfig()->GetBasicConfig().sequenceSwitch;
    mSequence->SetEnable(sequence);

    mNetEngine = MmsServer::Instance()->GetNetEngine();
    mMulticastEngine = MmsServer::Instance()->GetMulticastEngine();
    mMemMgr = MmsServer::Instance()->GetMemMgr();
    mCache = MmsServer::Instance()->GetCache();
    mCm = MmsServer::Instance()->GetCm();
    mNotifyDispatcher = &MmsNotifyDispatcher::Instance();
    mDataChangeCallbackSwitch = MmsServer::Instance()->GetConfig()->GetBasicConfig().dataChangeCallbackSwitch;
    bool isSeparate = MmsServer::Instance()->GetConfig()->GetBasicConfig().isSeparateMode;
    mSeparateMode = isSeparate;
    if (mDataChangeCallbackSwitch && isSeparate) {
        auto notifyRet = mNotifyShmPublisher.Initialize(MmsServer::Instance()->GetConfig()->GetNotifyShmConfig());
        if (notifyRet != MMS_OK) {
            LOG_ERROR("Initialize notify shm publisher failed, ret:" << notifyRet << ".");
            return notifyRet;
        }
    }

    mIoTimeOut = MmsServer::Instance()->GetConfig()->GetCmConfig().registeredTimeoutSec * IO_RETRY_NUM;
    mMulticast = !config->IsSingleNode() && config->GetBasicConfig().multicastSwitch;
    mMemAllocator = mMulticast ? MmsServer::Instance()->GetMemAllocator() :
                                 MmsServer::Instance()->GetForwardMemAllocator();
    if (UNLIKELY(mMemAllocator == nullptr)) {
        LOG_ERROR("IO staging memory allocator is not initialized, multicast:" << mMulticast << ".");
        return MMS_NOT_INITIALIZED;
    }
    allocFunc = [this](uint64_t size, uint16_t &numaId, uintptr_t &blockAddr) {
        return mMulticast ? mMemAllocator->MmsAlloc(size, numaId, blockAddr) :
                            mMemAllocator->MmsAllocDirect(size, numaId, blockAddr);
    };

    const auto &cmConfig = config->GetCmConfig();
    mKeyRouteEnabled = cmConfig.replicaNum < cmConfig.nodeNum;
    mReplicaNum = static_cast<uint16_t>(cmConfig.replicaNum);
    mRpcWorkerGroupNum = std::max<uint16_t>(NO_1, config->GetNetConfig().rpcWorkerGroupsNum);
    uint32_t ioCtxBuffLen = MmsServer::Instance()->GetConfig()->GetNetConfig().msgMaxBuffSize;
    mIoCtxBuffLen = ioCtxBuffLen;
    const auto &memConfig = MmsServer::Instance()->GetConfig()->GetMemConfig();
    mIoCtxMemSize = static_cast<uint64_t>(memConfig.numaNum) * memConfig.clientIoCtxSizePerNuma;
    MmsKvServer::mMaxPutItemNum =
        (ioCtxBuffLen - sizeof(IoDataRequest)) / (sizeof(IoLocDesc) + MIN_KEY_SIZE + MIN_VALUE_SIZE);
    MmsKvServer::mMaxUpdateItemNum = MmsKvServer::mMaxPutItemNum;
    MmsKvServer::mMaxDeleteItemNum = (ioCtxBuffLen - sizeof(IoDataRequest)) / (sizeof(IoLocDesc) + MIN_KEY_SIZE);

    RegisterOpcode();
    mStarted = true;
    return MMS_OK;
}

BResult MmsKvServer::SelectPtForIo(void *ioBuff, uint32_t ioLen, bool rangeDelete, uint16_t &ptId, uint64_t &ptv,
                                   uint16_t remoteId[], uint16_t &remoteNum)
{
    if (!mKeyRouteEnabled) {
        return mCm->GetPtInfo(ptId, ptv, remoteId, remoteNum);
    }

    if (UNLIKELY(rangeDelete)) {
        return MMS_INVALID_PARAM;
    }

    if (UNLIKELY(ioBuff == nullptr || ioLen <= sizeof(IoDataRequest))) {
        return MMS_INVALID_PARAM;
    }

    uint64_t offset = sizeof(IoDataRequest);
    auto *desc = reinterpret_cast<IoLocDesc *>(reinterpret_cast<uintptr_t>(ioBuff) + offset);
    offset += sizeof(IoLocDesc);
    if (UNLIKELY(desc->keyLen == 0 || offset + desc->keyLen > ioLen)) {
        return MMS_INVALID_PARAM;
    }

    const char *key = reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(ioBuff) + offset);
    return mCm->GetPtInfoByKey(key, static_cast<uint16_t>(desc->keyLen - NO_1),
                               ptId, ptv, remoteId, remoteNum);
}

void MmsKvServer::RegisterOpcode()
{
    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_BASIC,
        std::bind(&MmsKvServer::HandleBasic, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_SERVICEABLE,
        std::bind(&MmsKvServer::HandleServiceable, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_GET,
        std::bind(&MmsKvServer::HandleGet, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_BATCH_GET,
        std::bind(&MmsKvServer::HandleBatchGet, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_PUT,
        std::bind(&MmsKvServer::HandlePut, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_PUT,
        std::bind(&MmsKvServer::HandlePutRemote, this, std::placeholders::_1)); // 处理单点put

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_PUT_ONESIDE,
        std::bind(&MmsKvServer::HandlePutRemoteOneSide, this, std::placeholders::_1)); // 处理单点put单边拉取

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_MULTI_PUT,
        std::bind(&MmsKvServer::HandlePutRemoteMulti, this, std::placeholders::_1)); // 处理组播put

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_UPDATE,
        std::bind(&MmsKvServer::HandleUpdate, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_UPDATE,
        std::bind(&MmsKvServer::HandleUpdateRemote, this, std::placeholders::_1)); // 处理单点update

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_UPDATE_ONESIDE,
        std::bind(&MmsKvServer::HandleUpdateRemoteOneSide, this, std::placeholders::_1)); // 处理单点update单边拉取

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_MULTI_UPDATE,
        std::bind(&MmsKvServer::HandleUpdateRemoteMulti, this, std::placeholders::_1)); // 处理组播update

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_DELETE,
        std::bind(&MmsKvServer::HandleDelete, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_REPLACE,
        std::bind(&MmsKvServer::HandleReplace, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_REPLACE,
        std::bind(&MmsKvServer::HandleReplaceRemote, this, std::placeholders::_1)); // 处理单点replace

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_REPLACE_ONESIDE,
        std::bind(&MmsKvServer::HandleReplaceRemoteOneSide, this, std::placeholders::_1)); // 处理单点replace单边拉取

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_MULTI_REPLACE,
        std::bind(&MmsKvServer::HandleReplaceRemoteMulti, this, std::placeholders::_1)); // 处理组播replace

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_DELETE,
        std::bind(&MmsKvServer::HandleDeleteRemote, this, std::placeholders::_1)); // 处理单点delete

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_MULTI_DELETE,
        std::bind(&MmsKvServer::HandleDeleteRemoteMulti, this, std::placeholders::_1)); // 处理组播delete

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_GET_SEQNO_LIST,
        std::bind(&MmsKvServer::HandleGetSeqNoList, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_GET_SEQNO_DATA,
        std::bind(&MmsKvServer::HandleGetSeqNoData, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_UPDATE_PT_VERSION,
        std::bind(&MmsKvServer::HandleUpdatePtVersion, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_GET_ROUTE_VIEW,
        std::bind(&MmsKvServer::HandleGetRouteView, this, std::placeholders::_1));
}

void MmsKvServer::FreeBlocks(std::vector<IOCtxItem> &ctxItems)
{
    for (auto &item : ctxItems) {
        if (mMulticast) {
            mMemAllocator->MmsFree(item.buff);
        } else {
            mMemAllocator->MmsFreeDirect(item.buff);
        }
    }
}

BResult MmsKvServer::SendSingleReq(const IoHandle &handle, IOCtxItem &item)
{
    uint64_t startTime = Monotonic::TimeSec();
    uint16_t retryCount = 0;
    BResult ret = MMS_OK;

    do {
        ret = handle(reinterpret_cast<void *>(item.buff), static_cast<uint32_t>(item.reqLen));
        if (LIKELY(ret == MMS_OK)) {
            return MMS_OK;
        }

        LOG_ERROR("Send request failed, ret:" << ret << ", retry count:" << ++retryCount << ".");
        uint64_t costTime = Monotonic::TimeSec() - startTime;
        if (costTime >= mIoTimeOut) {
            LOG_ERROR("Send request failed after " << retryCount << " retries, exiting.");
            break;
        }

        bool isContinue = (ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY ||
                           ret == MMS_NET_RETRY || ret == MMS_CHECK_PT_FAIL);
        if (!isContinue) {
            break;
        }
        sleep(IO_RETRY_INTERAL);
    } while (true);

    return ret;
}

BResult MmsKvServer::HandleSendReqs(std::vector<IOCtxItem> &ctxItems, const IoHandle &handle, bool freeBlocks)
{
    BResult ret;
    for (auto &item : ctxItems) {
        ret = SendSingleReq(handle, item);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Send single request failed, ret:" << ret << ".");
            if (freeBlocks) {
                FreeBlocks(ctxItems);
            }
            return ret;
        }
    }

    if (freeBlocks) {
        FreeBlocks(ctxItems);
    }
    return MMS_OK;
}

bool MmsKvServer::ShouldUseRemoteOneSide(uint32_t ioLen, uint16_t remoteNum) const
{
    return !mMulticast && remoteNum != 0 && ioLen > MMS_TWOSIDE_IO_THRESHOLD;
}

BResult MmsKvServer::SendRemoteOneSideAndWait(uint16_t remoteId[], uint16_t remoteNum, MmsOpCode opCode,
                                              void *ioBuff, uint32_t ioLen, bool applyRemoteResults)
{
    if (remoteNum == 0) {
        return MMS_OK;
    }

    auto *dataReq = reinterpret_cast<IoDataRequest *>(ioBuff);
    MmsMemoryKey remoteKey{};
    auto ret = mNetEngine->GetRegisteredMemoryKey(reinterpret_cast<uintptr_t>(ioBuff), ioLen, remoteKey);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get registered memory key failed, ret:" << ret << ", ioLen:" << ioLen << ".");
        return ret;
    }

    std::vector<OneSideIoRequest> reqs(remoteNum);
    RemoteIoCbCtx cbCtx(remoteNum, ioBuff, ioLen, true, applyRemoteResults);
    Callback callback(HandleRemoteIoCallback, static_cast<void *>(&cbCtx));

    ReqHead oneSideHead = dataReq->head;
    oneSideHead.opcode = opCode;
    for (uint16_t index = 0; index < remoteNum; ++index) {
        reqs[index] = {oneSideHead, dataReq->seqNo, dataReq->negoSeqNo, reinterpret_cast<uintptr_t>(ioBuff),
                       remoteKey, ioLen, dataReq->num};
        mNetEngine->AsyncCall(remoteId[index], g_groupIndex, opCode, reqs[index], callback);
    }
    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }
    return cbCtx.result.load(std::memory_order_acquire);
}

BResult MmsKvServer::HandleRemoteOneSide(ServiceContext &ctx, const IoHandle &handle)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(OneSideIoRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive one-side message len:" << ctx.MessageDataLen() << " or message data invalid.");
        ReplyPeerRequest(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    OneSideIoRequest request = *static_cast<OneSideIoRequest *>(ctx.MessageData());
    auto *req = &request;
    if (UNLIKELY(req->ioLength < sizeof(IoDataRequest) || req->ioLength > MMS_ONESIDE_STAGING_SIZE)) {
        LOG_ERROR("Invalid one-side io length:" << req->ioLength << ".");
        ReplyPeerRequest(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint16_t numaId = 0;
    uintptr_t localBuff = 0;
    auto ret = mMemAllocator->MmsAllocDirect(req->ioLength, numaId, localBuff);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Alloc one-side local io buffer failed, ret:" << ret << ", len:" << req->ioLength << ".");
        ReplyPeerRequest(ctx, MMS_ALLOC_FAIL, nullptr, 0);
        return MMS_OK;
    }

    MmsMemoryKey localKey{};
    ret = mNetEngine->GetRegisteredMemoryKey(localBuff, req->ioLength, localKey);
    if (LIKELY(ret == MMS_OK)) {
        NetRequest netReq = mNetEngine->InitNetRequest(localBuff, req->remoteAddr, localKey, req->remoteKey,
                                                       req->ioLength);
        ret = mNetEngine->SyncReadOneSide(req->head.nodeId, req->head.groupIndex, netReq);
    }
    if (LIKELY(ret == MMS_OK)) {
        auto *dataReq = reinterpret_cast<IoDataRequest *>(localBuff);
        bool invalidHeader = dataReq->head.nodeId != req->head.nodeId || dataReq->head.ptId != req->head.ptId ||
            dataReq->head.ptv != req->head.ptv || dataReq->seqNo != req->seqNo ||
            dataReq->negoSeqNo != req->negoSeqNo || dataReq->num != req->reserved;
        if (UNLIKELY(invalidHeader)) {
            LOG_ERROR("One-side data header mismatch, source node:" << req->head.nodeId << ", ioLen:" << req->ioLength
                      << ", expected item num:" << req->reserved << ", actual item num:" << dataReq->num << ".");
            ret = MMS_INVALID_PARAM;
        }
    }
    bool handled = false;
    if (LIKELY(ret == MMS_OK)) {
        ret = handle(reinterpret_cast<void *>(localBuff), req->ioLength);
        handled = true;
    } else {
        LOG_ERROR("One-side read io buffer failed, ret:" << ret << ", len:" << req->ioLength << ".");
    }
    if (handled && mKeyRouteEnabled && mReplicaNum == NO_1) {
        auto reply = [this, &ctx](int32_t code, void *data, uint32_t len) {
            ReplyPeerRequest(ctx, code, data, len);
        };
        ReplyIoItemResults(reply, ret, reinterpret_cast<void *>(localBuff), req->ioLength, true);
    } else {
        ReplyPeerRequest(ctx, ret, nullptr, 0);
    }
    mMemAllocator->MmsFreeDirect(localBuff);
    return MMS_OK;
}

BResult MmsKvServer::Put(PutItems *itemList, uint32_t itemNum)
{
    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }

    static IoHandle handle = [this](void *ioBuff, uint32_t ioLen) -> BResult {
        if (mMulticast) {
            return HandlePutMultiImpl(ioBuff, ioLen);
        } else {
            return HandlePutDefImpl(ioBuff, ioLen);
        }
    };

    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    while (curItemIndex < itemNum) {
        ctxItems.clear();
        uint32_t encodeBuffLen = (itemList[curItemIndex].valueLen > MMS_TWOSIDE_IO_THRESHOLD) ?
            MMS_ONESIDE_STAGING_SIZE : mIoCtxBuffLen;
        ret = EncodePutRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc, encodeBuffLen);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(ctxItems, handle, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillPutItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillPutItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(ctxItems, handle, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillPutItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillPutItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            LOG_DEBUG("Send batch put success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                            << ".");
        } else {
            LOG_ERROR("Encode put request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

BResult MmsKvServer::Get(GetItems *itemList, uint32_t itemNum)
{
    BResult result = MMS_OK;

    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }

    for (uint32_t index = 0; index < itemNum; index++) {
        uint64_t realLength = 0;
        GetPara para = {itemList[index].key, itemList[index].keyLen, itemList[index].offset, itemList[index].length,
                        itemList[index].value, &realLength};
        auto ret = mCache->Get(para);
        *itemList[index].realLength = static_cast<uint32_t>(realLength);
        *itemList[index].result = ret;
        if (LIKELY(ret == MMS_OK)) {
            continue;
        }

        if (ret != MMS_NOT_EXISTS) {
            LOG_ERROR("Get cache failed, ret:" << ret << ", key:" << std::string(itemList[index].key,
                                                                                 itemList[index].keyLen) << ".");
        }
        result = ret;
    }

    return result;
}

BResult MmsKvServer::Update(UpdateItems *itemList, uint32_t itemNum)
{
    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }
    static IoHandle handle = [this](void *ioBuff, uint32_t ioLen) -> BResult {
        if (mMulticast) {
            return HandleUpdateMultiImpl(ioBuff, ioLen);
        } else {
            return HandleUpdateDefImpl(ioBuff, ioLen);
        }
    };

    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    while (curItemIndex < itemNum) {
        ctxItems.clear();
        uint32_t encodeBuffLen = (itemList[curItemIndex].valueLen > MMS_TWOSIDE_IO_THRESHOLD) ?
            MMS_ONESIDE_STAGING_SIZE : mIoCtxBuffLen;
        ret = EncodeUpdateRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc, encodeBuffLen);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(ctxItems, handle, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillUpdateItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(ctxItems, handle, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillUpdateItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            LOG_DEBUG("Send batch update success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                               << ".");
        } else {
            LOG_ERROR("Encode update request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

BResult MmsKvServer::Delete(DeleteItems *itemList, uint32_t itemNum)
{
    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }

    static IoHandle handle = [this](void *ioBuff, uint32_t ioLen) -> BResult {
        if (mMulticast) {
            return HandleDeleteMultiImpl(ioBuff, ioLen);
        } else {
            return HandleDeleteDefImpl(ioBuff, ioLen);
        }
    };

    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeDeleteRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc, mIoCtxBuffLen);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(ctxItems, handle, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillDeleteItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, false};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(ctxItems, handle, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillDeleteItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, false};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            LOG_DEBUG("Send batch delete success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                               << ".");
        } else {
            LOG_ERROR("Encode delete request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

BResult MmsKvServer::Replace(ReplaceItems *itemList, uint32_t itemNum)
{
    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }

    static IoHandle handle = [this](void *ioBuff, uint32_t ioLen) -> BResult {
        if (mMulticast) {
            return HandleReplaceMultiImpl(ioBuff, ioLen);
        } else {
            return HandleReplaceDefImpl(ioBuff, ioLen);
        }
    };

    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    while (curItemIndex < itemNum) {
        ctxItems.clear();
        uint32_t encodeBuffLen = (itemList[curItemIndex].valueLen > MMS_TWOSIDE_IO_THRESHOLD) ?
            MMS_ONESIDE_STAGING_SIZE : mIoCtxBuffLen;
        ret = EncodeReplaceRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc,
                                   encodeBuffLen);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(ctxItems, handle, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillReplaceItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(ctxItems, handle, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillReplaceItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            LOG_DEBUG("Send batch replace success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                               << ".");
        } else {
            LOG_ERROR("Encode replace request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

BResult MmsKvServer::HandleBasic(ServiceContext &ctx)
{
    BasicResponse rsp{};
    if (UNLIKELY(ctx.Channel() == nullptr)) {
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }
    NetChannelUpCtx channelCtx(ctx.Channel()->GetUpCtx());
    if (UNLIKELY(channelCtx.peerId != INVALID_NID || channelCtx.procId == 0)) {
        LOG_ERROR("Invalid client basic channel, node id:" << channelCtx.peerId << ", pid:" << channelCtx.procId
                                                           << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    ClientIoCtxPtr clientIoCtx;
    auto ret = MmsServer::Instance()->AcquireClientIoCtx(channelCtx.procId, clientIoCtx);
    if (UNLIKELY(ret != MMS_OK || clientIoCtx == nullptr)) {
        LOG_ERROR("Acquire client ioctx failed, pid:" << channelCtx.procId << ", ret:" << ret << ".");
        mNetEngine->Reply(ctx, ret, nullptr, 0);
        return MMS_OK;
    }

    MmsConfigPtr config = MmsServer::Instance()->GetConfig();
    rsp.serverPid = getpid();
    rsp.ioCtxProtocolVersion = MMS_IOCTX_PROTOCOL_VERSION;
    rsp.clientGeneration = clientIoCtx->generation;
    rsp.netTimeOut = config->GetCmConfig().registeredTimeoutSec; // 同zk心跳超时;
    rsp.ioTimeOut = rsp.netTimeOut * IO_RETRY_NUM; // 默认心跳超时x倍，确保IO RETRY重试OK;
    rsp.logLevel = config->GetBasicConfig().logLevel;
    rsp.traceSwitch = config->GetBasicConfig().traceSwitch;
    rsp.enableCrc = config->GetBasicConfig().crcSwitch;
    rsp.keyRouteEnabled = mKeyRouteEnabled;
    rsp.dataChangeCallbackSwitch = config->GetBasicConfig().dataChangeCallbackSwitch;
    rsp.maxMsgBuffSize = config->GetNetConfig().msgMaxBuffSize;
    rsp.valueBlockSize = config->GetMemConfig().valueBlockSize;

    int32_t fds[MMAP_AREA_BUTT];
    fds[MMAP_AREA_IOCTX] = clientIoCtx->fd;
    mMemMgr->GetAreaMemDesc(MMAP_AREA_BUCKET, fds[MMAP_AREA_BUCKET]);
    mMemMgr->GetAreaMemDesc(MMAP_AREA_VALUE, fds[MMAP_AREA_VALUE]);
    mMemMgr->GetAreaMemDesc(MMAP_AREA_INDEX, fds[MMAP_AREA_INDEX]);
    mMemMgr->GetNumaMemDesc(rsp.memNumaId, rsp.memSize, rsp.memNum);
    for (uint16_t index = 0; index < clientIoCtx->numaNum; ++index) {
        rsp.ioCtxNumaSize[index] = clientIoCtx->numaSize[index];
    }
    ret = mNetEngine->SendFds(ctx.Channel(), fds, MMAP_AREA_BUTT);
    if (ret != MMS_OK) {
        LOG_ERROR("Send fds failed, ret:" << ret << ".");
        mNetEngine->Reply(ctx, MMS_INNER_ERR, nullptr, 0);
    } else {
        mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(BasicResponse));
    }
    return MMS_OK;
}

BResult MmsKvServer::HandleServiceable(ServiceContext &ctx)
{
    ServiceResponse rsp;

    rsp.serviceable = mServiceable;
    mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(ServiceResponse));

    return MMS_OK;
}

BResult MmsKvServer::ResolveClientIoCtx(ServiceContext &ctx, uint64_t generation, uint64_t offset, uint64_t length,
                                        ClientIoCtxPtr &ioCtx, uintptr_t &address) const
{
    if (UNLIKELY(ctx.Channel() == nullptr)) {
        return MMS_INVALID_PARAM;
    }
    NetChannelUpCtx channelCtx(ctx.Channel()->GetUpCtx());
    if (UNLIKELY(channelCtx.peerId != INVALID_NID || channelCtx.procId == 0)) {
        return MMS_INVALID_PARAM;
    }
    return MmsServer::Instance()->ResolveClientIoCtx(channelCtx.procId, generation, offset, length, ioCtx, address);
}

bool MmsKvServer::IsIoCtxRequestValid(ServiceContext &ctx, const IoCtrlRequest &req, uint64_t minLength,
                                      ClientIoCtxPtr &ioCtx, uintptr_t &address) const
{
    if (req.ioLength < minLength || (mMulticast && req.ioLength > mIoCtxBuffLen)) {
        return false;
    }
    return ResolveClientIoCtx(ctx, req.clientGeneration, req.ioNumaOffset, req.ioLength, ioCtx, address) == MMS_OK;
}

BResult MmsKvServer::HandleClientIoBuffer(void *ioBuff, uint32_t ioLen, const IoHandle &handle)
{
    if (!mMulticast) {
        return handle(ioBuff, ioLen);
    }

    uint16_t numaId = 0;
    uintptr_t stagingBuff = 0;
    auto ret = mMemAllocator->MmsAlloc(ioLen, numaId, stagingBuff);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Alloc multicast client staging buffer failed, ret:" << ret << ", len:" << ioLen << ".");
        return ret;
    }
    ret = memcpy_s(reinterpret_cast<void *>(stagingBuff), ioLen, ioBuff, ioLen);
    if (LIKELY(ret == EOK)) {
        ret = handle(reinterpret_cast<void *>(stagingBuff), ioLen);
    } else {
        ret = MMS_ERR;
    }
    int32_t copyRet = memcpy_s(ioBuff, ioLen, reinterpret_cast<void *>(stagingBuff), ioLen);
    mMemAllocator->MmsFree(stagingBuff);
    if (UNLIKELY(copyRet != EOK && ret == MMS_OK)) {
        return MMS_ERR;
    }
    return ret;
}

BResult MmsKvServer::ForwardGet(ServiceContext &ctx, GetValueRequest &req, uint16_t remoteId)
{
    bool oneSide = (req.flags & MMS_GET_FLAG_ONESIDE) != 0;
    if (UNLIKELY(oneSide && (req.flags & MMS_GET_FLAG_PROXY_FORWARDED) == 0)) {
        LOG_ERROR("Direct one-side get cannot be forwarded, remote id:" << remoteId << ", key:" << req.key << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    if (oneSide) {
        GetValueResponse rsp = {};
        MMS_TRACE_START(SERVER_TRACE_GET_FORWARD);
        auto ret = mNetEngine->SyncCall<GetValueRequest, GetValueResponse>(remoteId, g_groupIndex, MMS_OP_C_GET,
                                                                            req, rsp);
        MMS_TRACE_END(SERVER_TRACE_GET_FORWARD, ret);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Forward one-side get failed, ret:" << ret << ", remote id:" << remoteId
                                                            << ", key:" << req.key << ".");
            mNetEngine->Reply(ctx, ret, nullptr, 0);
            return MMS_OK;
        }
        if (rsp.result == MMS_NOT_EXISTS) {
            LOG_DEBUG("Forward get key not found, remote id:" << remoteId << ", key:" << req.key << ".");
        }
        mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(rsp));
        return MMS_OK;
    }

    GetValueResponse *rsp = nullptr;
    uint64_t rspLen = 0;
    MMS_TRACE_START(SERVER_TRACE_GET_FORWARD);
    auto ret = mNetEngine->SyncCall<GetValueRequest, GetValueResponse>(remoteId, g_groupIndex, MMS_OP_C_GET, req, &rsp,
                                                                       rspLen);
    MMS_TRACE_END(SERVER_TRACE_GET_FORWARD, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Forward get failed, ret:" << ret << ", remote id:" << remoteId << ", key:" << req.key << ".");
        mNetEngine->Reply(ctx, ret, nullptr, 0);
        return MMS_OK;
    }

    if (rsp->result == MMS_NOT_EXISTS) {
        LOG_DEBUG("Forward get key not found, remote id:" << remoteId << ", key:" << req.key << ".");
    }
    mNetEngine->Reply(ctx, MMS_OK, rsp, rspLen);
    return MMS_OK;
}

BResult MmsKvServer::HandleGetLocalProxy(ServiceContext &ctx, const GetValueRequest &req)
{
    ClientIoCtxPtr clientIoCtx;
    uintptr_t responseBuff = 0;
    auto resolveRet = ResolveClientIoCtx(ctx, req.clientGeneration, req.valueAddr, req.length,
                                         clientIoCtx, responseBuff);
    if (UNLIKELY(resolveRet != MMS_OK)) {
        LOG_ERROR("Invalid proxy get response offset:" << req.valueAddr << ", len:" << req.length << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    GetValueResponse rsp = {};
    char *value = reinterpret_cast<char *>(responseBuff);
    GetPara para = {req.key, static_cast<uint16_t>(strnlen(req.key, MAX_KEY_SIZE)), req.offset, req.length, &value,
                    &rsp.realLength};
    MMS_TRACE_START(SERVER_TRACE_GET_CACHE);
    auto ret = mCache->Get(para);
    MMS_TRACE_END(SERVER_TRACE_GET_CACHE, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        if (ret == MMS_NOT_EXISTS) {
            rsp.result = ret;
            LOG_DEBUG("Get key not found, key:" << req.key << ".");
            mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(rsp));
            return MMS_OK;
        }
        LOG_WARN("Get cache failed, ret:" << ret << ", key:" << req.key << ".");
        mNetEngine->Reply(ctx, ret, nullptr, 0);
        return MMS_OK;
    }
    rsp.result = MMS_OK;
    mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(rsp));
    return MMS_OK;
}

BResult MmsKvServer::PrepareGetProxyForward(ServiceContext &ctx, GetValueRequest &req, ClientIoCtxPtr &clientIoCtx)
{
    uintptr_t responseBuff = 0;
    auto ret = ResolveClientIoCtx(ctx, req.clientGeneration, req.valueAddr, req.length, clientIoCtx, responseBuff);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Invalid proxy get response offset:" << req.valueAddr << ", len:" << req.length << ".");
        return MMS_INVALID_PARAM;
    }
    if (!clientIoCtx->mrRegistered) {
        LOG_ERROR("Client ioctx is not registered for remote get, pid:" << clientIoCtx->pid << ".");
        return MMS_NOT_READY;
    }
    req.valueKey = clientIoCtx->memoryKey;
    req.valueAddr = responseBuff;
    req.flags = (req.flags & ~MMS_GET_FLAG_PROXY_BUFFER) |
                MMS_GET_FLAG_ONESIDE | MMS_GET_FLAG_PROXY_FORWARDED;
    return MMS_OK;
}

BResult MmsKvServer::HandleGetLocal(ServiceContext &ctx, const GetValueRequest &req)
{
    if ((req.flags & MMS_GET_FLAG_PROXY_BUFFER) != 0) {
        return HandleGetLocalProxy(ctx, req);
    }
    if ((req.flags & MMS_GET_FLAG_ONESIDE) != 0) {
        return HandleGetLocalOneSide(ctx, req);
    }

    uint64_t rspLen = sizeof(GetValueResponse) + req.length;
    bool needFree = false;
    void *rspData = g_getRspBuffer.data();
    if (UNLIKELY(rspLen > GET_RSP_BUFFER_SIZE)) {
        rspData = malloc(rspLen);
        if (UNLIKELY(rspData == nullptr)) {
            LOG_ERROR("Malloc get response failed, len:" << rspLen << ".");
            mNetEngine->Reply(ctx, MMS_ALLOC_FAIL, nullptr, 0);
            return MMS_OK;
        }
        needFree = true;
    }

    auto *rsp = static_cast<GetValueResponse *>(rspData);
    rsp->result = MMS_OK;
    rsp->reserved = 0;
    rsp->realLength = 0;
    char *value = rsp->value;
    GetPara para = {req.key, static_cast<uint16_t>(strnlen(req.key, MAX_KEY_SIZE)), req.offset, req.length, &value,
                    &rsp->realLength};
    MMS_TRACE_START(SERVER_TRACE_GET_CACHE);
    auto ret = mCache->Get(para);
    MMS_TRACE_END(SERVER_TRACE_GET_CACHE, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        if (ret == MMS_NOT_EXISTS) {
            rsp->result = ret;
            LOG_DEBUG("Get key not found, key:" << req.key << ".");
            mNetEngine->Reply(ctx, MMS_OK, rspData, sizeof(GetValueResponse));
        } else {
            LOG_WARN("Get cache failed, ret:" << ret << ", key:" << req.key << ".");
            mNetEngine->Reply(ctx, ret, nullptr, 0);
        }
        if (needFree) {
            free(rspData);
        }
        return MMS_OK;
    }

    mNetEngine->Reply(ctx, MMS_OK, rspData, sizeof(GetValueResponse) + rsp->realLength);
    if (needFree) {
        free(rspData);
    }
    return MMS_OK;
}

BResult MmsKvServer::HandleGetLocalOneSide(ServiceContext &ctx, const GetValueRequest &req)
{
    if (UNLIKELY(req.valueAddr == 0 || req.length == 0)) {
        LOG_ERROR("Invalid one-side get param, addr:" << req.valueAddr << ", len:" << req.length << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    GetValueResponse rsp = {};
    char *directValue = nullptr;
    bool directMrUnavailable = false;
    GetPara directPara = {req.key, static_cast<uint16_t>(strnlen(req.key, MAX_KEY_SIZE)), req.offset, req.length,
                          &directValue, &rsp.realLength};
    BResult ret = MMS_OK;
    {
        MMS_TRACE_START(SERVER_TRACE_GET_CACHE);
        ret = mCache->GetDirect(directPara, [this, &ctx, &req, &directMrUnavailable]
            (const char *value, uint64_t realLength) -> BResult {
            MmsMemoryKey localKey{};
            auto directRet =
                mNetEngine->GetRegisteredMemoryKey(reinterpret_cast<uintptr_t>(value), realLength, localKey);
            if (UNLIKELY(directRet != MMS_OK)) {
                directMrUnavailable = true;
                return directRet;
            }
            NetRequest netReq = mNetEngine->InitNetRequest(reinterpret_cast<uintptr_t>(value), req.valueAddr,
                                                           localKey, req.valueKey,
                                                           static_cast<uint32_t>(realLength));
            MMS_TRACE_START(SERVER_TRACE_GET_ONESIDE_WRITE);
            directRet = mNetEngine->SyncWrite(ctx.Channel(), netReq);
            MMS_TRACE_END(SERVER_TRACE_GET_ONESIDE_WRITE, directRet);
            return directRet;
        });
        MMS_TRACE_END(SERVER_TRACE_GET_CACHE, ret);
    }
    if (!directMrUnavailable) {
        if (UNLIKELY(ret != MMS_OK)) {
            if (ret == MMS_NOT_EXISTS) {
                rsp.result = ret;
                LOG_DEBUG("Get key not found, key:" << req.key << ".");
                mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(rsp));
                return MMS_OK;
            }
            LOG_WARN("Get cache direct failed, ret:" << ret << ", key:" << req.key << ".");
        }
        rsp.result = ret;
        mNetEngine->Reply(ctx, ret, ret == MMS_OK ? &rsp : nullptr, ret == MMS_OK ? sizeof(rsp) : 0);
        return MMS_OK;
    }

    LOG_WARN("Value memory is not registered, fallback to get staging buffer, key:" << req.key << ".");
    uint16_t numaId = 0;
    uintptr_t localBuff = 0;
    MMS_TRACE_START(SERVER_TRACE_GET_ONESIDE_ALLOC);
    ret = mMemAllocator->MmsAllocDirect(req.length, numaId, localBuff);
    MMS_TRACE_END(SERVER_TRACE_GET_ONESIDE_ALLOC, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Alloc get one-side local buffer failed, ret:" << ret << ", len:" << req.length << ".");
        mNetEngine->Reply(ctx, MMS_ALLOC_FAIL, nullptr, 0);
        return MMS_OK;
    }

    char *value = reinterpret_cast<char *>(localBuff);
    GetPara para = {req.key, static_cast<uint16_t>(strnlen(req.key, MAX_KEY_SIZE)), req.offset, req.length, &value,
                    &rsp.realLength};
    MMS_TRACE_START(SERVER_TRACE_GET_CACHE);
    ret = mCache->Get(para);
    MMS_TRACE_END(SERVER_TRACE_GET_CACHE, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        if (ret == MMS_NOT_EXISTS) {
            rsp.result = ret;
            LOG_DEBUG("Get key not found, key:" << req.key << ".");
            mMemAllocator->MmsFreeDirect(localBuff);
            mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(rsp));
            return MMS_OK;
        }
        LOG_WARN("Get cache failed, ret:" << ret << ", key:" << req.key << ".");
        mMemAllocator->MmsFreeDirect(localBuff);
        mNetEngine->Reply(ctx, ret, nullptr, 0);
        return MMS_OK;
    }

    if (rsp.realLength != 0) {
        MmsMemoryKey localKey{};
        ret = mNetEngine->GetRegisteredMemoryKey(localBuff, rsp.realLength, localKey);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Get registered memory key for get response failed, ret:" << ret << ", len:" << rsp.realLength
                                                                                << ".");
            mMemAllocator->MmsFreeDirect(localBuff);
            mNetEngine->Reply(ctx, ret, nullptr, 0);
            return MMS_OK;
        }
        NetRequest netReq = mNetEngine->InitNetRequest(localBuff, req.valueAddr, localKey, req.valueKey,
                                                       static_cast<uint32_t>(rsp.realLength));
        MMS_TRACE_START(SERVER_TRACE_GET_ONESIDE_WRITE);
        ret = mNetEngine->SyncWrite(ctx.Channel(), netReq);
        MMS_TRACE_END(SERVER_TRACE_GET_ONESIDE_WRITE, ret);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("One-side write get response failed, ret:" << ret << ", len:" << rsp.realLength << ".");
            mMemAllocator->MmsFreeDirect(localBuff);
            mNetEngine->Reply(ctx, ret, nullptr, 0);
            return MMS_OK;
        }
    }

    mMemAllocator->MmsFreeDirect(localBuff);
    rsp.result = MMS_OK;
    mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(rsp));
    return MMS_OK;
}

BResult MmsKvServer::HandleBatchGet(ServiceContext &ctx)
{
    MMS_TRACE_START(SERVER_TRACE_GET_BATCH);
    auto handle = [this, &ctx]() -> BResult {
        if (UNLIKELY(ctx.MessageData() == nullptr || ctx.MessageDataLen() < sizeof(BatchGetRequest))) {
            LOG_ERROR("Invalid batch get request len:" << ctx.MessageDataLen() << ".");
            mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
            return MMS_OK;
        }

        auto *req = static_cast<BatchGetRequest *>(ctx.MessageData());
        uint64_t expectedLen = sizeof(BatchGetRequest) +
            static_cast<uint64_t>(req->itemNum) * sizeof(BatchGetItemRequest);
        uint64_t responseMetaSize = sizeof(BatchGetResponse) +
            static_cast<uint64_t>(req->itemNum) * sizeof(BatchGetItemResponse);
        bool forwarded = (req->flags & MMS_BATCH_GET_FLAG_FORWARDED) != 0;
        if (UNLIKELY(req->itemNum == 0 || expectedLen != ctx.MessageDataLen() ||
                     expectedLen > MMS_TWOSIDE_IO_THRESHOLD || req->targetNid >= MAX_NODES_NUM ||
                     (forwarded && req->valueAddr == 0) ||
                     req->responseCapacity < responseMetaSize ||
                     req->responseCapacity > MMS_ONESIDE_STAGING_SIZE)) {
            LOG_ERROR("Invalid batch get request, item num:" << req->itemNum << ", request len:" << expectedLen
                                                              << ", response cap:" << req->responseCapacity << ".");
            mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
            return MMS_OK;
        }
        if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
            mNetEngine->Reply(ctx, MMS_NOT_READY, nullptr, 0);
            return MMS_OK;
        }

        uint64_t localPtV = mCm->GetPtVersion();
        if (UNLIKELY(req->head.ptv != localPtV)) {
            mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
            return MMS_OK;
        }

        BResult routeRet = MMS_OK;
        uint16_t localNid = mCm->GetLocalNid();
        MMS_TRACE_START(SERVER_TRACE_GET_BATCH_ROUTE);
        for (uint32_t index = 0; routeRet == MMS_OK && index < req->itemNum; ++index) {
            const auto &item = req->items[index];
            if (UNLIKELY(item.keyLen == 0 || item.keyLen >= MAX_KEY_SIZE || item.key[item.keyLen] != '\0' ||
                         item.length == 0)) {
                routeRet = MMS_INVALID_PARAM;
                break;
            }
        }
        if (routeRet == MMS_OK && mKeyRouteEnabled) {
            routeRet = mCm->ValidateBatchOwnerItems(req->items, req->itemNum, req->head.ptv, req->targetNid);
        }
        for (uint32_t index = 0; routeRet == MMS_OK && !mKeyRouteEnabled && index < req->itemNum; ++index) {
            const auto &item = req->items[index];
            uint16_t ptId = 0;
            uint64_t ptv = 0;
            uint16_t remoteId[MAX_NODES_NUM] = {0};
            uint16_t remoteNum = 0;
            routeRet = mCm->GetPtInfoByKey(item.key, item.keyLen, ptId, ptv, remoteId, remoteNum);
            if (UNLIKELY(routeRet != MMS_OK)) {
                break;
            }
            uint16_t ownerNid = (remoteNum < mReplicaNum) ? localNid : remoteId[NO_0];
            if (UNLIKELY(ownerNid != req->targetNid)) {
                routeRet = MMS_NEED_UPDATE_PT_VERSION;
                break;
            }
        }
        MMS_TRACE_END(SERVER_TRACE_GET_BATCH_ROUTE, routeRet);
        if (UNLIKELY(routeRet != MMS_OK)) {
            mNetEngine->Reply(ctx, routeRet, nullptr, 0);
            return MMS_OK;
        }

        auto fillResponse = [this, req, responseMetaSize](uintptr_t responseBuff,
                                                         uint32_t &responseLength) -> BResult {
            auto *rsp = reinterpret_cast<BatchGetResponse *>(responseBuff);
            rsp->itemNum = req->itemNum;
            uint64_t valueOffset = responseMetaSize;
            BResult cacheRet = MMS_OK;
            MMS_TRACE_START(SERVER_TRACE_GET_BATCH_CACHE);
            for (uint32_t index = 0; index < req->itemNum; ++index) {
                const auto &reqItem = req->items[index];
                auto &rspItem = rsp->items[index];
                rspItem = {};
                rspItem.valueOffset = static_cast<uint32_t>(valueOffset);
                if (UNLIKELY(reqItem.length > req->responseCapacity - valueOffset)) {
                    rspItem.result = MMS_INVALID_PARAM;
                    cacheRet = MMS_INVALID_PARAM;
                    continue;
                }

                char *value = reinterpret_cast<char *>(responseBuff + valueOffset);
                uint64_t realLength = 0;
                GetPara para = {reqItem.key, reqItem.keyLen, reqItem.offset, reqItem.length, &value, &realLength};
                auto itemRet = mCache->Get(para);
                rspItem.result = itemRet;
                rspItem.realLength = static_cast<uint32_t>(realLength);
                if (LIKELY(itemRet == MMS_OK)) {
                    valueOffset += realLength;
                } else if (itemRet != MMS_NOT_EXISTS) {
                    cacheRet = itemRet;
                }
            }
            rsp->dataLength = static_cast<uint32_t>(valueOffset);
            responseLength = rsp->dataLength;
            MMS_TRACE_END(SERVER_TRACE_GET_BATCH_CACHE, cacheRet);
            return MMS_OK;
        };

        if (!forwarded) {
            BResult ret = MMS_OK;
            ClientIoCtxPtr clientIoCtx;
            uintptr_t responseBuff = 0;
            auto resolveRet = ResolveClientIoCtx(ctx, req->clientGeneration, req->responseOffset,
                                                 req->responseCapacity,
                                                 clientIoCtx, responseBuff);
            if (UNLIKELY(resolveRet != MMS_OK)) {
                LOG_ERROR("Invalid batch get response offset:" << req->responseOffset
                                                                 << ", capacity:" << req->responseCapacity
                                                                 << ", total size:" << mIoCtxMemSize << ".");
                mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
                return MMS_OK;
            }

            if (req->targetNid == localNid) {
                uint32_t responseLength = 0;
                auto ret = fillResponse(responseBuff, responseLength);
                mNetEngine->Reply(ctx, ret, nullptr, 0);
                return MMS_OK;
            }

            if (UNLIKELY(!clientIoCtx->mrRegistered)) {
                LOG_ERROR("Client ioctx is not registered for remote batch get, pid:" << clientIoCtx->pid << ".");
                mNetEngine->Reply(ctx, MMS_NOT_READY, nullptr, 0);
                return MMS_OK;
            }
            req->valueKey = clientIoCtx->memoryKey;
            req->head = {localNid, MMS_OP_C_BATCH_GET, g_groupIndex, 0, localPtV};
            req->valueAddr = responseBuff;
            req->flags |= MMS_BATCH_GET_FLAG_FORWARDED;
            BResult rspRet = MMS_OK;
            MMS_TRACE_START(SERVER_TRACE_GET_BATCH_FORWARD);
            ret = mNetEngine->SyncCall<BResult>(req->targetNid, g_groupIndex, MMS_OP_C_BATCH_GET, req,
                                                static_cast<uint32_t>(expectedLen), rspRet);
            BResult callRet = (ret == MMS_OK) ? rspRet : ret;
            MMS_TRACE_END(SERVER_TRACE_GET_BATCH_FORWARD, callRet);
            if (UNLIKELY(callRet != MMS_OK)) {
                LOG_ERROR("Forward batch get failed, ret:" << callRet << ", remote id:" << req->targetNid << ".");
            }
            mNetEngine->Reply(ctx, callRet, nullptr, 0);
            return MMS_OK;
        }

        static thread_local ServerDirectBufferCache batchGetResponseCache;
        uintptr_t localBuff = 0;
        auto ret = batchGetResponseCache.Reserve(mMemAllocator, req->responseCapacity, localBuff);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Alloc batch get response failed, ret:" << ret << ", len:" << req->responseCapacity << ".");
            mNetEngine->Reply(ctx, ret, nullptr, 0);
            return MMS_OK;
        }

        uint32_t responseLength = 0;
        ret = fillResponse(localBuff, responseLength);
        MmsMemoryKey localKey{};
        if (LIKELY(ret == MMS_OK)) {
            ret = mNetEngine->GetRegisteredMemoryKey(localBuff, responseLength, localKey);
        }
        if (LIKELY(ret == MMS_OK)) {
            NetRequest netReq = mNetEngine->InitNetRequest(localBuff, req->valueAddr, localKey, req->valueKey,
                                                           responseLength);
            MMS_TRACE_START(SERVER_TRACE_GET_BATCH_WRITE);
            ret = mNetEngine->SyncWrite(ctx.Channel(), netReq);
            MMS_TRACE_END(SERVER_TRACE_GET_BATCH_WRITE, ret);
        }
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Write batch get response failed, ret:" << ret << ", len:" << responseLength << ".");
        }
        mNetEngine->Reply(ctx, ret, nullptr, 0);
        return MMS_OK;
    };

    auto ret = handle();
    MMS_TRACE_END(SERVER_TRACE_GET_BATCH, ret);
    return ret;
}

BResult MmsKvServer::HandleGet(ServiceContext &ctx)
{
    MMS_TRACE_START(SERVER_TRACE_GET_SINGLE);
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetValueRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, MMS_INVALID_PARAM);
        return MMS_OK;
    }

    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        mNetEngine->Reply(ctx, MMS_NOT_READY, nullptr, 0);
        MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, MMS_NOT_READY);
        return MMS_OK;
    }

    auto *req = static_cast<GetValueRequest *>(ctx.MessageData());
    if (UNLIKELY(req->length == 0 || req->key[0] == '\0')) {
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, MMS_INVALID_PARAM);
        return MMS_OK;
    }

    if (!mKeyRouteEnabled) {
        auto ret = HandleGetLocal(ctx, *req);
        MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, ret);
        return ret;
    }

    bool forwarded = (req->flags & MMS_GET_FLAG_ROUTE_FORWARDED) != 0;
    if (forwarded) {
        CmPtInfo ptInfo{};
        auto ret = mCm->GetPtInfo(req->head.ptId, ptInfo);
        if (UNLIKELY(ret != MMS_OK)) {
            mNetEngine->Reply(ctx, MMS_CHECK_PT_FAIL, nullptr, 0);
            MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, MMS_CHECK_PT_FAIL);
            return MMS_OK;
        }
        if (UNLIKELY(req->head.ptv != ptInfo.version)) {
            LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:"
                                                                 << ptInfo.version
                                                                 << ", key:" << req->key << ".");
            mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
            MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, MMS_NEED_UPDATE_PT_VERSION);
            return MMS_OK;
        }
        bool localCopy = false;
        for (const auto &copy : ptInfo.copys) {
            if (copy.nodeId == mCm->GetLocalNid() && copy.state == CM_COPY_RUNNING) {
                localCopy = true;
                break;
            }
        }
        if (UNLIKELY(!localCopy)) {
            mNetEngine->Reply(ctx, MMS_CHECK_PT_FAIL, nullptr, 0);
            MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, MMS_CHECK_PT_FAIL);
            return MMS_OK;
        }
        ret = HandleGetLocal(ctx, *req);
        MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, ret);
        return ret;
    }

    uint64_t localPtV = mCm->GetPtVersion();
    if (UNLIKELY(req->head.ptv != localPtV)) {
        LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:" << localPtV
                                                             << ", key:" << req->key << ".");
        mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
        MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, MMS_NEED_UPDATE_PT_VERSION);
        return MMS_OK;
    }

    uint16_t ptId = 0;
    uint64_t ptv = 0;
    uint16_t remoteId[MAX_NODES_NUM] = {0};
    uint16_t remoteNum = 0;
    MMS_TRACE_START(SERVER_TRACE_GET_ROUTE);
    uint16_t keyLen = static_cast<uint16_t>(strnlen(req->key, MAX_KEY_SIZE));
    auto ret = mCm->GetPtInfoByKey(req->key, keyLen, ptId, ptv, remoteId, remoteNum);
    MMS_TRACE_END(SERVER_TRACE_GET_ROUTE, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt info by key failed, ret:" << ret << ", key:" << req->key << ".");
        mNetEngine->Reply(ctx, ret, nullptr, 0);
        MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, ret);
        return MMS_OK;
    }

    bool localCopy = remoteNum < mReplicaNum;
    if (localCopy) {
        ret = HandleGetLocal(ctx, *req);
        MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, ret);
        return ret;
    }

    ClientIoCtxPtr clientIoCtx;
    if ((req->flags & MMS_GET_FLAG_PROXY_BUFFER) != 0) {
        ret = PrepareGetProxyForward(ctx, *req, clientIoCtx);
        if (UNLIKELY(ret != MMS_OK)) {
            mNetEngine->Reply(ctx, ret, nullptr, 0);
            MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, ret);
            return MMS_OK;
        }
    }
    req->head = {mCm->GetLocalNid(), MMS_OP_C_GET, g_groupIndex, ptId, ptv};
    req->flags |= MMS_GET_FLAG_ROUTE_FORWARDED;
    ret = ForwardGet(ctx, *req, remoteId[NO_0]);
    MMS_TRACE_END(SERVER_TRACE_GET_SINGLE, ret);
    return ret;
}

void MmsKvServer::AppendNotifyItem(std::vector<NotifyShmPublishItem> &items, uint32_t &itemNum,
    const char *key, uint16_t keyLen, OperateType opType)
{
    items[itemNum++] = {key, keyLen, static_cast<uint16_t>(opType)};
}

void MmsKvServer::NotifyDataChangeBatch(const NotifyShmPublishItem *items, uint32_t itemNum)
{
    if (itemNum == 0) {
        return;
    }
    if (mSeparateMode) {
        if (UNLIKELY(!mNotifyShmPublisher.PublishBatch(items, itemNum))) {
            LOG_ERROR("Publish notify shm item batch failed, itemNum:" << itemNum << ".");
        }
        return;
    }
    for (uint32_t index = 0; index < itemNum; ++index) {
        auto opType = static_cast<OperateType>(items[index].opType);
        mNotifyDispatcher->Notify(items[index].key, items[index].keyLen, opType);
    }
}

BResult MmsKvServer::HandlePutRoutedBatch(void *ioBuff, uint32_t ioLen)
{
    auto *req = static_cast<IoDataRequest *>(ioBuff);
    if (!mKeyRouteEnabled || req->num <= NO_1) {
        return HandlePutDefImpl(ioBuff, ioLen);
    }

    MMS_TRACE_START(SERVER_TRACE_PUT_BATCH);
    static thread_local std::vector<DecodePutItem> decodedItems;
    decodedItems.resize(req->num);
    uint32_t itemNum = 0;
    auto ret = DeCodePutRequest(decodedItems, itemNum, reinterpret_cast<uint64_t>(ioBuff), ioLen);
    if (UNLIKELY(ret != MMS_OK || itemNum != req->num)) {
        ret = ret == MMS_OK ? MMS_INVALID_PARAM : ret;
        MMS_TRACE_END(SERVER_TRACE_PUT_BATCH, ret);
        return ret;
    }

    auto &groups = g_routedBatchGroups;
    MMS_TRACE_START(SERVER_TRACE_PUT_BATCH_ROUTE);
    ret = BuildRoutedPtGroups(mCm, decodedItems, itemNum, groups);
    MMS_TRACE_END(SERVER_TRACE_PUT_BATCH_ROUTE, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        MMS_TRACE_END(SERVER_TRACE_PUT_BATCH, ret);
        return ret;
    }

    auto runSerial = [this, &groups]() -> BResult {
        BResult result = MMS_OK;
        static thread_local std::vector<PutItems> items;
        static thread_local std::vector<char *> valueAddrs;
        for (auto ptId : groups.ptIds) {
            const auto &itemIndexes = groups.itemIndexes[ptId];
            items.resize(itemIndexes.size());
            valueAddrs.assign(itemIndexes.size(), nullptr);
            for (size_t index = 0; index < itemIndexes.size(); ++index) {
                auto &decoded = decodedItems[itemIndexes[index]];
                items[index] = {decoded.key, decoded.value, decoded.valueLen, decoded.keyLen, decoded.isNotify,
                                &valueAddrs[index], decoded.result};
            }

            auto taskRet = Put(items.data(), static_cast<uint32_t>(items.size()));
            for (size_t index = 0; index < itemIndexes.size(); ++index) {
                auto &decoded = decodedItems[itemIndexes[index]];
                *decoded.valueAddr = reinterpret_cast<uint64_t>(valueAddrs[index]);
            }
            if (UNLIKELY(taskRet != MMS_OK)) {
                result = taskRet;
            }
        }
        return result;
    };

    if (mMulticast || mReplicaNum != NO_1) {
        ret = runSerial();
        MMS_TRACE_END(SERVER_TRACE_PUT_BATCH, ret);
        return ret;
    }

    MMS_TRACE_START(SERVER_TRACE_PUT_BATCH_ENCODE);
    static thread_local std::vector<RoutedPutTask> tasks;
    tasks.resize(groups.ptIds.size());
    for (auto &task : tasks) {
        task.ctxItems.clear();
    }
    BResult encodeRet = MMS_OK;
    for (size_t taskIndex = 0; taskIndex < groups.ptIds.size(); ++taskIndex) {
        auto ptId = groups.ptIds[taskIndex];
        const auto &itemIndexes = groups.itemIndexes[ptId];
        auto &task = tasks[taskIndex];
        task.ptId = ptId;
        task.itemIndexes.assign(itemIndexes.begin(), itemIndexes.end());
        task.items.resize(itemIndexes.size());
        task.valueAddrs.assign(itemIndexes.size(), nullptr);
        uint64_t encodeLen = sizeof(IoDataRequest);
        for (size_t index = 0; index < itemIndexes.size(); ++index) {
            auto &decoded = decodedItems[itemIndexes[index]];
            task.items[index] = {decoded.key, decoded.value, decoded.valueLen, decoded.keyLen, decoded.isNotify,
                                 &task.valueAddrs[index], decoded.result};
            encodeLen += sizeof(IoLocDesc) + decoded.keyLen + NO_1 + decoded.valueLen;
        }
        if (UNLIKELY(encodeLen > MMS_ONESIDE_STAGING_SIZE || encodeLen > UINT32_MAX)) {
            encodeRet = MMS_INVALID_PARAM;
            break;
        }
        encodeRet = EncodePutRequest(task.items.data(), static_cast<uint32_t>(task.items.size()), task.ctxItems,
                                     allocFunc, static_cast<uint32_t>(encodeLen));
        if (UNLIKELY(encodeRet != MMS_OK || task.ctxItems.size() != NO_1)) {
            encodeRet = encodeRet == MMS_OK ? MMS_INNER_RETRY : encodeRet;
            break;
        }
    }
    MMS_TRACE_END(SERVER_TRACE_PUT_BATCH_ENCODE, encodeRet);
    if (UNLIKELY(encodeRet != MMS_OK)) {
        for (auto &task : tasks) {
            FreeBlocks(task.ctxItems);
        }
        ret = runSerial();
        MMS_TRACE_END(SERVER_TRACE_PUT_BATCH, ret);
        return ret;
    }

    IoHandle serialHandle = [this](void *buffer, uint32_t length) {
        return HandlePutDefImpl(buffer, length);
    };
    auto finishTask = [this](RoutedPutTask &task, BResult taskRet) {
        if (LIKELY(taskRet == MMS_OK)) {
            FillPutItemResults(task.items.data(), 0, task.ctxItems);
        } else {
            FillPutItemResultsAfterSend(task.items.data(), task.ctxItems, {0, taskRet, true});
        }
        for (size_t index = 0; index < task.itemIndexes.size(); ++index) {
            auto &decoded = decodedItems[task.itemIndexes[index]];
            *decoded.valueAddr = reinterpret_cast<uint64_t>(task.valueAddrs[index]);
        }
        FreeBlocks(task.ctxItems);
    };
    auto retryable = [](BResult taskRet) {
        return taskRet == MMS_ALLOC_FAIL || taskRet == MMS_INNER_RETRY ||
               taskRet == MMS_NET_RETRY || taskRet == MMS_CHECK_PT_FAIL;
    };

    BResult result = MMS_OK;
    for (size_t waveBegin = 0; waveBegin < tasks.size(); waveBegin += ROUTED_PUT_MAX_INFLIGHT) {
        size_t waveEnd = std::min(tasks.size(), waveBegin + ROUTED_PUT_MAX_INFLIGHT);
        std::vector<std::unique_ptr<RoutedPutAsyncOp>> ops;
        ops.reserve(waveEnd - waveBegin);
        BResult prepareRet = MMS_OK;
        for (size_t taskIndex = waveBegin; taskIndex < waveEnd; ++taskIndex) {
            auto op = std::unique_ptr<RoutedPutAsyncOp>(new (std::nothrow) RoutedPutAsyncOp());
            if (UNLIKELY(op == nullptr)) {
                prepareRet = MMS_ALLOC_FAIL;
                break;
            }
            op->task = &tasks[taskIndex];
            op->ctxItem = &op->task->ctxItems[NO_0];
            auto *dataReq = reinterpret_cast<IoDataRequest *>(op->ctxItem->buff);
            uint64_t ptv = 0;
            prepareRet = SelectPtForIo(reinterpret_cast<void *>(op->ctxItem->buff),
                                       static_cast<uint32_t>(op->ctxItem->reqLen), false, op->ptId, ptv,
                                       op->remoteIds.data(), op->remoteNum);
            if (UNLIKELY(prepareRet != MMS_OK || op->ptId != op->task->ptId)) {
                prepareRet = prepareRet == MMS_OK ? MMS_NEED_UPDATE_PT_VERSION : prepareRet;
                ops.emplace_back(std::move(op));
                break;
            }
            op->groupIndex = SelectPtWorkerGroup(op->ptId, mRpcWorkerGroupNum);
            prepareRet = mSequence->ApplyForSeqNo2Mst(op->ptId, op->groupIndex, dataReq->seqNo,
                                                      dataReq->negoSeqNo);
            if (UNLIKELY(prepareRet != MMS_OK)) {
                ops.emplace_back(std::move(op));
                break;
            }
            op->sequenceApplied = true;
            op->seqNo = dataReq->seqNo;
            dataReq->head = {mCm->GetLocalNid(), MMS_OP_S_PUT, op->groupIndex, op->ptId, ptv};
            op->localCopy = op->remoteNum == 0;
            op->oneSide = ShouldUseRemoteOneSide(static_cast<uint32_t>(op->ctxItem->reqLen), op->remoteNum);
            uint16_t quota = op->remoteNum + static_cast<uint16_t>(op->localCopy);
            op->callbackCtx = std::unique_ptr<RemoteIoCbCtx>(
                new (std::nothrow) RemoteIoCbCtx(quota, reinterpret_cast<void *>(op->ctxItem->buff),
                                                 static_cast<uint32_t>(op->ctxItem->reqLen), true, !op->localCopy));
            if (UNLIKELY(op->callbackCtx == nullptr)) {
                prepareRet = MMS_ALLOC_FAIL;
                ops.emplace_back(std::move(op));
                break;
            }
            if (op->oneSide) {
                MmsMemoryKey remoteKey{};
                prepareRet = mNetEngine->GetRegisteredMemoryKey(op->ctxItem->buff, op->ctxItem->reqLen, remoteKey);
                if (UNLIKELY(prepareRet != MMS_OK)) {
                    ops.emplace_back(std::move(op));
                    break;
                }
                op->oneSideReqs.resize(op->remoteNum);
                ReqHead oneSideHead = dataReq->head;
                oneSideHead.opcode = MMS_OP_S_PUT_ONESIDE;
                for (uint16_t index = 0; index < op->remoteNum; ++index) {
                    op->oneSideReqs[index] = {oneSideHead, dataReq->seqNo, dataReq->negoSeqNo, op->ctxItem->buff,
                                              remoteKey, static_cast<uint32_t>(op->ctxItem->reqLen), dataReq->num};
                }
            }
            ops.emplace_back(std::move(op));
        }

        if (UNLIKELY(prepareRet != MMS_OK || ops.size() != waveEnd - waveBegin)) {
            for (auto &op : ops) {
                if (op->sequenceApplied) {
                    mSequence->ReleaseSeqNo2Mst(op->ptId, op->groupIndex, op->seqNo);
                }
            }
            for (size_t taskIndex = waveBegin; taskIndex < waveEnd; ++taskIndex) {
                auto &task = tasks[taskIndex];
                auto taskRet = SendSingleReq(serialHandle, task.ctxItems[NO_0]);
                finishTask(task, taskRet);
                if (UNLIKELY(taskRet != MMS_OK)) {
                    result = taskRet;
                }
            }
            continue;
        }

        MMS_TRACE_START(SERVER_TRACE_PUT_BATCH_SEND);
        for (auto &op : ops) {
            Callback callback(HandleRemoteIoCallback, static_cast<void *>(op->callbackCtx.get()));
            if (op->oneSide) {
                for (uint16_t index = 0; index < op->remoteNum; ++index) {
                    mNetEngine->AsyncCall(op->remoteIds[index], op->groupIndex, MMS_OP_S_PUT_ONESIDE,
                                          op->oneSideReqs[index], callback);
                }
            } else {
                for (uint16_t index = 0; index < op->remoteNum; ++index) {
                    mNetEngine->AsyncCallBuff(op->remoteIds[index], op->groupIndex, MMS_OP_S_PUT,
                                              reinterpret_cast<void *>(op->ctxItem->buff),
                                              static_cast<uint32_t>(op->ctxItem->reqLen), callback);
                }
            }
            if (op->localCopy) {
                auto localRet = PutLocal(reinterpret_cast<void *>(op->ctxItem->buff),
                                         static_cast<uint32_t>(op->ctxItem->reqLen));
                callback.cb(callback.cbCtx, nullptr, 0, localRet);
            }
        }
        MMS_TRACE_END(SERVER_TRACE_PUT_BATCH_SEND, MMS_OK);

        BResult waitRet = MMS_OK;
        MMS_TRACE_START(SERVER_TRACE_PUT_BATCH_WAIT);
        for (auto &op : ops) {
            while (op->callbackCtx->quota.load(std::memory_order_acquire) != 0) {
                CPU_RELAX();
            }
            op->result = static_cast<BResult>(op->callbackCtx->result.load(std::memory_order_acquire));
            mSequence->ReleaseSeqNo2Mst(op->ptId, op->groupIndex, op->seqNo);
            if (UNLIKELY(op->result != MMS_OK)) {
                waitRet = op->result;
            }
        }
        MMS_TRACE_END(SERVER_TRACE_PUT_BATCH_WAIT, waitRet);

        for (auto &op : ops) {
            auto taskRet = op->result;
            if (UNLIKELY(retryable(taskRet))) {
                taskRet = SendSingleReq(serialHandle, *op->ctxItem);
            }
            finishTask(*op->task, taskRet);
            if (UNLIKELY(taskRet != MMS_OK)) {
                result = taskRet;
            }
        }
    }
    MMS_TRACE_END(SERVER_TRACE_PUT_BATCH, result);
    return result;
}

BResult MmsKvServer::HandlePut(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(IoCtrlRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    BResult ret = MMS_OK;
    IoCtrlRequest *req = static_cast<IoCtrlRequest *>(ctx.MessageData());
    ClientIoCtxPtr clientIoCtx;
    uintptr_t ioBuff = 0;
    if (UNLIKELY(!IsIoCtxRequestValid(ctx, *req, sizeof(IoDataRequest), clientIoCtx, ioBuff))) {
        LOG_ERROR("Invalid io context, offset:" << req->ioNumaOffset << ", length:" << req->ioLength
                                               << ", total size:" << mIoCtxMemSize << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint64_t localPtV = mCm->GetPtVersion();
    if (UNLIKELY(req->head.ptv != localPtV)) {
        LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:" << localPtV << ".");
        mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
        return MMS_OK;
    }

    IoHandle handle = [this](void *buffer, uint32_t length) {
        if (mMulticast) {
            MMS_TRACE_START(SERVER_TRACE_MULTICAST_PUT);
            auto handleRet = HandlePutMultiImpl(buffer, length);
            MMS_TRACE_END(SERVER_TRACE_MULTICAST_PUT, handleRet);
            return handleRet;
        }
        return HandlePutRoutedBatch(buffer, length);
    };
    ret = HandleClientIoBuffer(reinterpret_cast<void *>(ioBuff), static_cast<uint32_t>(req->ioLength), handle);
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandlePutDefImpl(void *ioBuff, uint32_t ioLen)
{
    MMS_TRACE_START(SERVER_TRACE_PUT);
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteId[MAX_NODES_NUM];
    uint16_t remoteNum;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    MMS_TRACE_START(SERVER_TRACE_PUT_ROUTE);
    auto ret = SelectPtForIo(ioBuff, ioLen, false, ptId, ptv, remoteId, remoteNum);
    MMS_TRACE_END(SERVER_TRACE_PUT_ROUTE, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    MMS_TRACE_START(SERVER_TRACE_PUT_SEQUENCE);
    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    MMS_TRACE_END(SERVER_TRACE_PUT_SEQUENCE, ret);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        MMS_TRACE_END(SERVER_TRACE_PUT, ret);
        return ret;
    }

    req->head = { mCm->GetLocalNid(), MMS_OP_S_PUT, g_groupIndex, ptId, ptv };

    bool localCopy = !mKeyRouteEnabled || remoteNum == 0;

    MMS_TRACE_START(SERVER_TRACE_PUT_COPY);
    if (ShouldUseRemoteOneSide(ioLen, remoteNum)) {
        ret = SendRemoteOneSideAndWait(remoteId, remoteNum, MMS_OP_S_PUT_ONESIDE, ioBuff, ioLen, !localCopy);
        if (LIKELY(ret == MMS_OK) && localCopy) {
            ret = PutLocal(ioBuff, ioLen);
        }
        mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);
        MMS_TRACE_END(SERVER_TRACE_PUT_COPY, ret);
        MMS_TRACE_END(SERVER_TRACE_PUT, ret);
        return ret;
    }

    int32_t quotaNum = remoteNum + static_cast<int32_t>(localCopy);

    RemoteIoCbCtx cbCtx(quotaNum, ioBuff, ioLen, true, !localCopy);
    Callback callback(HandleRemoteIoCallback, static_cast<void *>(&cbCtx));

    PutRemote(remoteId, remoteNum, ioBuff, ioLen, callback);
    if (localCopy) {
        ret = PutLocal(ioBuff, ioLen);
        callback.cb(callback.cbCtx, nullptr, 0, ret);
    }

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    ret = cbCtx.result.load(std::memory_order_acquire);
    MMS_TRACE_END(SERVER_TRACE_PUT_COPY, ret);
    MMS_TRACE_END(SERVER_TRACE_PUT, ret);
    return ret;
}

BResult MmsKvServer::HandlePutMultiImpl(void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteNum = 0;
    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { 0, MMS_OP_S_MULTI_PUT, g_groupIndex, ptId, ptv };

    int32_t quotaNum = (remoteNum == NO_0) ? NO_1 : NO_2;

    KvCbCtx cbCtx(quotaNum, MMS_OK);
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (KvCbCtx *)ctx;
        if (UNLIKELY(result != MMS_OK)) {
            int32_t expected = MMS_OK;
            cbCtx->result.compare_exchange_strong(expected, result, std::memory_order_relaxed);
        }
        cbCtx->quota.fetch_sub(NO_1, std::memory_order_release);
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    if (remoteNum != NO_0) {
        SendRemoteMulticast(ioBuff, ioLen, callback);
    }
    ret = PutLocal(ioBuff, ioLen);
    callback.cb(callback.cbCtx, nullptr, 0, ret);

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result;
}

BResult MmsKvServer::HandlePutRemote(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        ReplyPeerRequest(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = PutLocal(ctx.MessageData(), ctx.MessageDataLen(), true);
    if (mKeyRouteEnabled && mReplicaNum == NO_1) {
        auto reply = [this, &ctx](int32_t code, void *data, uint32_t len) {
            ReplyPeerRequest(ctx, code, data, len);
        };
        ReplyIoItemResults(reply, ret, ctx.MessageData(), ctx.MessageDataLen(), true);
    } else {
        ReplyPeerRequest(ctx, ret, nullptr, 0);
    }
    return MMS_OK;
}

BResult MmsKvServer::HandlePutRemoteOneSide(ServiceContext &ctx)
{
    IoHandle handle = [this](void *ioBuff, uint32_t ioLen) {
        return PutLocal(ioBuff, ioLen, true);
    };
    return HandleRemoteOneSide(ctx, handle);
}

BResult MmsKvServer::HandlePutRemoteMulti(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) || UNLIKELY(ctx.MessageDataLen() > mIoCtxBuffLen) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mMulticastEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    MMS_TRACE_START(SERVER_TRACE_PUT_LOCAL);
    auto ret = PutLocal(ctx.MessageData(), ctx.MessageDataLen(), true);
    MMS_TRACE_END(SERVER_TRACE_PUT_LOCAL, ret);
    mMulticastEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

void MmsKvServer::SendRemoteMulticast(void *ioBuff, uint32_t ioLen, Callback &callback)
{
    mMulticastEngine->MulticastAsyncCallBuff(ioBuff, ioLen, callback);
}

void MmsKvServer::PutRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback)
{
    for (uint16_t i = 0; i < remoteNum; i++) {
        mNetEngine->AsyncCallBuff(remoteId[i], g_groupIndex, MMS_OP_S_PUT, ioBuff, ioLen, callback);
    }
}

BResult MmsKvServer::PutLocal(void *ioBuff, uint32_t ioLen, bool notifyDataChange)
{
    MMS_TRACE_START(SERVER_TRACE_PUT_LOCAL);
    if (UNLIKELY(itemListPut.size() < mMaxPutItemNum)) {
        itemListPut.resize(mMaxPutItemNum);
        notifyPutItems.resize(mMaxPutItemNum);
    }

    uint32_t itemNum;
    uint32_t index;
    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(ioBuff);

    auto ret =
        mSequence->NegoSeqNo2Slv(req->head.ptId, req->head.groupIndex, req->seqNo, ioBuff, ioLen, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Nego fail, ret: " << ret << ", ptId:" << req->head.ptId << ", groupIndex:" << req->head.groupIndex
                                     << ", seq no:" << req->seqNo << ".");
        MMS_TRACE_END(SERVER_TRACE_PUT_LOCAL, ret);
        return ret;
    }

    ret = DeCodePutRequest(itemListPut, itemNum, reinterpret_cast<uint64_t>(ioBuff), ioLen);
    if (ret != MMS_OK) {
        LOG_ERROR("Decode put request fail, ret:" << ret << ", ptId:" << req->head.ptId << ", itemNum:" << itemNum
                  << ", capacity:" << itemListPut.size() << ", maxItemNum:" << mMaxPutItemNum << ", ioLen:" << ioLen
                  << ".");
        MMS_TRACE_END(SERVER_TRACE_PUT_LOCAL, ret);
        return ret;
    }

    BResult result = MMS_OK;
    uint32_t notifyEventNum = 0;
    bool notifyEnabled = notifyDataChange && mDataChangeCallbackSwitch &&
        (!mSeparateMode || mNotifyShmPublisher.IsActive());
    for (index = 0; index < itemNum; index++) {
        char *valueAddr = nullptr;
        if (mServiceable.load(std::memory_order_acquire)) {
            PutPara para = {itemListPut[index].key, itemListPut[index].keyLen, itemListPut[index].value,
                            itemListPut[index].valueLen, static_cast<uint32_t>(itemListPut[index].version),
                            req->head.ptId, &valueAddr};
            MMS_TRACE_START(CACHE_TRACE_PUT);
            ret = mCache->Put(para);
            MMS_TRACE_END(CACHE_TRACE_PUT, ret);
        } else {
            ret = mCache->Replace({itemListPut[index].key, itemListPut[index].keyLen, itemListPut[index].value, 0,
                                   itemListPut[index].valueLen,
                                   static_cast<uint32_t>(itemListPut[index].version), req->head.ptId});
        }
        *itemListPut[index].result = (ret == MMS_PUT_REPEAT) ? MMS_OK : ret;
        *itemListPut[index].valueAddr = reinterpret_cast<uint64_t>(valueAddr);

        if (UNLIKELY(ret != MMS_OK && ret != MMS_PUT_REPEAT)) {
            LOG_ERROR("Put cache fail, ret:" << ret << ", key:" << itemListPut[index].key << ", ptId:" << req->head.ptId
                                             << ".");
            result = ret;
            continue;
        }
        if (notifyEnabled && itemListPut[index].isNotify != 0 && LIKELY(ret == MMS_OK)) {
            AppendNotifyItem(notifyPutItems, notifyEventNum,
                itemListPut[index].key, itemListPut[index].keyLen, OP_PUT);
        }
    }
    if (notifyEventNum != 0) {
        MMS_TRACE_START(SERVER_TRACE_NOTIFY_DATA_CHANGE);
        NotifyDataChangeBatch(notifyPutItems.data(), notifyEventNum);
        MMS_TRACE_END(SERVER_TRACE_NOTIFY_DATA_CHANGE, MMS_OK);
    }
    MMS_TRACE_END(SERVER_TRACE_PUT_LOCAL, result);
    return result;
}

thread_local std::vector<DecodePutItem> MmsKvServer::itemListPut = [] {
    return std::vector<DecodePutItem>(mMaxPutItemNum);
}();

thread_local std::vector<DecodeUpdateItem> MmsKvServer::itemListUpdate = [] {
    return std::vector<DecodeUpdateItem>(mMaxUpdateItemNum);
}();

thread_local std::vector<DecodeDeleteItem> MmsKvServer::itemListDelete = [] {
    return std::vector<DecodeDeleteItem>(mMaxDeleteItemNum);
}();

thread_local std::vector<NotifyShmPublishItem> MmsKvServer::notifyPutItems = [] {
    return std::vector<NotifyShmPublishItem>(mMaxPutItemNum);
}();

thread_local std::vector<NotifyShmPublishItem> MmsKvServer::notifyDeleteItems = [] {
    return std::vector<NotifyShmPublishItem>(mMaxDeleteItemNum);
}();

BResult MmsKvServer::HandleUpdateRoutedBatch(void *ioBuff, uint32_t ioLen)
{
    auto *req = static_cast<IoDataRequest *>(ioBuff);
    if (!mKeyRouteEnabled || req->num <= NO_1) {
        return HandleUpdateDefImpl(ioBuff, ioLen);
    }

    static thread_local std::vector<DecodeUpdateItem> decodedItems;
    decodedItems.resize(req->num);
    uint32_t itemNum = 0;
    auto ret = DeCodeUpdateRequest(decodedItems, itemNum, reinterpret_cast<uint64_t>(ioBuff), ioLen);
    if (UNLIKELY(ret != MMS_OK || itemNum != req->num)) {
        return ret == MMS_OK ? MMS_INVALID_PARAM : ret;
    }

    auto &groups = g_routedBatchGroups;
    ret = BuildRoutedPtGroups(mCm, decodedItems, itemNum, groups);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    BResult result = MMS_OK;
    static thread_local std::vector<UpdateItems> items;
    for (auto ptId : groups.ptIds) {
        const auto &itemIndexes = groups.itemIndexes[ptId];
        items.resize(itemIndexes.size());
        for (size_t index = 0; index < itemIndexes.size(); ++index) {
            auto &decoded = decodedItems[itemIndexes[index]];
            items[index] = {decoded.key, decoded.value, decoded.keyLen, decoded.valueLen, decoded.offset,
                            decoded.result};
        }
        ret = Update(items.data(), static_cast<uint32_t>(items.size()));
        if (UNLIKELY(ret != MMS_OK)) {
            result = ret;
        }
    }
    return result;
}

BResult MmsKvServer::HandleUpdate(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(IoCtrlRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    BResult ret = MMS_OK;
    IoCtrlRequest *req = static_cast<IoCtrlRequest *>(ctx.MessageData());
    ClientIoCtxPtr clientIoCtx;
    uintptr_t ioBuff = 0;
    if (UNLIKELY(!IsIoCtxRequestValid(ctx, *req, sizeof(IoDataRequest), clientIoCtx, ioBuff))) {
        LOG_ERROR("Invalid io context, offset:" << req->ioNumaOffset << ", length:" << req->ioLength
                                               << ", total size:" << mIoCtxMemSize << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint64_t localPtV = mCm->GetPtVersion();
    if (UNLIKELY(req->head.ptv != localPtV)) {
        LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:" << localPtV << ".");
        mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
        return MMS_OK;
    }

    IoHandle handle = [this](void *buffer, uint32_t length) {
        return mMulticast ? HandleUpdateMultiImpl(buffer, length) : HandleUpdateRoutedBatch(buffer, length);
    };
    ret = HandleClientIoBuffer(reinterpret_cast<void *>(ioBuff), static_cast<uint32_t>(req->ioLength), handle);
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandleUpdateDefImpl(void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteId[MAX_NODES_NUM];
    uint16_t remoteNum;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = SelectPtForIo(ioBuff, ioLen, false, ptId, ptv, remoteId, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { mCm->GetLocalNid(), MMS_OP_S_UPDATE, g_groupIndex, ptId, ptv };

    bool localCopy = !mKeyRouteEnabled || remoteNum == 0;

    if (ShouldUseRemoteOneSide(ioLen, remoteNum)) {
        ret = SendRemoteOneSideAndWait(remoteId, remoteNum, MMS_OP_S_UPDATE_ONESIDE, ioBuff, ioLen, !localCopy);
        if (LIKELY(ret == MMS_OK) && localCopy) {
            ret = UpdateLocal(ioBuff, ioLen);
        }
        mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);
        return ret;
    }

    int32_t quotaNum = remoteNum + static_cast<int32_t>(localCopy);

    RemoteIoCbCtx cbCtx(quotaNum, ioBuff, ioLen, true, !localCopy);
    Callback callback(HandleRemoteIoCallback, static_cast<void *>(&cbCtx));

    UpdateRemote(remoteId, remoteNum, ioBuff, ioLen, callback);
    if (localCopy) {
        ret = UpdateLocal(ioBuff, ioLen);
        callback.cb(callback.cbCtx, nullptr, 0, ret);
    }

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result.load(std::memory_order_acquire);
}

BResult MmsKvServer::HandleUpdateMultiImpl(void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteNum = 0;
    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { 0, MMS_OP_S_MULTI_UPDATE, g_groupIndex, ptId, ptv };

    int32_t quotaNum = (remoteNum == NO_0) ? NO_1 : NO_2;

    KvCbCtx cbCtx(quotaNum, MMS_OK);
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (KvCbCtx *)ctx;
        if (UNLIKELY(result != MMS_OK)) {
            int32_t expected = MMS_OK;
            cbCtx->result.compare_exchange_strong(expected, result, std::memory_order_relaxed);
        }
        cbCtx->quota.fetch_sub(NO_1, std::memory_order_release);
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    if (remoteNum != NO_0) {
        SendRemoteMulticast(ioBuff, ioLen, callback);
    }
    ret = UpdateLocal(ioBuff, ioLen);
    callback.cb(callback.cbCtx, nullptr, 0, ret);

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result;
}

BResult MmsKvServer::HandleUpdateRemote(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        ReplyPeerRequest(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = UpdateLocal(ctx.MessageData(), ctx.MessageDataLen());
    if (mKeyRouteEnabled && mReplicaNum == NO_1) {
        auto reply = [this, &ctx](int32_t code, void *data, uint32_t len) {
            ReplyPeerRequest(ctx, code, data, len);
        };
        ReplyIoItemResults(reply, ret, ctx.MessageData(), ctx.MessageDataLen(), true);
    } else {
        ReplyPeerRequest(ctx, ret, nullptr, 0);
    }
    return MMS_OK;
}

BResult MmsKvServer::HandleUpdateRemoteOneSide(ServiceContext &ctx)
{
    IoHandle handle = [this](void *ioBuff, uint32_t ioLen) {
        return UpdateLocal(ioBuff, ioLen);
    };
    return HandleRemoteOneSide(ctx, handle);
}

BResult MmsKvServer::HandleUpdateRemoteMulti(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) || UNLIKELY(ctx.MessageDataLen() > mIoCtxBuffLen) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mMulticastEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = UpdateLocal(ctx.MessageData(), ctx.MessageDataLen());
    mMulticastEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

void MmsKvServer::UpdateRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback)
{
    for (uint16_t i = 0; i < remoteNum; i++) {
        mNetEngine->AsyncCallBuff(remoteId[i], g_groupIndex, MMS_OP_S_UPDATE, ioBuff, ioLen, callback);
    }
}

BResult MmsKvServer::UpdateLocal(void *ioBuff, uint32_t ioLen)
{
    if (UNLIKELY(itemListUpdate.size() < mMaxUpdateItemNum)) {
        itemListUpdate.resize(mMaxUpdateItemNum);
    }

    uint32_t itemNum;
    uint32_t index;
    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(ioBuff);

    auto ret = mSequence->NegoSeqNo2Slv(req->head.ptId, req->head.groupIndex, req->seqNo, ioBuff, ioLen,
                                        req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Nego fail, ret: " << ret << ", ptId:" << req->head.ptId << ", groupIndex:" << req->head.groupIndex
                  << ", seq no:" << req->seqNo << ".");
        return ret;
    }

    ret = DeCodeUpdateRequest(itemListUpdate, itemNum, reinterpret_cast<uint64_t>(ioBuff), ioLen);
    if (ret != MMS_OK) {
        LOG_ERROR("Decode update request fail, ret:" << ret << ", ptId:" << req->head.ptId << ", itemNum:"
                  << itemNum << ", capacity:" << itemListUpdate.size() << ", maxItemNum:" << mMaxUpdateItemNum
                  << ", ioLen:" << ioLen << ".");
        return ret;
    }

    BResult result = MMS_OK;
    for (index = 0; index < itemNum; index++) {
        if (mServiceable.load(std::memory_order_acquire)) {
            UpdatePara para = {itemListUpdate[index].key, itemListUpdate[index].keyLen, itemListUpdate[index].value,
                               itemListUpdate[index].offset, itemListUpdate[index].valueLen,
                               static_cast<uint32_t>(itemListUpdate[index].version)};
            ret = mCache->Update(para);
        } else {
            ret = mCache->Replace({itemListUpdate[index].key, itemListUpdate[index].keyLen,
                                   itemListUpdate[index].value, itemListUpdate[index].offset,
                                   itemListUpdate[index].valueLen,
                                   static_cast<uint32_t>(itemListUpdate[index].version),
                                   req->head.ptId});
        }
        *itemListUpdate[index].result = ret;
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Update cache fail, ret:" << ret << ", key:" << itemListUpdate[index].key << ", ptId:" <<
                      req->head.ptId);
            result = ret;
        }
    }
    return result;
}

BResult MmsKvServer::HandleDeleteRoutedBatch(void *ioBuff, uint32_t ioLen)
{
    auto *req = static_cast<IoDataRequest *>(ioBuff);
    if (!mKeyRouteEnabled || req->num <= NO_1) {
        return HandleDeleteDefImpl(ioBuff, ioLen);
    }

    static thread_local std::vector<DecodeDeleteItem> decodedItems;
    decodedItems.resize(req->num);
    uint32_t itemNum = 0;
    auto ret = DeCodeDeleteRequest(decodedItems, itemNum, reinterpret_cast<uint64_t>(ioBuff), ioLen);
    if (UNLIKELY(ret != MMS_OK || itemNum != req->num)) {
        return ret == MMS_OK ? MMS_INVALID_PARAM : ret;
    }

    auto &groups = g_routedBatchGroups;
    ret = BuildRoutedPtGroups(mCm, decodedItems, itemNum, groups);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    BResult result = MMS_OK;
    static thread_local std::vector<DeleteItems> items;
    for (auto ptId : groups.ptIds) {
        const auto &itemIndexes = groups.itemIndexes[ptId];
        items.resize(itemIndexes.size());
        for (size_t index = 0; index < itemIndexes.size(); ++index) {
            auto &decoded = decodedItems[itemIndexes[index]];
            items[index] = {decoded.key, decoded.keyLen, decoded.isNotify, decoded.result};
        }
        ret = Delete(items.data(), static_cast<uint32_t>(items.size()));
        if (UNLIKELY(ret != MMS_OK)) {
            result = ret;
        }
    }
    return result;
}

BResult MmsKvServer::HandleDelete(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(IoCtrlRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    BResult ret = MMS_OK;
    IoCtrlRequest *req = static_cast<IoCtrlRequest *>(ctx.MessageData());
    ClientIoCtxPtr clientIoCtx;
    uintptr_t ioBuff = 0;
    if (UNLIKELY(!IsIoCtxRequestValid(ctx, *req, sizeof(IoDataRequest), clientIoCtx, ioBuff))) {
        LOG_ERROR("Invalid io context, offset:" << req->ioNumaOffset << ", length:" << req->ioLength
                                               << ", total size:" << mIoCtxMemSize << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint64_t localPtV = mCm->GetPtVersion();
    if (UNLIKELY(req->head.ptv != localPtV)) {
        LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:" << localPtV << ".");
        mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
        return MMS_OK;
    }

    IoHandle handle = [this](void *buffer, uint32_t length) {
        if (mMulticast) {
            MMS_TRACE_START(SERVER_TRACE_MULTICAST_DELETE);
            auto handleRet = HandleDeleteMultiImpl(buffer, length);
            MMS_TRACE_END(SERVER_TRACE_MULTICAST_DELETE, handleRet);
            return handleRet;
        }
        return HandleDeleteRoutedBatch(buffer, length);
    };
    ret = HandleClientIoBuffer(reinterpret_cast<void *>(ioBuff), static_cast<uint32_t>(req->ioLength), handle);
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandleDeleteDefImpl(void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteId[MAX_NODES_NUM];
    uint16_t remoteNum;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = SelectPtForIo(ioBuff, ioLen, false, ptId, ptv, remoteId, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { mCm->GetLocalNid(), MMS_OP_S_DELETE, g_groupIndex, ptId, ptv };

    bool localCopy = !mKeyRouteEnabled || remoteNum == 0;
    int32_t quotaNum = remoteNum + static_cast<int32_t>(localCopy);

    RemoteIoCbCtx cbCtx(quotaNum, ioBuff, ioLen, false, !localCopy);
    Callback callback(HandleRemoteIoCallback, static_cast<void *>(&cbCtx));

    DeleteRemote(remoteId, remoteNum, ioBuff, ioLen, callback);
    if (localCopy) {
        ret = DeleteLocal(ioBuff, ioLen);
        callback.cb(callback.cbCtx, nullptr, 0, ret);
    }

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result.load(std::memory_order_acquire);
}

BResult MmsKvServer::HandleDeleteMultiImpl(void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteNum = 0;
    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { 0, MMS_OP_S_MULTI_DELETE, g_groupIndex, ptId, ptv };

    int32_t quotaNum = (remoteNum == NO_0) ? NO_1 : NO_2;

    KvCbCtx cbCtx(quotaNum, MMS_OK);
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (KvCbCtx *)ctx;
        if (UNLIKELY(result != MMS_OK)) {
            int32_t expected = MMS_OK;
            cbCtx->result.compare_exchange_strong(expected, result, std::memory_order_relaxed);
        }
        cbCtx->quota.fetch_sub(NO_1, std::memory_order_release);
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    if (remoteNum != NO_0) {
        SendRemoteMulticast(ioBuff, ioLen, callback);
    }
    ret = DeleteLocal(ioBuff, ioLen);
    callback.cb(callback.cbCtx, nullptr, 0, ret);

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result;
}

BResult MmsKvServer::HandleDeleteRemote(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        ReplyPeerRequest(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = DeleteLocal(ctx.MessageData(), ctx.MessageDataLen(), true);
    if (mKeyRouteEnabled && mReplicaNum == NO_1) {
        auto reply = [this, &ctx](int32_t code, void *data, uint32_t len) {
            ReplyPeerRequest(ctx, code, data, len);
        };
        ReplyIoItemResults(reply, ret, ctx.MessageData(), ctx.MessageDataLen(), false);
    } else {
        ReplyPeerRequest(ctx, ret, nullptr, 0);
    }
    return MMS_OK;
}

BResult MmsKvServer::HandleDeleteRemoteMulti(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) || UNLIKELY(ctx.MessageDataLen() > mIoCtxBuffLen) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mMulticastEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = DeleteLocal(ctx.MessageData(), ctx.MessageDataLen(), true);
    mMulticastEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandleReplaceRoutedBatch(void *ioBuff, uint32_t ioLen)
{
    auto *req = static_cast<IoDataRequest *>(ioBuff);
    if (!mKeyRouteEnabled || req->num <= NO_1) {
        return HandleReplaceDefImpl(ioBuff, ioLen);
    }

    static thread_local std::vector<DecodeUpdateItem> decodedItems;
    decodedItems.resize(req->num);
    uint32_t itemNum = 0;
    auto ret = DeCodeReplaceRequest(decodedItems, itemNum, reinterpret_cast<uint64_t>(ioBuff), ioLen);
    if (UNLIKELY(ret != MMS_OK || itemNum != req->num)) {
        return ret == MMS_OK ? MMS_INVALID_PARAM : ret;
    }

    auto &groups = g_routedBatchGroups;
    ret = BuildRoutedPtGroups(mCm, decodedItems, itemNum, groups);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    BResult result = MMS_OK;
    static thread_local std::vector<ReplaceItems> items;
    for (auto ptId : groups.ptIds) {
        const auto &itemIndexes = groups.itemIndexes[ptId];
        items.resize(itemIndexes.size());
        for (size_t index = 0; index < itemIndexes.size(); ++index) {
            auto &decoded = decodedItems[itemIndexes[index]];
            items[index] = {decoded.key, decoded.value, decoded.keyLen, decoded.valueLen, decoded.offset,
                            decoded.result};
        }
        ret = Replace(items.data(), static_cast<uint32_t>(items.size()));
        if (UNLIKELY(ret != MMS_OK)) {
            result = ret;
        }
    }
    return result;
}

BResult MmsKvServer::HandleReplace(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(IoCtrlRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    BResult ret = MMS_OK;
    IoCtrlRequest *req = static_cast<IoCtrlRequest *>(ctx.MessageData());
    ClientIoCtxPtr clientIoCtx;
    uintptr_t ioBuff = 0;
    if (UNLIKELY(!IsIoCtxRequestValid(ctx, *req, sizeof(IoDataRequest), clientIoCtx, ioBuff))) {
        LOG_ERROR("Invalid io context, offset:" << req->ioNumaOffset << ", length:" << req->ioLength
                                               << ", total size:" << mIoCtxMemSize << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint64_t localPtV = mCm->GetPtVersion();
    if (UNLIKELY(req->head.ptv != localPtV)) {
        LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:" << localPtV << ".");
        mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
        return MMS_OK;
    }

    IoHandle handle = [this](void *buffer, uint32_t length) {
        return mMulticast ? HandleReplaceMultiImpl(buffer, length) : HandleReplaceRoutedBatch(buffer, length);
    };
    ret = HandleClientIoBuffer(reinterpret_cast<void *>(ioBuff), static_cast<uint32_t>(req->ioLength), handle);
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandleReplaceDefImpl(void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteId[MAX_NODES_NUM];
    uint16_t remoteNum;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = SelectPtForIo(ioBuff, ioLen, false, ptId, ptv, remoteId, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { mCm->GetLocalNid(), MMS_OP_S_REPLACE, g_groupIndex, ptId, ptv };

    bool localCopy = !mKeyRouteEnabled || remoteNum == 0;

    if (ShouldUseRemoteOneSide(ioLen, remoteNum)) {
        ret = SendRemoteOneSideAndWait(remoteId, remoteNum, MMS_OP_S_REPLACE_ONESIDE, ioBuff, ioLen, !localCopy);
        if (LIKELY(ret == MMS_OK) && localCopy) {
            ret = ReplaceLocal(ioBuff, ioLen);
        }
        mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);
        return ret;
    }

    int32_t quotaNum = remoteNum + static_cast<int32_t>(localCopy);

    RemoteIoCbCtx cbCtx(quotaNum, ioBuff, ioLen, true, !localCopy);
    Callback callback(HandleRemoteIoCallback, static_cast<void *>(&cbCtx));

    ReplaceRemote(remoteId, remoteNum, ioBuff, ioLen, callback);
    if (localCopy) {
        ret = ReplaceLocal(ioBuff, ioLen);
        callback.cb(callback.cbCtx, nullptr, 0, ret);
    }

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result.load(std::memory_order_acquire);
}

BResult MmsKvServer::HandleReplaceMultiImpl(void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteNum = 0;
    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { 0, MMS_OP_S_MULTI_REPLACE, g_groupIndex, ptId, ptv };

    int32_t quotaNum = (remoteNum == NO_0) ? NO_1 : NO_2;

    KvCbCtx cbCtx(quotaNum, MMS_OK);
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *cbCtx = (KvCbCtx *)ctx;
        if (UNLIKELY(result != MMS_OK)) {
            int32_t expected = MMS_OK;
            cbCtx->result.compare_exchange_strong(expected, result, std::memory_order_relaxed);
        }
        cbCtx->quota.fetch_sub(NO_1, std::memory_order_release);
    };
    Callback callback(cbFunc, static_cast<void *>(&cbCtx));

    if (remoteNum != NO_0) {
        SendRemoteMulticast(ioBuff, ioLen, callback);
    }
    ret = ReplaceLocal(ioBuff, ioLen);
    callback.cb(callback.cbCtx, nullptr, 0, ret);

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result;
}

BResult MmsKvServer::HandleReplaceRemote(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        ReplyPeerRequest(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = ReplaceLocal(ctx.MessageData(), ctx.MessageDataLen());
    if (mKeyRouteEnabled && mReplicaNum == NO_1) {
        auto reply = [this, &ctx](int32_t code, void *data, uint32_t len) {
            ReplyPeerRequest(ctx, code, data, len);
        };
        ReplyIoItemResults(reply, ret, ctx.MessageData(), ctx.MessageDataLen(), true);
    } else {
        ReplyPeerRequest(ctx, ret, nullptr, 0);
    }
    return MMS_OK;
}

BResult MmsKvServer::HandleReplaceRemoteOneSide(ServiceContext &ctx)
{
    IoHandle handle = [this](void *ioBuff, uint32_t ioLen) {
        return ReplaceLocal(ioBuff, ioLen);
    };
    return HandleRemoteOneSide(ctx, handle);
}

BResult MmsKvServer::HandleReplaceRemoteMulti(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) || UNLIKELY(ctx.MessageDataLen() > mIoCtxBuffLen) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mMulticastEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = ReplaceLocal(ctx.MessageData(), ctx.MessageDataLen());
    mMulticastEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

void MmsKvServer::ReplaceRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen,
                                Callback &callback)
{
    for (uint16_t i = 0; i < remoteNum; i++) {
        mNetEngine->AsyncCallBuff(remoteId[i], g_groupIndex, MMS_OP_S_REPLACE, ioBuff, ioLen, callback);
    }
}

BResult MmsKvServer::ReplaceLocal(void *ioBuff, uint32_t ioLen)
{
    if (UNLIKELY(itemListUpdate.size() < mMaxUpdateItemNum)) {
        itemListUpdate.resize(mMaxUpdateItemNum);
    }

    uint32_t itemNum;
    uint32_t index;
    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(ioBuff);

    auto ret =
        mSequence->NegoSeqNo2Slv(req->head.ptId, req->head.groupIndex, req->seqNo, ioBuff, ioLen, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Nego fail, ret: " << ret << ", ptId:" << req->head.ptId << ", groupIndex:" << req->head.groupIndex
                                     << ", seq no:" << req->seqNo << ".");
        return ret;
    }

    ret = DeCodeReplaceRequest(itemListUpdate, itemNum, reinterpret_cast<uint64_t>(ioBuff), ioLen);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Decode replace request fail, ret:" << ret << ", ptId:" << req->head.ptId << ", itemNum:"
                  << itemNum << ", capacity:" << itemListUpdate.size() << ", maxItemNum:" << mMaxUpdateItemNum
                  << ", ioLen:" << ioLen << ".");
        return ret;
    }

    BResult result = MMS_OK;
    for (index = 0; index < itemNum; index++) {
        ret = mCache->Replace({itemListUpdate[index].key, itemListUpdate[index].keyLen, itemListUpdate[index].value,
                               itemListUpdate[index].offset,
                               itemListUpdate[index].valueLen, static_cast<uint32_t>(itemListUpdate[index].version),
                               req->head.ptId});
        *itemListUpdate[index].result = ret;
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Replace fail, ret:" << ret << ", key:" << itemListUpdate[index].key);
            result = ret;
        }

    }
    return result;
}

void MmsKvServer::DeleteRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback)
{
    for (uint16_t i = 0; i < remoteNum; i++) {
        mNetEngine->AsyncCallBuff(remoteId[i], g_groupIndex, MMS_OP_S_DELETE, ioBuff, ioLen, callback);
    }
}

BResult MmsKvServer::DeleteLocal(void *ioBuff, uint32_t ioLen, bool notifyDataChange)
{
    if (UNLIKELY(itemListDelete.size() < mMaxDeleteItemNum)) {
        itemListDelete.resize(mMaxDeleteItemNum);
        notifyDeleteItems.resize(mMaxDeleteItemNum);
    }

    uint32_t itemNum;
    uint32_t index;
    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(ioBuff);

    auto ret = mSequence->NegoSeqNo2Slv(req->head.ptId, req->head.groupIndex, req->seqNo, ioBuff, ioLen,
                                        req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Nego fail, ret: " << ret << ", ptId:" << req->head.ptId << ", groupIndex:" << req->head.groupIndex
                  << ", seq no:" << req->seqNo << ".");
        return ret;
    }

    ret = DeCodeDeleteRequest(itemListDelete, itemNum, reinterpret_cast<uint64_t>(ioBuff), ioLen);
    if (ret != MMS_OK) {
        LOG_ERROR("Decode delete request fail, ret:" << ret << ", ptId:" << req->head.ptId);
        return ret;
    }

    BResult result = MMS_OK;
    uint32_t notifyEventNum = 0;
    bool notifyEnabled = notifyDataChange && mDataChangeCallbackSwitch &&
        (!mSeparateMode || mNotifyShmPublisher.IsActive());
    for (index = 0; index < itemNum; index++) {
        MMS_TRACE_START(CACHE_TRACE_DELETE);
        ret = mCache->Delete(itemListDelete[index].key, itemListDelete[index].keyLen, itemListDelete[index].version);
        MMS_TRACE_END(CACHE_TRACE_DELETE, ret);
        *itemListDelete[index].result = ret;
        if (ret != MMS_OK && ret != MMS_KEY_NOT_EXISTS) {
            LOG_ERROR("Delete cache fail, ret:" << ret << ", key:" << itemListDelete[index].key << ", ptId:" <<
                      req->head.ptId);
            result = ret;
            continue;
        }
        if (notifyEnabled && itemListDelete[index].isNotify != 0 && LIKELY(ret == MMS_OK)) {
            AppendNotifyItem(notifyDeleteItems, notifyEventNum,
                itemListDelete[index].key, itemListDelete[index].keyLen, OP_DELETE);
        }
    }
    if (notifyEventNum != 0) {
        MMS_TRACE_START(SERVER_TRACE_NOTIFY_DATA_CHANGE);
        NotifyDataChangeBatch(notifyDeleteItems.data(), notifyEventNum);
        MMS_TRACE_END(SERVER_TRACE_NOTIFY_DATA_CHANGE, MMS_OK);
    }
    return result;
}

void MmsKvServer::NotifyServiceable(bool serviceable)
{
    mCm = MmsServer::Instance()->GetCm();
    mServiceable.store(serviceable, std::memory_order_release);
    LOG_INFO("Notify serviceable: " << serviceable << ".");
}

void MmsKvServer::NotifyPtMigrate(uint16_t ptId)
{
    do {
        LOG_INFO("Notify pt migrate begin, ptId:" << ptId << ".");
        auto ret = NotifyPtMigrateImpl(ptId);
        if (ret != MMS_OK) {
            LOG_ERROR("Notify pt migrate fail:" << ret << ", ptId:" << ptId << ".");
        }
        bool isRetry = (ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY ||
            ret == MMS_NET_RETRY || ret == MMS_CHECK_PT_FAIL);
        if (!isRetry) {
            LOG_INFO("Notify pt migrate end, ptId:" << ptId << ", result:" << ret << ".");
            break;
        }
        sleep(NO_2);
    } while (true);
    return;
}

BResult MmsKvServer::NotifyPtMigrateImpl(uint16_t ptId)
{
    CmPtInfo ptInfo;

    auto ret = mCm->GetPtInfo(ptId, ptInfo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ".");
        return ret;
    }
    uint16_t nodeList[MAX_NODES_NUM];
    uint16_t nodeNum = 0;
    for (const auto& elem : ptInfo.copys) {
        if (elem.state != CM_COPY_RUNNING) {
            continue;
        }
        nodeList[nodeNum] = elem.nodeId;
        nodeNum++;
    }

    uint16_t groupNum = MmsServer::Instance()->GetConfig()->GetNetConfig().rpcWorkerGroupsNum;

    for (uint16_t groupIndex = 0; groupIndex < groupNum; groupIndex++) {
        LOG_DEBUG("Nego ptId:" << ptId << ", groupIndex:" << groupIndex << ".");
        uint64_t seqList[MAX_NODES_NUM][SEQ_QUEUE_LEN] = { {0} };
        uint16_t locList[MAX_NODES_NUM][SEQ_QUEUE_LEN] = { {0} };
        uint32_t seqNum[MAX_NODES_NUM] = { 0 };
        for (uint16_t i = 0; i < nodeNum; i++) {
            ret = GetSeqNoList(seqList[i], seqNum[i], ptId, ptInfo.version, groupIndex, nodeList[i]);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Get seq list failed, ret: " << ret << ", ptId:" << ptId << ".");
                return ret;
            }
        }
        uint64_t negoSeqList[SEQ_QUEUE_LEN] = { 0 };
        uint16_t negoLocList[SEQ_QUEUE_LEN] = { 0 };
        uint32_t negoNum = 0;
        for (uint16_t i = 0; i < nodeNum; i++) {
            ret = MergeSeqNoList(negoSeqList, negoLocList, negoNum, seqList[i], seqNum[i], nodeList[i]);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Merge seq list failed, ret: " << ret << ", ptId:" << ptId << ".");
                return ret;
            }
        }
        for (uint16_t j = 0; j < negoNum; j++) {
            for (uint16_t i = 0; i < nodeNum; i++) {
                ret = SyncSeqNoData(negoSeqList[j], negoLocList[j], seqList[i], seqNum[i], nodeList[i],
                                    ptInfo, groupIndex);
                if (UNLIKELY(ret != MMS_OK)) {
                    LOG_ERROR("Sync seq data failed, ret: " << ret << ", ptId:" << ptId << ".");
                    return ret;
                }
            }
        }
        if (negoNum != 0) {
            LOG_DEBUG("Reset ptId:" << ptId << ", groupIndex:" << groupIndex << ", seq no:" <<
                      negoSeqList[negoNum - NO_1]);
            mSequence->ResetSeqNoState2Mst(ptId, groupIndex, negoSeqList[negoNum - NO_1]);
        }
    }

    mCm->UpdatePtState(ptId);

    return MMS_OK;
}

BResult MmsKvServer::GetSeqNoList(uint64_t seqList[], uint32_t &seqNum, uint16_t ptId, uint64_t ptv,
                                  uint16_t groupIndex, uint16_t nid)
{
    uint16_t localId = mCm->GetLocalNid();
    if (nid == localId) {
        return mSequence->GetSeqNoList2Slv(ptId, groupIndex, seqList, seqNum);
    }

    GetSeqListRequest req;
    GetSeqListResponse rsp;

    req.head = { 0, MMS_OP_S_GET_SEQNO_LIST, groupIndex, ptId, ptv };

    BResult ret = MMS_OK;
    if (mMulticast) {
        ret = mMulticastEngine->MulticastSyncCall<GetSeqListRequest, GetSeqListResponse>(
            nid, MMS_OP_S_GET_SEQNO_LIST, req, rsp);
    } else {
        ret = mNetEngine->SyncCall<GetSeqListRequest, GetSeqListResponse>(nid, groupIndex, MMS_OP_S_GET_SEQNO_LIST,
                                                                          req, rsp);
    }
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get seq list failed, ret: " << ret << ", remote:" << nid << ", ptId:" << ptId << ".");
        return ret;
    }

    if (UNLIKELY(rsp.seqNum > SEQ_QUEUE_LEN)) {
        LOG_ERROR("Invalid seqNum:" << rsp.seqNum << ", must be less than " << SEQ_QUEUE_LEN << ".");
        return MMS_INVALID_PARAM;
    }

    for (uint32_t i = 0; i < rsp.seqNum; i++) {
        seqList[i] = rsp.seqList[i];
        LOG_DEBUG("Get remote:" << nid << ", seq no:" << seqList[i]);
    }
    seqNum = rsp.seqNum;
    return MMS_OK;
}

BResult MmsKvServer::MergeSeqNoList(uint64_t negoSeqList[], uint16_t negoLocList[], uint32_t &negoSeqNum,
                                    uint64_t seqList[], uint32_t seqNum, uint16_t remoteId)
{
    uint64_t mergeSeqList[SEQ_QUEUE_LEN_T] = { 0 };
    uint16_t mergeLocList[SEQ_QUEUE_LEN_T] = { 0 };
    uint32_t mergeNum = 0;
    uint32_t i = 0;
    uint32_t j = 0;

    while (i < negoSeqNum && j < seqNum) {
        if (negoSeqList[i] < seqList[j]) {
            if (mergeNum == 0 || mergeSeqList[mergeNum - NO_1] != negoSeqList[i]) {
                mergeSeqList[mergeNum] = negoSeqList[i];
                mergeLocList[mergeNum] = negoLocList[i];
                mergeNum++;
            }
            i++;
        } else if (negoSeqList[i] > seqList[j]) {
            if (mergeNum == 0 || mergeSeqList[mergeNum - NO_1] != seqList[j]) {
                mergeSeqList[mergeNum] = seqList[j];
                mergeLocList[mergeNum] = remoteId;
                mergeNum++;
            }
            j++;
        } else {
            if (mergeNum == 0 || mergeSeqList[mergeNum - NO_1] != negoSeqList[i]) {
                mergeSeqList[mergeNum] = negoSeqList[i];
                mergeLocList[mergeNum] = negoLocList[i];
                mergeNum++;
            }
            i++;
            j++;
        }
    }
    while (i < negoSeqNum) {
        if (mergeNum == 0 || mergeSeqList[mergeNum - NO_1] != negoSeqList[i]) {
            mergeSeqList[mergeNum] = negoSeqList[i];
            mergeLocList[mergeNum] = negoLocList[i];
            mergeNum++;
        }
        i++;
    }
    while (j < seqNum) {
        if (mergeNum == 0 || mergeSeqList[mergeNum - NO_1] != seqList[j]) {
            mergeSeqList[mergeNum] = seqList[j];
            mergeLocList[mergeNum] = remoteId;
            mergeNum++;
        }
        j++;
    }

    if (mergeNum >= SEQ_QUEUE_LEN) {
        LOG_WARN("Impossible, Out of range, merge seq no:" << mergeNum);
        return MMS_ERR;
    }

    for (i = 0; i < mergeNum; i++) {
        negoSeqList[i] = mergeSeqList[i];
        negoLocList[i] = mergeLocList[i];
        LOG_DEBUG("Merge remote:" << remoteId << ", loc:" << negoLocList[i] << ", seq no:" << negoSeqList[i]);
    }
    negoSeqNum = mergeNum;
    return MMS_OK;
}

BResult MmsKvServer::SyncSeqNoData(uint64_t negoSeqNo, uint16_t negoLocId, uint64_t seqList[], uint32_t seqNum,
                                   uint16_t remoteId, CmPtInfo &ptInfo, uint16_t groupIndex)
{
    for (uint32_t i = 0; i < seqNum; i++) {
        if (seqList[i] == negoSeqNo) {
            return MMS_OK;
        }
    }

    void *data;
    uint32_t len;

    LOG_DEBUG("Sync from:" << negoLocId << ", to:" << remoteId << ", seq no:" << negoSeqNo);

    auto ret = GetSeqNoData(negoSeqNo, negoLocId, ptInfo, groupIndex, &data, len);
    if (ret != MMS_OK) {
        return ret;
    }

    ret = PutSeqNoData(negoSeqNo, negoLocId, ptInfo, groupIndex, data, len);
    if (ret != MMS_OK) {
        free(data);
        return ret;
    }

    free(data);
    return MMS_OK;
}

BResult MmsKvServer::GetSeqNoData(uint64_t negoSeqNo, uint16_t negoLocId, CmPtInfo &ptInfo, uint16_t groupIndex,
                                  void **data, uint32_t &len)
{
    uint16_t localId = mCm->GetLocalNid();
    if (negoLocId == localId) {
        uint32_t ioLen = mIoCtxBuffLen;
        void *ioBuff = malloc(ioLen);
        if (ioBuff == nullptr) {
            return MMS_ALLOC_FAIL;
        }
        auto ret = mSequence->GetSeqNoData2Slv(ptInfo.ptId, groupIndex, negoSeqNo, ioBuff, ioLen);
        if (ret != MMS_OK) {
            free(ioBuff);
            return ret;
        }
        *data = ioBuff;
        len = ioLen;
        return MMS_OK;
    }

    GetSeqDataRequest req;
    req.head = { 0, MMS_OP_S_GET_SEQNO_DATA, groupIndex, ptInfo.ptId, ptInfo.version };
    req.seqNo = negoSeqNo;
    char *respData = nullptr;
    uint64_t respLen = 0;
    BResult ret = MMS_OK;
    if (mMulticast) {
        ret = mMulticastEngine->MulticastSyncCall<GetSeqDataRequest, char>(
            negoLocId, MMS_OP_S_GET_SEQNO_DATA, req, &respData, respLen);
    } else {
        ret = mNetEngine->SyncCall<GetSeqDataRequest, char>(negoLocId, groupIndex, MMS_OP_S_GET_SEQNO_DATA,
                                                            req, &respData, respLen);
    }
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get seq data failed, ret: " << ret << ", ptId:" << ptInfo.ptId << ".");
        return ret;
    }
    *data = respData;
    len = static_cast<uint32_t>(respLen);
    return MMS_OK;
}

BResult MmsKvServer::PutSeqNoData(uint64_t negoSeqNo, uint16_t negoLocId, CmPtInfo &ptInfo, uint16_t groupIndex,
                                  void *data, uint32_t len)
{
    uint16_t localId = mCm->GetLocalNid();

    IoDataRequest *req = static_cast<IoDataRequest *>(data);
    req->head = { 0, req->head.opcode, groupIndex, ptInfo.ptId, ptInfo.version };
    req->seqNo = negoSeqNo;
    req->negoSeqNo = negoSeqNo - NO_1;

    if (negoLocId == localId) {
        if (req->head.opcode == MMS_OP_S_PUT) {
            return PutLocal(data, len);
        } else if (req->head.opcode == MMS_OP_S_UPDATE) {
            return UpdateLocal(data, len);
        } else if (req->head.opcode == MMS_OP_S_DELETE) {
            return DeleteLocal(data, len);
        } else {
            LOG_ERROR("Impossible, invalid opcode:" << req->head.opcode);
            return MMS_ERR;
        }
    }

    BResult resp;
    BResult ret = MMS_OK;
    if (mMulticast) {
        ret = mMulticastEngine->MulticastSyncCall<BResult>(negoLocId, req->head.opcode, data, len, resp);
    } else {
        ret = mNetEngine->SyncCall<BResult>(negoLocId, groupIndex, req->head.opcode, data, len, resp);
    }
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Put seq data failed, ret: " << ret << ", ptId:" << ptInfo.ptId << ".");
        return ret;
    }
    if (UNLIKELY(resp != MMS_OK)) {
        LOG_ERROR("Put seq data failed, ret: " << resp << ", ptId:" << ptInfo.ptId << ".");
        return resp;
    }

    return MMS_OK;
}

void MmsKvServer::ReplyPeerRequest(ServiceContext &ctx, int32_t retCode, void *resp, uint32_t respSize)
{
    if (mMulticast) {
        mMulticastEngine->Reply(ctx, retCode, resp, respSize);
        return;
    }
    mNetEngine->Reply(ctx, retCode, resp, respSize);
}

BResult MmsKvServer::HandleGetSeqNoList(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetSeqListRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        ReplyPeerRequest(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    GetSeqListRequest *req = static_cast<GetSeqListRequest *>(ctx.MessageData());
    GetSeqListResponse rsp;
    auto ret = mSequence->GetSeqNoList2Slv(req->head.ptId, req->head.groupIndex, rsp.seqList, rsp.seqNum);
    if (ret != MMS_OK) {
        LOG_ERROR("Get seq no list failed, ret:" << ret << ", ptId:" << req->head.ptId << ", groupIndex:" <<
                  req->head.groupIndex << ".");
        ReplyPeerRequest(ctx, MMS_INNER_ERR, nullptr, 0);
    } else {
        ReplyPeerRequest(ctx, MMS_OK, &rsp, sizeof(GetSeqListResponse));
    }
    return MMS_OK;
}

BResult MmsKvServer::HandleGetSeqNoData(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetSeqDataRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        ReplyPeerRequest(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    GetSeqDataRequest *req = static_cast<GetSeqDataRequest *>(ctx.MessageData());
    uint32_t rspLen = mIoCtxBuffLen;
    void *rspData = malloc(rspLen);
    if (UNLIKELY(rspData == nullptr)) {
        LOG_ERROR("Malloc data fail, len:" << rspLen);
        ReplyPeerRequest(ctx, MMS_ALLOC_FAIL, nullptr, 0);
        return MMS_OK;
    }
    auto ret = mSequence->GetSeqNoData2Slv(req->head.ptId, req->head.groupIndex, req->seqNo, rspData, rspLen);
    if (ret != MMS_OK) {
        LOG_ERROR("Get seq no data failed, ret:" << ret << ", ptId:" << req->head.ptId << ", groupIndex:" <<
                  req->head.groupIndex << ", seq no:" << req->seqNo << ".");
        ReplyPeerRequest(ctx, MMS_INNER_ERR, nullptr, 0);
    } else {
        ReplyPeerRequest(ctx, MMS_OK, rspData, rspLen);
    }
    free(rspData);
    return MMS_OK;
}

BResult MmsKvServer::HandleUpdatePtVersion(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(BasicRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint64_t ptVersion = mCm->GetPtVersion();
    UpdatePtVRsp rsp = {ptVersion};
    mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(UpdatePtVRsp));
    return MMS_OK;
}

BResult MmsKvServer::HandleGetRouteView(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(BasicRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto nodeView = mCm->GetNodeView();
    auto ptView = mCm->GetPtView();
    if (UNLIKELY(nodeView.size() > MAX_NODES_NUM || ptView.size() > MMS_ROUTE_MAX_PT_NUM)) {
        LOG_ERROR("Route view too large, node num:" << nodeView.size() << ", pt num:" << ptView.size() << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    RouteViewResponse rsp{};
    rsp.localNid = mCm->GetLocalNid();
    rsp.nodeNum = static_cast<uint16_t>(nodeView.size());
    rsp.ptNum = mCm->GetPtNum();
    rsp.replicaNum = mReplicaNum;
    rsp.ptVersion = mCm->GetPtVersion();

    uint32_t executorNum = 0;
    NetOptions rpcOption;
    NetOptions ipcOption;
    mNetEngine->Show(executorNum, rpcOption, ipcOption);
    rsp.rpcProtocol = static_cast<uint16_t>(rpcOption.protocol);
    rsp.rpcConnCount = rpcOption.connCount;
    rsp.rpcGroupNum = rpcOption.workerGroupsNum;

    uint16_t nodeIndex = 0;
    for (const auto &elem : nodeView) {
        auto &node = rsp.nodes[nodeIndex++];
        node.nodeId = elem.second.id;
        node.status = elem.second.status;
        node.port = elem.second.port;
        if (UNLIKELY(strncpy_s(node.ip, IP_SIZE, elem.second.ip.c_str(), elem.second.ip.size() + NO_1) != MMS_OK)) {
            LOG_ERROR("Copy route node ip failed, node id:" << elem.second.id << ", ip:" << elem.second.ip << ".");
            mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
            return MMS_OK;
        }
    }

    uint16_t ptIndex = 0;
    for (const auto &elem : ptView) {
        auto &pt = rsp.pts[ptIndex++];
        pt.version = elem.second.version;
        pt.ptId = elem.second.ptId;
        pt.state = elem.second.state;
        pt.masterNodeId = elem.second.masterNodeId;
        pt.copyNum = static_cast<uint16_t>(std::min(elem.second.copys.size(), static_cast<size_t>(MAX_NODES_NUM)));
        for (uint16_t copyIndex = 0; copyIndex < pt.copyNum; copyIndex++) {
            pt.copys[copyIndex].nodeId = elem.second.copys[copyIndex].nodeId;
            pt.copys[copyIndex].state = elem.second.copys[copyIndex].state;
        }
    }

    mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(RouteViewResponse));
    return MMS_OK;
}

}
}
