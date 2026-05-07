/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include "mms_log.h"
#include "mms_comm.h"
#include "mms_functions.h"
#include "mms_config_instance.h"
#include "mms_server.h"
#include "mms_trace.h"
#include "mms_kv_server.h"

namespace ock {
namespace mms {

static thread_local uint16_t g_groupIndex = NumaGroupIndex::Instance()->GetGroupIndex(); // one-time execution
static constexpr uint16_t IO_LOC_DESC_LEN = sizeof(IoLocDesc);

uint32_t MmsKvServer::mMaxPutItemNum = 0;
uint32_t MmsKvServer::mMaxUpdateItemNum = 0;
uint32_t MmsKvServer::mMaxDeleteItemNum = 0;

BResult MmsKvServer::Initialize()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return MMS_OK;
    }

    uint32_t lev1Cap = static_cast<uint32_t>(MmsServer::Instance()->GetConfig()->GetCmConfig().nodeNum);
    uint32_t lev2Cap = static_cast<uint32_t>(MmsServer::Instance()->GetConfig()->GetNetConfig().rpcWorkerGroupsNum);
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
    mMemAllocator = MmsServer::Instance()->GetMemAllocator();
    mCache = MmsServer::Instance()->GetCache();
    allocFunc = [this](uint64_t size, uint16_t &numaId, uintptr_t &blockAddr) {
        return mMemAllocator->MmsAlloc(size, numaId, blockAddr);
    };

    mIoTimeOut = MmsServer::Instance()->GetConfig()->GetCmConfig().registeredTimeoutSec * IO_RETRY_NUM;
    mMulticast = MmsServer::Instance()->GetConfig()->GetBasicConfig().multicastSwitch;
    uint32_t ioCtxBuffLen = MmsServer::Instance()->GetConfig()->GetNetConfig().msgMaxBuffSize;
    mIoCtxBuffLen = ioCtxBuffLen;
    MmsKvServer::mMaxPutItemNum =
        (ioCtxBuffLen - sizeof(IoDataRequest)) / (sizeof(IoLocDesc) + MIN_KEY_SIZE + MIN_VALUE_SIZE);
    MmsKvServer::mMaxUpdateItemNum = MmsKvServer::mMaxPutItemNum;
    MmsKvServer::mMaxDeleteItemNum = (ioCtxBuffLen - sizeof(IoDataRequest)) / (sizeof(IoLocDesc) + MIN_KEY_SIZE);

    RegisterOpcode();
    mStarted = true;
    return MMS_OK;
}

void MmsKvServer::RegisterOpcode()
{
    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_BASIC,
        std::bind(&MmsKvServer::HandleBasic, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_SERVICEABLE,
        std::bind(&MmsKvServer::HandleServiceable, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_PUT,
        std::bind(&MmsKvServer::HandlePut, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_PUT,
        std::bind(&MmsKvServer::HandlePutRemote, this, std::placeholders::_1)); // 处理单点put

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_MULTI_PUT,
        std::bind(&MmsKvServer::HandlePutRemoteMulti, this, std::placeholders::_1)); // 处理组播put

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_UPDATE,
        std::bind(&MmsKvServer::HandleUpdate, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_UPDATE,
        std::bind(&MmsKvServer::HandleUpdateRemote, this, std::placeholders::_1)); // 处理单点update

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_MULTI_UPDATE,
        std::bind(&MmsKvServer::HandleUpdateRemoteMulti, this, std::placeholders::_1)); // 处理组播update

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_DELETE,
        std::bind(&MmsKvServer::HandleDelete, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_C_REPLACE,
        std::bind(&MmsKvServer::HandleReplace, this, std::placeholders::_1));

    mNetEngine->RegisterNewRequestHandler(MMS_OP_S_REPLACE,
        std::bind(&MmsKvServer::HandleReplaceRemote, this, std::placeholders::_1)); // 处理单点replace

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
}

