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
