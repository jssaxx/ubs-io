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

thread_local std::vector<PutItems> MmsKvServer::itemListPut = [] {
    return std::vector<PutItems>(mMaxPutItemNum);
}();

thread_local std::vector<UpdateItems> MmsKvServer::itemListUpdate = [] {
    return std::vector<UpdateItems>(mMaxUpdateItemNum);
}();

thread_local std::vector<DeleteItems> MmsKvServer::itemListDelete = [] {
    return std::vector<DeleteItems>(mMaxDeleteItemNum);
}();

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

}
}