void MmsKvServer::FreeBlocks(std::vector<IOCtxItem> &ctxItems)
{
    for (auto &item : ctxItems) {
        mMemAllocator->MmsFree(item.buff);
    }
}

BResult MmsKvServer::SendSingleReq(uint64_t userId, const IoHandle &handle, IOCtxItem &item)
{
    BResult ret = MMS_OK;
    ret = handle(userId, reinterpret_cast<void *>(item.buff), static_cast<uint32_t>(item.reqLen));
    if (LIKELY(ret == MMS_OK)) {
        return MMS_OK;
    }

    ret = FailHandle(ret, userId, reinterpret_cast<void *>(item.buff), static_cast<uint32_t>(item.reqLen), handle);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Handle send request failed, ret:" << ret << ".");
        return ret;
    }

    return ret;
}

BResult MmsKvServer::HandleSendReqs(std::vector<IOCtxItem> &ctxItems, uint64_t userId, const IoHandle &handle)
{
    BResult ret;
    for (auto &item : ctxItems) {
        ret = SendSingleReq(userId, handle, item);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Send single request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            return ret;
        }
    }

    FreeBlocks(ctxItems);
    return MMS_OK;
}

BResult MmsKvServer::Put(uint64_t userId, PutItems *itemList, uint32_t itemNum)
{
    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }

    static IoHandle handle = [this](uint64_t userId, void *ioBuff, uint32_t ioLen) -> BResult {
        if (mMulticast) {
            return HandlePutMultiImpl(userId, ioBuff, ioLen);
        } else {
            return HandlePutDefImpl(userId, ioBuff, ioLen);
        }
    };

    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodePutRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc, mIoCtxBuffLen);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(ctxItems, userId, handle);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(ctxItems, userId, handle);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            curItemIndex += ctxItems.size();
            LOG_DEBUG("Send batch put success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                            << ".");
            continue;
        } else {
            LOG_ERROR("Encode put request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            return ret;
        }
    }

    return MMS_OK;
}

BResult MmsKvServer::Get(uint64_t userId, GetItems *itemList, uint32_t itemNum)
{
    uint16_t index;

    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }

    for (index = 0; index < itemNum; index++) {
        auto ret = mCache->Get(itemList[index].key, itemList[index].offset, itemList[index].length,
            itemList[index].value, itemList[index].realLength);
        if (LIKELY(ret == MMS_OK)) {
            continue;
        }

        if ((itemNum > NO_1) && (ret == MMS_NOT_EXISTS)) { // 批量get时忽略不存在的key
            LOG_WARN("Get cache failed, ret:" << ret << ", key:" << itemList[index].key << ".");
            continue;
        } else {
            LOG_ERROR("Get cache failed, ret:" << ret << ", key:" << itemList[index].key << ".");
            return ret;
        }
    }

    return MMS_OK;
}

BResult MmsKvServer::Update(uint64_t userId, UpdateItems *itemList, uint32_t itemNum)
{
    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }

    static IoHandle handle = [this](uint64_t userId, void *ioBuff, uint32_t ioLen) -> BResult {
        if (mMulticast) {
            return HandleUpdateMultiImpl(userId, ioBuff, ioLen);
        } else {
            return HandleUpdateDefImpl(userId, ioBuff, ioLen);
        }
    };

    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeUpdateRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc, mIoCtxBuffLen);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(ctxItems, userId, handle);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(ctxItems, userId, handle);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            curItemIndex += ctxItems.size();
            LOG_DEBUG("Send batch update success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                               << ".");
            continue;
        } else {
            LOG_ERROR("Encode update request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            return ret;
        }
    }

    return MMS_OK;
}

