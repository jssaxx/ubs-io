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

#include <functional>
#include <string>
#include <vector>
#include <sched.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include "mms_comm.h"
#include "mms_client_log.h"
#include "mms_trace.h"
#include "mms_functions.h"
#include "mms_client.h"
#include "mms_monotonic.h"
#include "securec.h"
#include "mms_kv_client.h"

namespace ock {
namespace mms {

static thread_local uint16_t g_groupIndex = NumaGroupIndex::Instance()->GetGroupIndex();

static BResult FillPutValueAddr(CachePtr cache, PutItems *itemList, uint32_t itemNum)
{
    BResult result = MMS_OK;
    for (uint32_t index = 0; index < itemNum; index++) {
        if (*itemList[index].result != MMS_OK) {
            result = *itemList[index].result;
            continue;
        }

        *itemList[index].valueAddr = nullptr;
        uint64_t realLength = 0;
        GetPara para = {itemList[index].key, itemList[index].keyLen, 0, itemList[index].valueLen,
                        itemList[index].valueAddr, &realLength};
        auto ret = cache->Get(para);
        if (UNLIKELY(ret != MMS_OK)) {
            *itemList[index].result = ret;
            result = ret;
        }
    }
    return result;
}

struct SendResultContext {
    uint32_t itemIndex;
    BResult failedRet;
    bool withValue;
};

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
    MMS_TRACE_START(NET_TRACE_SYNC_CALL);
    ret = mNetEngine->SyncCall<IoCtrlRequest, BResult>(INVALID_NID, g_groupIndex, opCode, req, rsp);
    MMS_TRACE_END(NET_TRACE_SYNC_CALL, ret);
    if (LIKELY(ret == MMS_OK && rsp == MMS_OK)) {
        return MMS_OK;
    }

