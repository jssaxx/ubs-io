/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include <functional>
#include <string>
#include <vector>
#include <sched.h>
#include <dlfcn.h>
#include "mms_comm.h"
#include "mms_client_log.h"
#include "mms_trace.h"
#include "mms_functions.h"
#include "mms_client.h"
#include "mms_monotonic.h"
#include "mms_kv_client.h"

namespace ock {
namespace mms {

BResult MmsKvClient::Initialize(const KvClientPara &para)
{
    mCache = para.cache;
    mNetEngine = para.netEngine;
    mMemMgr = para.memMgr;
    mMemAllocator = para.memAllocator;
    mIoTimeOut = para.ioTimeOut;
    mMaxMsgBuffSize = para.maxMsgBuffSize;
    allocFunc = [this](uint64_t size, uint16_t &numaId, uintptr_t &blockAddr) {
        return mMemAllocator->MmsAlloc(size, numaId, blockAddr);
    };

    UpdateLocalPtVersion(mPtVersion);
    return MMS_OK;
}

BResult MmsKvClient::Start(void)
{
    return MMS_OK;
}

void MmsKvClient::Exit(void)
{
    return;
}

BResult MmsKvClient::SendSingleReq(IoCtrlRequest &req)
{
    BResult rsp = MMS_OK;
    BResult ret = MMS_OK;
    MmsOpCode opCode = static_cast<MmsOpCode>(req.head.opcode);
    ret = mNetEngine->SyncCall<IoCtrlRequest, BResult>(INVALID_NID, 0, opCode, req, rsp);
    if (LIKELY(ret == MMS_OK && rsp == MMS_OK)) {
        return MMS_OK;
    }

    ret = FailHandle(ret, opCode, req, rsp);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Send put request failed, ret:" << ret << ", opCode:" << opCode << ".");
    }

    return ret;
}

void MmsKvClient::FreeBlocks(std::vector<IOCtxItem> &ctxItems)
{
    for (auto &item : ctxItems) {
        mMemAllocator->MmsFree(item.buff);
    }
}

BResult MmsKvClient::HandleSendReqs(uint16_t numaId, uint64_t userId, MmsOpCode opCode,
                                    std::vector<IOCtxItem> &ctxItems)
{
    BResult ret;
    uint64_t numaOffset;
    for (auto &item : ctxItems) {
        mMemMgr->Trans2Offset(MMAP_AREA_IOCTX, item.buff, numaOffset);
        IoCtrlRequest req = {{0, opCode, 0, 0, mPtVersion.load(std::memory_order_acquire)},
                             userId,
                             numaId,
                             numaOffset,
                             item.reqLen};
        ret = SendSingleReq(req);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Send single request failed, ret:" << ret << ", opCode:" << opCode << ".");
            FreeBlocks(ctxItems);
            return ret;
        }
    }

    FreeBlocks(ctxItems);
    return MMS_OK;
}

BResult MmsKvClient::MmsPut(uint64_t userId, PutItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    uint16_t numaId = mMemAllocator->GetNumaId();

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodePutRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc, mMaxMsgBuffSize);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_PUT, ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_PUT, ctxItems);
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

BResult MmsKvClient::MmsGet(uint64_t userId, GetItems *itemList, uint32_t itemNum)
{
    uint16_t index;

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
            CLIENT_LOG_ERROR("Get cache failed, ret:" << ret << ", key:" << itemList[index].key << ".");
            return ret;
        }
    }

    return MMS_OK;
}

BResult MmsKvClient::MmsUpdate(uint64_t userId, UpdateItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    uint16_t numaId = mMemAllocator->GetNumaId();

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeUpdateRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc,
                                  mMaxMsgBuffSize);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_UPDATE, ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_UPDATE, ctxItems);
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

BResult MmsKvClient::MmsDelete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    uint16_t numaId = mMemAllocator->GetNumaId();

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeDeleteRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc,
                                  mMaxMsgBuffSize);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_DELETE, ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_DELETE, ctxItems);
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

BResult MmsKvClient::MmsReplace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    uint16_t numaId = mMemAllocator->GetNumaId();

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeReplaceRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc,
                                   mMaxMsgBuffSize);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_REPLACE, ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_REPLACE, ctxItems);
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

void MmsKvClient::HandleUpdatePtVersion(uint64_t ptVersion)
{
    mPtVersion.store(ptVersion, std::memory_order_release);
    UpdateLocalPtVersion(ptVersion);
    LOG_INFO("Update client pt version:" << ptVersion << ".");
}

BResult MmsKvClient::UpdateClientPtVersion()
{
    BResult ret = MMS_OK;
    UpdatePtVRsp rsp;
    BasicRequest req = {{0, MMS_OP_C_UPDATE_PT_VERSION, 0, 0, 0}};
    uint16_t retryCount = 0;

    do {
        ret = mNetEngine->SyncCall<BasicRequest, UpdatePtVRsp>(INVALID_NID, 0, MMS_OP_C_UPDATE_PT_VERSION,
                                                               req, rsp);
        if (LIKELY(ret == MMS_OK)) {
            HandleUpdatePtVersion(rsp.ptVersion);
            break;
        }

        LOG_ERROR("Send request failed, ret:" << ret << ", retry count:" << ++retryCount << ".");
        if (retryCount > RETRY_COUNT) {
            LOG_ERROR("Send request failed after " << retryCount << " retries, exiting.");
            break;
        }

        sleep(RETRY_SLEEP);
        bool isContinue =
            (ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY || ret == MMS_NET_RETRY || ret == MMS_CHECK_PT_FAIL);
        if (!isContinue) {
            break;
        }
    } while (true);

    return ret;
}

BResult MmsKvClient::FailHandle(BResult lastRet, MmsOpCode opCode, IoCtrlRequest &req, BResult &rsp)
{
    uint64_t startTime = Monotonic::TimeSec();
    BResult ret = lastRet;

    do {
        if (ret == MMS_NEED_UPDATE_PT_VERSION) {
            BResult updateRet = UpdateClientPtVersion();
            if (UNLIKELY(updateRet != MMS_OK)) {
                return updateRet;
            }

            req.head.ptv = mPtVersion.load(std::memory_order_acquire);
        }

        bool isContinue = (ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY ||
            ret == MMS_NET_RETRY || ret == MMS_CHECK_PT_FAIL || ret == MMS_NEED_UPDATE_PT_VERSION);
        if (!isContinue) {
            break;
        }

        sleep(IO_RETRY_INTERAL);

        uint64_t costTime = Monotonic::TimeSec() - startTime;
        if (costTime >= mIoTimeOut) {
            break;
        }

        ret = mNetEngine->SyncCall<IoCtrlRequest, BResult>(INVALID_NID, 0, opCode, req, rsp);
        if (UNLIKELY(ret != MMS_OK)) {
            LOG_ERROR("Send request failed, ret:" << ret << ".");
        }
    } while (true);

    return ret;
}
}
}