BResult MmsKvServer::Delete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum)
{
    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }

    static IoHandle handle = [this](uint64_t userId, void *ioBuff, uint32_t ioLen) -> BResult {
        if (mMulticast) {
            return HandleDeleteMultiImpl(userId, ioBuff, ioLen);
        } else {
            return HandleDeleteDefImpl(userId, ioBuff, ioLen);
        }
    };

    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeDeleteRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc, mIoCtxBuffLen);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(ctxItems, userId, handle);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(ctxItems, userId, handle);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            curItemIndex += ctxItems.size();
            LOG_DEBUG("Send batch delete success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                               << ".");
            continue;
        } else {
            LOG_ERROR("Encode delete request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            return ret;
        }
    }

    return MMS_OK;
}

BResult MmsKvServer::Replace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum)
{
    if (UNLIKELY(!mServiceable.load(std::memory_order_acquire))) {
        LOG_WARN("Service is not available.");
        return MMS_NOT_READY;
    }

    static IoHandle handle = [this](uint64_t userId, void *ioBuff, uint32_t ioLen) -> BResult {
        if (mMulticast) {
            return HandleReplaceMultiImpl(userId, ioBuff, ioLen);
        } else {
            return HandleReplaceDefImpl(userId, ioBuff, ioLen);
        }
    };

    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeReplaceRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc, mIoCtxBuffLen);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(ctxItems, userId, handle);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(ctxItems, userId, handle);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            curItemIndex += ctxItems.size();
            LOG_DEBUG("Send batch replace success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                               << ".");
            continue;
        } else {
            LOG_ERROR("Encode replace request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            return ret;
        }
    }

    return MMS_OK;
}

BResult MmsKvServer::FailHandle(BResult lastRet, uint64_t userId, void *ioBuff, uint32_t ioLen, const IoHandle &handle)
{
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = lastRet;

    do {
        bool isContinue = (ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY ||
            ret == MMS_NET_RETRY || ret == MMS_CHECK_PT_FAIL);
        if (!isContinue) {
            break;
        }

        sleep(IO_RETRY_INTERAL);

        uint64_t costTime = Monotonic::TimeSec() - startTime;
        if (costTime >= mIoTimeOut) {
            break;
        }

        ret = handle(userId, ioBuff, ioLen);
    } while (true);

    return ret;
}