    if (ret == MMS_OK) {
        ret = rsp;
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

BResult MmsKvClient::HandleSendReqs(uint16_t numaId, MmsOpCode opCode, std::vector<IOCtxItem> &ctxItems,
                                    bool freeBlocks)
{
    BResult ret;
    uint64_t numaOffset;
    uint32_t ctxItemNum = static_cast<uint32_t>(ctxItems.size());
    for (uint32_t index = 0; index < ctxItemNum; index++) {
        auto &item = ctxItems[index];
        mMemMgr->Trans2Offset(MMAP_AREA_IOCTX, item.buff, numaOffset);
        IoCtrlRequest req = {{0, opCode, 0, 0, mPtVersion.load(std::memory_order_acquire)},
                             numaId,
                             numaOffset,
                             item.reqLen};
        ret = SendSingleReq(req);
        if (UNLIKELY(ret != MMS_OK)) {
            CLIENT_LOG_ERROR("Send single request failed, ret:" << ret << ", opCode:" << opCode << ".");
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

BResult MmsKvClient::MmsPut(PutItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    uint16_t numaId = mMemAllocator->GetNumaId();

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodePutRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc, mMaxMsgBuffSize);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, MMS_OP_C_PUT, ctxItems, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillPutItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillPutItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, MMS_OP_C_PUT, ctxItems, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillPutItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillPutItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            CLIENT_LOG_DEBUG("Send batch put success, total send:" << curItemIndex
                                                                   << ", current batch:" << ctxItems.size() << ".");
        } else {
            CLIENT_LOG_ERROR("Encode put request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    ret = FillPutValueAddr(mCache, itemList, itemNum);
    return (ret == MMS_OK) ? result : ret;
}

BResult MmsKvClient::MmsGet(GetItems *itemList, uint32_t itemNum)
{
    uint16_t index;
    BResult result = MMS_OK;

    for (index = 0; index < itemNum; index++) {
        uint64_t realLength = 0;
        GetPara para = {itemList[index].key, itemList[index].keyLen, itemList[index].offset, itemList[index].length,
                        itemList[index].value, &realLength};
        auto ret = mCache->Get(para);
        *itemList[index].realLength = static_cast<uint32_t>(realLength);
        *itemList[index].result = ret;
        if (LIKELY(ret == MMS_OK)) {
            continue;
        }

        CLIENT_LOG_ERROR(
            "Get cache failed, ret:" << ret << ", key:" << std::string(itemList[index].key, itemList[index].keyLen) <<
            ".");
        result = ret;
    }

    return result;
}

BResult MmsKvClient::GetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    PrefixSearchRsp rsp;
    BResult ret = SendPrefixSearchReq(prefix, rsp);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    return ReceiveSearchResult(rsp, valueInfoItems, itemNum);
}

BResult MmsKvClient::GetValuesByRange(const char *start, const char *end, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    PrefixSearchRsp rsp;
    BResult ret = SendRangeSearchReq(start, end, rsp);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    return ReceiveSearchResult(rsp, valueInfoItems, itemNum);
}

BResult MmsKvClient::BatchDeleteByRange(const char *start, const char *end)
{
    return SendRangeDeleteReq(start, end);
}

BResult MmsKvClient::SendPrefixSearchReq(const char *prefix, PrefixSearchRsp &rsp)
{
    PrefixSearchReq req = {{0, MMS_OP_C_GET_BY_PREFIX, 0, 0, 0}, {0}};
    uint16_t keyLen = strlen(prefix);
    auto ret = strncpy_s(req.prefix, MAX_KEY_SIZE, prefix, keyLen);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("string copy failed.");
        return MMS_ERR;
    }

    uint64_t startTime = Monotonic::TimeSec();
    uint16_t retryCount = 0;
    do {
        ret = mNetEngine->SyncCall<PrefixSearchReq, PrefixSearchRsp>(INVALID_NID, g_groupIndex, MMS_OP_C_GET_BY_PREFIX,
                                                                     req, rsp);
        if (LIKELY(ret == MMS_OK)) {
            return MMS_OK;
        }

        CLIENT_LOG_ERROR("Send prefix get request failed, ret:" << ret << ", retry count:" << ++retryCount << ".");

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
    } while (true);

    return ret;
}

BResult MmsKvClient::SendRangeSearchReq(const char *start, const char *end, PrefixSearchRsp &rsp)
{
    RangeSearchReq req = {{0, MMS_OP_C_GET_BY_RANGE, 0, 0, 0}, {0}, {0}};
    if (UNLIKELY(strncpy_s(req.startKey, MAX_KEY_SIZE, start, strlen(start)) != MMS_OK ||
                 strncpy_s(req.endKey, MAX_KEY_SIZE, end, strlen(end)) != MMS_OK)) {
        CLIENT_LOG_ERROR("string copy failed.");
        return MMS_ERR;
    }

    BResult ret = MMS_OK;
    uint64_t startTime = Monotonic::TimeSec();
    uint16_t retryCount = 0;
    do {
        ret = mNetEngine->SyncCall<RangeSearchReq, PrefixSearchRsp>(INVALID_NID, g_groupIndex, MMS_OP_C_GET_BY_RANGE,
                                                                    req, rsp);
        if (LIKELY(ret == MMS_OK)) {
            return MMS_OK;
        }

        CLIENT_LOG_ERROR("Send range get request failed, ret:" << ret << ", retry count:" << ++retryCount << ".");
        bool isContinue = (ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY ||
                           ret == MMS_NET_RETRY || ret == MMS_CHECK_PT_FAIL);
        if (!isContinue || Monotonic::TimeSec() - startTime >= mIoTimeOut) {
            break;
        }
        sleep(IO_RETRY_INTERAL);
    } while (true);

    return ret;
}

BResult MmsKvClient::SendRangeDeleteReq(const char *start, const char *end)
{
    std::vector<IOCtxItem> ctxItems{};
    BResult ret = EncodeRangeDeleteRequest(start, end, ctxItems, allocFunc, mMaxMsgBuffSize);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Encode range delete request failed, ret:" << ret << ".");
        FreeBlocks(ctxItems);
        return ret;
    }

    uint64_t numaOffset;
    IOCtxItem &item = ctxItems[0];
    mMemMgr->Trans2Offset(MMAP_AREA_IOCTX, item.buff, numaOffset);
    IoCtrlRequest req = {{0, MMS_OP_C_DELETE_BY_RANGE, 0, 0, mPtVersion.load(std::memory_order_acquire)},
                         mMemAllocator->GetNumaId(),
                         numaOffset,
                         item.reqLen};
    ret = SendSingleReq(req);
    FreeBlocks(ctxItems);
    return ret;
}

BResult MmsKvClient::ReceiveSearchResult(PrefixSearchRsp &rsp, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    *valueInfoItems = nullptr;
    *itemNum = 0;
    if (rsp.totalSize == 0) {
        return MMS_OK;
    }

    int32_t fds[NO_1];
    BResult ret = mNetEngine->ReceiveFds(INVALID_NID, fds, NO_1);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Receive file mem fd failed, ret:" << ret << ".");
        return ret;
    }

    int32_t mfd = fds[0];
    void *dataPtr = mmap(NULL, rsp.totalSize, PROT_READ, MAP_SHARED, mfd, 0);
    close(mfd);
    if (UNLIKELY(dataPtr == MAP_FAILED)) {
        CLIENT_LOG_ERROR("Memory map failed in client.");
        return MMS_INNER_ERR;
    }

    return FillValueInfoItems(dataPtr, rsp, valueInfoItems, itemNum);
}

BResult MmsKvClient::FillValueInfoItems(void *dataPtr, PrefixSearchRsp &rsp, ValueInfo **valueInfoItems,
                                        uint64_t *itemNum)
{
    char *memAddr = static_cast<char *>(dataPtr);
    PrefixSearchDes *des = reinterpret_cast<PrefixSearchDes *>(memAddr);
    *itemNum = des->itemNum;
    *valueInfoItems = new (std::nothrow) ValueInfo[des->itemNum];
    if (UNLIKELY(*valueInfoItems == nullptr)) {
        munmap(dataPtr, rsp.totalSize);
        return MMS_ALLOC_FAIL;
    }

    uint64_t memOffset = sizeof(PrefixSearchDes) + des->itemNum * sizeof(ValueDesInfo);
    for (uint64_t idx = 0; idx < des->itemNum; idx++) {
        (*valueInfoItems)[idx].key = memAddr + memOffset;
        memOffset += des->values[idx].keyLen;
        (*valueInfoItems)[idx].value = memAddr + memOffset;
        (*valueInfoItems)[idx].length = des->values[idx].valueLen;
        memOffset += des->values[idx].valueLen;
    }
    return MMS_OK;
}

void MmsKvClient::FreeResources(ValueInfo **valueInfoItems, uint64_t itemNum)
{
    if (valueInfoItems == nullptr || *valueInfoItems == nullptr || itemNum == 0) {
        return;
    }

    uint64_t memOffset = sizeof(PrefixSearchDes) + itemNum * sizeof(ValueDesInfo);
    char *shmAddr = (*valueInfoItems)[0].key - memOffset;
    PrefixSearchDes *des = reinterpret_cast<PrefixSearchDes *>(shmAddr);
    if (UNLIKELY(munmap(shmAddr, des->totalSize) != MMS_OK)) {
        CLIENT_LOG_ERROR("Memory unmap failed, error:" << strerror(errno) << ".");
    }
    delete[] *valueInfoItems;
    *valueInfoItems = nullptr;
}

BResult MmsKvClient::MmsUpdate(UpdateItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    uint16_t numaId = mMemAllocator->GetNumaId();

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeUpdateRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc,
                                  mMaxMsgBuffSize);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, MMS_OP_C_UPDATE, ctxItems, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillUpdateItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, MMS_OP_C_UPDATE, ctxItems, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillUpdateItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            CLIENT_LOG_DEBUG("Send batch update success, total send:" << curItemIndex
                                                                      << ", current batch:" << ctxItems.size() << ".");
        } else {
            CLIENT_LOG_ERROR("Encode update request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

BResult MmsKvClient::MmsDelete(DeleteItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    uint16_t numaId = mMemAllocator->GetNumaId();

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeDeleteRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc,
                                  mMaxMsgBuffSize);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, MMS_OP_C_DELETE, ctxItems, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillDeleteItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, false};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, MMS_OP_C_DELETE, ctxItems, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillDeleteItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, false};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            CLIENT_LOG_DEBUG("Send batch delete success, total send:" << curItemIndex
                                                                      << ", current batch:" << ctxItems.size() << ".");
        } else {
            CLIENT_LOG_ERROR("Encode delete request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

BResult MmsKvClient::MmsReplace(ReplaceItems *itemList, uint32_t itemNum)
{
    uint32_t curItemIndex = 0;
    std::vector<IOCtxItem> ctxItems{};
    BResult ret;
    BResult result = MMS_OK;
    uint16_t numaId = mMemAllocator->GetNumaId();

    while (curItemIndex < itemNum) {
        ctxItems.clear();
        ret = EncodeReplaceRequest(&itemList[curItemIndex], itemNum - curItemIndex, ctxItems, allocFunc,
                                   mMaxMsgBuffSize);
        if (LIKELY(ret == MMS_OK)) {
            ret = HandleSendReqs(numaId, MMS_OP_C_REPLACE, ctxItems, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillReplaceItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, MMS_OP_C_REPLACE, ctxItems, false);
            if (LIKELY(ret == MMS_OK)) {
                curItemIndex = FillReplaceItemResults(itemList, curItemIndex, ctxItems);
            } else {
                SendResultContext context = {curItemIndex, ret, true};
                curItemIndex = FillNoValueItemResultsAfterSend(itemList, ctxItems, context);
            }
            FreeBlocks(ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                result = ret;
            }
            CLIENT_LOG_DEBUG("Send batch replace success, total send:" << curItemIndex
                                                                       << ", current batch:" << ctxItems.size() << ".");
        } else {
            CLIENT_LOG_ERROR("Encode replace request failed, ret:" << ret << ".");
            FreeBlocks(ctxItems);
            *itemList[curItemIndex].result = ret;
            result = ret;
            curItemIndex++;
        }
    }

    return result;
}

void MmsKvClient::HandleUpdatePtVersion(uint64_t ptVersion)
{
    mPtVersion.store(ptVersion, std::memory_order_release);
    UpdateLocalPtVersion(ptVersion);
    CLIENT_LOG_INFO("Update client pt version:" << ptVersion << ".");
}

BResult MmsKvClient::UpdateClientPtVersion()
{
    BResult ret = MMS_OK;
    UpdatePtVRsp rsp;
    BasicRequest req = {{0, MMS_OP_C_UPDATE_PT_VERSION, 0, 0, 0}};
    uint16_t retryCount = 0;

    do {
        ret = mNetEngine->SyncCall<BasicRequest, UpdatePtVRsp>(INVALID_NID, g_groupIndex, MMS_OP_C_UPDATE_PT_VERSION,
                                                               req, rsp);
        if (LIKELY(ret == MMS_OK)) {
            HandleUpdatePtVersion(rsp.ptVersion);
            break;
        }

        CLIENT_LOG_ERROR("Send request failed, ret:" << ret << ", retry count:" << ++retryCount << ".");
        if (retryCount > RETRY_COUNT) {
            CLIENT_LOG_ERROR("Send request failed after " << retryCount << " retries, exiting.");
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

        ret = mNetEngine->SyncCall<IoCtrlRequest, BResult>(INVALID_NID, g_groupIndex, opCode, req, rsp);
        if (UNLIKELY(ret != MMS_OK)) {
            CLIENT_LOG_ERROR("Send request failed, ret:" << ret << ".");
        }
    } while (true);

    return ret;
}
}
}