BResult MmsKvServer::HandleBasic(ServiceContext &ctx)
{
    BasicResponse rsp;

    MmsConfigPtr config = MmsServer::Instance()->GetConfig();
    rsp.serverPid = getpid();
    rsp.netTimeOut = config->GetCmConfig().registeredTimeoutSec; // 同zk心跳超时;
    rsp.ioTimeOut = rsp.netTimeOut * IO_RETRY_NUM; // 默认心跳超时x倍，确保IO RETRY重试OK;
    rsp.logLevel = config->GetBasicConfig().logLevel;
    rsp.traceSwitch = config->GetBasicConfig().traceSwitch;
    rsp.enableCrc = config->GetBasicConfig().crcSwitch;
    rsp.maxMsgBuffSize = config->GetNetConfig().msgMaxBuffSize;
    rsp.minBlockSize = config->GetMemConfig().minBlockSize;
    rsp.maxBlockSize = config->GetMemConfig().maxBlockSize;
    rsp.minBlockSizeRate = static_cast<uint8_t>(config->GetMemConfig().blockRate.first);

    int32_t fds[MMAP_AREA_BUTT];
    mMemMgr->GetAreaMemDesc(MMAP_AREA_IOCTX, fds[MMAP_AREA_IOCTX]);
    mMemMgr->GetAreaMemDesc(MMAP_AREA_BUCKET, fds[MMAP_AREA_BUCKET]);
    mMemMgr->GetAreaMemDesc(MMAP_AREA_VALUE, fds[MMAP_AREA_VALUE]);
    mMemMgr->GetAreaMemDesc(MMAP_AREA_INDEX, fds[MMAP_AREA_INDEX]);
    mMemMgr->GetNumaMemDesc(rsp.memNumaId, rsp.memSize, rsp.memNum);
    auto ret = mNetEngine->SendFds(ctx.Channel(), fds, MMAP_AREA_BUTT);
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

BResult MmsKvServer::HandlePut(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(IoCtrlRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    BResult ret = MMS_OK;
    IoCtrlRequest *req = static_cast<IoCtrlRequest *>(ctx.MessageData());
    if (UNLIKELY(req->ioLength > mIoCtxBuffLen)) {
        LOG_ERROR("Invalid io buff length:" << req->ioLength << ", must be less than " <<  mIoCtxBuffLen << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint64_t localPtV = mCm->GetPtVersion();
    if (UNLIKELY(req->head.ptv != localPtV)) {
        LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:" << localPtV << ".");
        mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
        return MMS_OK;
    }

    uint64_t ioBuff;
    mMemMgr->Trans2Addr(MMAP_AREA_IOCTX, req->ioNumaOffset, ioBuff);

    if (mMulticast) {
        ret = HandlePutMultiImpl(req->userId, reinterpret_cast<void *>(ioBuff), static_cast<uint32_t>(req->ioLength));
    } else {
        ret = HandlePutDefImpl(req->userId, reinterpret_cast<void *>(ioBuff), static_cast<uint32_t>(req->ioLength));
    }
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandlePutDefImpl(uint64_t userId, void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteId[MAX_NODES_NUM];
    uint16_t remoteNum;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteId, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ", userId:" << userId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { 0, MMS_OP_S_PUT, g_groupIndex, ptId, ptv };

    int32_t quotaNum = remoteNum + NO_1;

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

    PutRemote(remoteId, remoteNum, ioBuff, ioLen, callback);
    ret = PutLocal(ioBuff, ioLen);
    callback.cb(callback.cbCtx, nullptr, 0, ret);

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result;
}

BResult MmsKvServer::HandlePutMultiImpl(uint64_t userId, void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    std::unordered_set<std::string>  remoteIps{};
    remoteIps.reserve(MAX_NODES_NUM - 1);
    uint16_t remoteNum = 0;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteIps);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ", userId:" << userId << ".");
        return ret;
    }
    remoteNum = remoteIps.size();

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
        PutRemoteMulticast(remoteIps, ioBuff, ioLen, callback);
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
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) || UNLIKELY(ctx.MessageDataLen() > mIoCtxBuffLen) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = PutLocal(ctx.MessageData(), ctx.MessageDataLen());
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandlePutRemoteMulti(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) || UNLIKELY(ctx.MessageDataLen() > mIoCtxBuffLen) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mMulticastEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = PutLocal(ctx.MessageData(), ctx.MessageDataLen());
    mMulticastEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

void MmsKvServer::PutRemoteMulticast(const std::unordered_set<std::string> &remoteIps, void *ioBuff, uint32_t ioLen,
                                     Callback &callback)
{
    mMulticastEngine->MulticastAsyncCallBuff(remoteIps, ioBuff, ioLen, callback);
}

void MmsKvServer::PutRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback)
{
    for (uint16_t i = 0; i < remoteNum; i++) {
        mNetEngine->AsyncCallBuff(remoteId[i], g_groupIndex, MMS_OP_S_PUT, ioBuff, ioLen, callback);
    }
}

BResult MmsKvServer::PutLocal(void *ioBuff, uint32_t ioLen)
{
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

    ret = DeCodePutRequest(itemListPut, itemNum, reinterpret_cast<uint64_t>(ioBuff), ioLen);
    if (ret != MMS_OK) {
        LOG_ERROR("Decode put request fail, ret:" << ret << ", ptId:" << req->head.ptId);
        return ret;
    }

    BResult result = MMS_OK;
    for (index = 0; index < itemNum; index++) {
        if (mServiceable.load(std::memory_order_acquire)) {
            ret = mCache->Put(itemListPut[index].key, itemListPut[index].value, itemListPut[index].length,
                              static_cast<uint32_t>(itemListPut[index].version), req->head.ptId);
        } else {
            ret = mCache->Replace({itemListPut[index].key, itemListPut[index].value, 0, itemListPut[index].length,
                                   static_cast<uint32_t>(itemListPut[index].version), req->head.ptId});
        }

        if (UNLIKELY(ret != MMS_OK && ret != MMS_PUT_REPEAT)) {
            LOG_ERROR("Put cache fail, ret:" << ret << ", key:" << itemListPut[index].key << ", ptId:" << req->head.ptId
                                             << ".");
            result = ret;
            break;
        }
    }
    return result;
}

thread_local std::vector<PutItems> MmsKvServer::itemListPut = [] {
    return std::vector<PutItems>(mMaxPutItemNum);
}();

thread_local std::vector<UpdateItems> MmsKvServer::itemListUpdate = [] {
    return std::vector<UpdateItems>(mMaxUpdateItemNum);
}();

thread_local std::vector<DeleteItems> MmsKvServer::itemListDelete = [] {
    return std::vector<DeleteItems>(mMaxDeleteItemNum);
}();

BResult MmsKvServer::HandleUpdate(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(IoCtrlRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    BResult ret = MMS_OK;
    IoCtrlRequest *req = static_cast<IoCtrlRequest *>(ctx.MessageData());
    if (UNLIKELY(req->ioLength > mIoCtxBuffLen)) {
        LOG_ERROR("Invalid io buff length:" << req->ioLength << ", must be less than " <<  mIoCtxBuffLen << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint64_t localPtV = mCm->GetPtVersion();
    if (UNLIKELY(req->head.ptv != localPtV)) {
        LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:" << localPtV << ".");
        mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
        return MMS_OK;
    }

    uint64_t ioBuff;
    mMemMgr->Trans2Addr(MMAP_AREA_IOCTX, req->ioNumaOffset, ioBuff);

    if (mMulticast) {
        ret = HandleUpdateMultiImpl(req->userId, reinterpret_cast<void *>(ioBuff),
                                    static_cast<uint32_t>(req->ioLength));
    } else {
        ret = HandleUpdateDefImpl(req->userId, reinterpret_cast<void *>(ioBuff), static_cast<uint32_t>(req->ioLength));
    }
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandleUpdateDefImpl(uint64_t userId, void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteId[MAX_NODES_NUM];
    uint16_t remoteNum;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteId, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ", userId:" << userId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { 0, MMS_OP_S_UPDATE, g_groupIndex, ptId, ptv };

    int32_t quotaNum = remoteNum + NO_1;

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

    UpdateRemote(remoteId, remoteNum, ioBuff, ioLen, callback);
    ret = UpdateLocal(ioBuff, ioLen);
    callback.cb(callback.cbCtx, nullptr, 0, ret);

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result;
}

BResult MmsKvServer::HandleUpdateMultiImpl(uint64_t userId, void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    std::unordered_set<std::string>  remoteIps{};
    remoteIps.reserve(MAX_NODES_NUM - 1);
    uint16_t remoteNum = 0;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteIps);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ", userId:" << userId << ".");
        return ret;
    }
    remoteNum = remoteIps.size();

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
        UpdateRemoteMulticast(remoteIps, ioBuff, ioLen, callback);
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
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) || UNLIKELY(ctx.MessageDataLen() > mIoCtxBuffLen) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = UpdateLocal(ctx.MessageData(), ctx.MessageDataLen());
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
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

void MmsKvServer::UpdateRemoteMulticast(const std::unordered_set<std::string> &remoteIps, void *ioBuff, uint32_t ioLen,
                                        Callback &callback)
{
    mMulticastEngine->MulticastAsyncCallBuff(remoteIps, ioBuff, ioLen, callback);
}

void MmsKvServer::UpdateRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback)
{
    for (uint16_t i = 0; i < remoteNum; i++) {
        mNetEngine->AsyncCallBuff(remoteId[i], g_groupIndex, MMS_OP_S_UPDATE, ioBuff, ioLen, callback);
    }
}

BResult MmsKvServer::UpdateLocal(void *ioBuff, uint32_t ioLen)
{
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
        LOG_ERROR("Decode update request fail, ret:" << ret << ", ptId:" << req->head.ptId);
        return ret;
    }

    for (index = 0; index < itemNum; index++) {
        if (mServiceable.load(std::memory_order_acquire)) {
            ret = mCache->Update(itemListUpdate[index].key, itemListUpdate[index].value, itemListUpdate[index].offset,
                                 itemListUpdate[index].length, static_cast<uint32_t>(itemListPut[index].version));
        } else {
            ret = mCache->Replace({itemListUpdate[index].key, itemListUpdate[index].value, itemListUpdate[index].offset,
                                   itemListUpdate[index].length, static_cast<uint32_t>(itemListPut[index].version),
                                   req->head.ptId});
        }
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Update cache fail, ret:" << ret << ", key:" << itemListUpdate[index].key << ", ptId:" <<
                      req->head.ptId);
            break;
        }
    }
    return ret;
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
    if (UNLIKELY(req->ioLength > mIoCtxBuffLen)) {
        LOG_ERROR("Invalid io buff length:" << req->ioLength << ", must be less than " <<  mIoCtxBuffLen << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint64_t localPtV = mCm->GetPtVersion();
    if (UNLIKELY(req->head.ptv != localPtV)) {
        LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:" << localPtV << ".");
        mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
        return MMS_OK;
    }

    uint64_t ioBuff;
    mMemMgr->Trans2Addr(MMAP_AREA_IOCTX, req->ioNumaOffset, ioBuff);

    if (mMulticast) {
        ret = HandleDeleteMultiImpl(req->userId, reinterpret_cast<void *>(ioBuff),
                                    static_cast<uint32_t>(req->ioLength));
    } else {
        ret = HandleDeleteDefImpl(req->userId, reinterpret_cast<void *>(ioBuff), static_cast<uint32_t>(req->ioLength));
    }
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandleDeleteDefImpl(uint64_t userId, void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteId[MAX_NODES_NUM];
    uint16_t remoteNum;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteId, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ", userId:" << userId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { 0, MMS_OP_S_DELETE, g_groupIndex, ptId, ptv };

    int32_t quotaNum = remoteNum + NO_1;

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

    DeleteRemote(remoteId, remoteNum, ioBuff, ioLen, callback);
    ret = DeleteLocal(ioBuff, ioLen);
    callback.cb(callback.cbCtx, nullptr, 0, ret);

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result;
}

BResult MmsKvServer::HandleDeleteMultiImpl(uint64_t userId, void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    std::unordered_set<std::string>  remoteIps{};
    remoteIps.reserve(MAX_NODES_NUM - 1);
    uint16_t remoteNum = 0;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteIps);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ", userId:" << userId << ".");
        return ret;
    }
    remoteNum = remoteIps.size();

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
        DeleteRemoteMulticast(remoteIps, ioBuff, ioLen, callback);
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
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) || UNLIKELY(ctx.MessageDataLen() > mIoCtxBuffLen) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = DeleteLocal(ctx.MessageData(), ctx.MessageDataLen());
    mNetEngine->Reply(ctx, ret, nullptr, 0);
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

    auto ret = DeleteLocal(ctx.MessageData(), ctx.MessageDataLen());
    mMulticastEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
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
    if (UNLIKELY(req->ioLength > mIoCtxBuffLen)) {
        LOG_ERROR("Invalid io buff length:" << req->ioLength << ", must be less than " <<  mIoCtxBuffLen << ".");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    uint64_t localPtV = mCm->GetPtVersion();
    if (UNLIKELY(req->head.ptv != localPtV)) {
        LOG_ERROR("Client pt version is lower, old version:" << req->head.ptv << ", new version:" << localPtV << ".");
        mNetEngine->Reply(ctx, MMS_NEED_UPDATE_PT_VERSION, nullptr, 0);
        return MMS_OK;
    }

    uint64_t ioBuff;
    mMemMgr->Trans2Addr(MMAP_AREA_IOCTX, req->ioNumaOffset, ioBuff);

    if (mMulticast) {
        ret = HandleReplaceMultiImpl(req->userId, reinterpret_cast<void *>(ioBuff),
                                     static_cast<uint32_t>(req->ioLength));
    } else {
        ret = HandleReplaceDefImpl(req->userId, reinterpret_cast<void *>(ioBuff), static_cast<uint32_t>(req->ioLength));
    }
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
}

BResult MmsKvServer::HandleReplaceDefImpl(uint64_t userId, void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    uint16_t remoteId[MAX_NODES_NUM];
    uint16_t remoteNum;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteId, remoteNum);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ", userId:" << userId << ".");
        return ret;
    }

    ret = mSequence->ApplyForSeqNo2Mst(ptId, g_groupIndex, req->seqNo, req->negoSeqNo);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Apply for seq no fail, ret: " << ret << ", ptId:" << ptId << ", groupIndex:" << g_groupIndex << ".");
        return ret;
    }

    req->head = { 0, MMS_OP_S_REPLACE, g_groupIndex, ptId, ptv };

    int32_t quotaNum = remoteNum + NO_1;

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

    ReplaceRemote(remoteId, remoteNum, ioBuff, ioLen, callback);
    ret = ReplaceLocal(ioBuff, ioLen);
    callback.cb(callback.cbCtx, nullptr, 0, ret);

    while (cbCtx.quota.load(std::memory_order_acquire) != 0) {
        CPU_RELAX();
    }

    mSequence->ReleaseSeqNo2Mst(ptId, g_groupIndex, req->seqNo);

    return cbCtx.result;
}

BResult MmsKvServer::HandleReplaceMultiImpl(uint64_t userId, void *ioBuff, uint32_t ioLen)
{
    uint16_t ptId;
    uint64_t ptv;
    std::unordered_set<std::string>  remoteIps{};
    remoteIps.reserve(MAX_NODES_NUM - 1);
    uint16_t remoteNum = 0;

    IoDataRequest *req = static_cast<IoDataRequest *>(ioBuff);

    auto ret = mCm->GetPtInfo(ptId, ptv, remoteIps);
    if (UNLIKELY(ret != MMS_OK)) {
        LOG_ERROR("Get pt failed, ret: " << ret << ", ptId:" << ptId << ", userId:" << userId << ".");
        return ret;
    }
    remoteNum = remoteIps.size();

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
        ReplaceRemoteMulticast(remoteIps, ioBuff, ioLen, callback);
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
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(IoDataRequest)) || UNLIKELY(ctx.MessageDataLen() > mIoCtxBuffLen) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    auto ret = ReplaceLocal(ctx.MessageData(), ctx.MessageDataLen());
    mNetEngine->Reply(ctx, ret, nullptr, 0);
    return MMS_OK;
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

void MmsKvServer::ReplaceRemoteMulticast(const std::unordered_set<std::string> &remoteIps, void *ioBuff,
                                         uint32_t ioLen, Callback &callback)
{
    mMulticastEngine->MulticastAsyncCallBuff(remoteIps, ioBuff, ioLen, callback);
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
        LOG_ERROR("Decode replace request fail, ret:" << ret << ", ptId:" << req->head.ptId);
        return ret;
    }

    for (index = 0; index < itemNum; index++) {
        ret = mCache->Replace({itemListUpdate[index].key, itemListUpdate[index].value, itemListUpdate[index].offset,
                               itemListUpdate[index].length, static_cast<uint32_t>(itemListUpdate[index].version),
                               req->head.ptId});
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Replace fail, ret:" << ret << ", key:" << itemListUpdate[index].key);
            break;
        }

    }
    return ret;
}

void MmsKvServer::DeleteRemoteMulticast(const std::unordered_set<std::string> &remoteIps, void *ioBuff, uint32_t ioLen,
                                        Callback &callback)
{
    mMulticastEngine->MulticastAsyncCallBuff(remoteIps, ioBuff, ioLen, callback);
}

void MmsKvServer::DeleteRemote(uint16_t remoteId[], int32_t remoteNum, void *ioBuff, uint32_t ioLen, Callback &callback)
{
    for (uint16_t i = 0; i < remoteNum; i++) {
        mNetEngine->AsyncCallBuff(remoteId[i], g_groupIndex, MMS_OP_S_DELETE, ioBuff, ioLen, callback);
    }
}

BResult MmsKvServer::DeleteLocal(void *ioBuff, uint32_t ioLen)
{
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
    for (index = 0; index < itemNum; index++) {
        ret = mCache->Delete(itemListDelete[index].key, itemListDelete[index].version);
        if (ret != MMS_OK && ret != MMS_KEY_NOT_EXISTS) {
            LOG_ERROR("Delete cache fail, ret:" << ret << ", key:" << itemListDelete[index].key << ", ptId:" <<
                      req->head.ptId);
            result = ret;
            break;
        }
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

    auto ret = mNetEngine->SyncCall<GetSeqListRequest, GetSeqListResponse>(nid, groupIndex, MMS_OP_S_GET_SEQNO_LIST,
                                                                           req, rsp);
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
    auto ret = mNetEngine->SyncCall<GetSeqDataRequest, char>(negoLocId, groupIndex, MMS_OP_S_GET_SEQNO_DATA,
                                                             req, &respData, respLen);
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
    auto ret = mNetEngine->SyncCall<BResult>(negoLocId, groupIndex, req->head.opcode, data, len, resp);
    if (UNLIKELY(ret != MMS_OK || resp != MMS_OK)) {
        LOG_ERROR("Put seq data failed, ret: " << ret << ", ptId:" << ptInfo.ptId << ".");
        return ret;
    }

    return MMS_OK;
}

BResult MmsKvServer::HandleGetSeqNoList(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetSeqListRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    GetSeqListRequest *req = static_cast<GetSeqListRequest *>(ctx.MessageData());
    GetSeqListResponse rsp;
    auto ret = mSequence->GetSeqNoList2Slv(req->head.ptId, req->head.groupIndex, rsp.seqList, rsp.seqNum);
    if (ret != MMS_OK) {
        LOG_ERROR("Get seq no list failed, ret:" << ret << ", ptId:" << req->head.ptId << ", groupIndex:" <<
                  req->head.groupIndex << ".");
        mNetEngine->Reply(ctx, MMS_INNER_ERR, nullptr, 0);
    } else {
        mNetEngine->Reply(ctx, MMS_OK, &rsp, sizeof(GetSeqListResponse));
    }
    return MMS_OK;
}

BResult MmsKvServer::HandleGetSeqNoData(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(GetSeqDataRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive message len:" << ctx.MessageDataLen() << " or message data invalid.");
        mNetEngine->Reply(ctx, MMS_INVALID_PARAM, nullptr, 0);
        return MMS_OK;
    }

    GetSeqDataRequest *req = static_cast<GetSeqDataRequest *>(ctx.MessageData());
    uint32_t rspLen = mIoCtxBuffLen;
    void *rspData = malloc(rspLen);
    if (UNLIKELY(rspData == nullptr)) {
        LOG_ERROR("Malloc data fail, len:" << rspLen);
        mNetEngine->Reply(ctx, MMS_ALLOC_FAIL, nullptr, 0);
        return MMS_OK;
    }
    auto ret = mSequence->GetSeqNoData2Slv(req->head.ptId, req->head.groupIndex, req->seqNo, rspData, rspLen);
    if (ret != MMS_OK) {
        LOG_ERROR("Get seq no data failed, ret:" << ret << ", ptId:" << req->head.ptId << ", groupIndex:" <<
                  req->head.groupIndex << ", seq no:" << req->seqNo << ".");
        mNetEngine->Reply(ctx, MMS_INNER_ERR, nullptr, 0);
    } else {
        mNetEngine->Reply(ctx, MMS_OK, rspData, rspLen);
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

}
}

