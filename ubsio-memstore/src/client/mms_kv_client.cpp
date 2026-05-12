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
            CLIENT_LOG_ERROR("Send single request failed, ret:" << ret << ", opCode:" << opCode << ".");
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
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_PUT, ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            curItemIndex += ctxItems.size();
            CLIENT_LOG_DEBUG("Send batch put success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                            << ".");
            continue;
        } else {
            CLIENT_LOG_ERROR("Encode put request failed, ret:" << ret << ".");
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
        ret = mNetEngine->SyncCall<PrefixSearchReq, PrefixSearchRsp>(INVALID_NID, 0, MMS_OP_C_GET_BY_PREFIX, req, rsp);
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
        ret = mNetEngine->SyncCall<RangeSearchReq, PrefixSearchRsp>(INVALID_NID, 0, MMS_OP_C_GET_BY_RANGE, req, rsp);
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
                         0,
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
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_UPDATE, ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            curItemIndex += ctxItems.size();
            CLIENT_LOG_DEBUG("Send batch update success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                               << ".");
            continue;
        } else {
            CLIENT_LOG_ERROR("Encode update request failed, ret:" << ret << ".");
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
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_DELETE, ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            curItemIndex += ctxItems.size();
            CLIENT_LOG_DEBUG("Send batch delete success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                               << ".");
            continue;
        } else {
            CLIENT_LOG_ERROR("Encode delete request failed, ret:" << ret << ".");
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
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            return MMS_OK;
        } else if (ret == MMS_ALLOC_FAIL && !ctxItems.empty()) {
            ret = HandleSendReqs(numaId, userId, MMS_OP_C_REPLACE, ctxItems);
            if (UNLIKELY(ret != MMS_OK)) {
                CLIENT_LOG_ERROR("Send reqs failed, ret:" << ret << ".");
                return ret;
            }
            curItemIndex += ctxItems.size();
            CLIENT_LOG_DEBUG("Send batch replace success, total send:" << curItemIndex << ", current batch:" << ctxItems.size()
                                                                << ".");
            continue;
        } else {
            CLIENT_LOG_ERROR("Encode replace request failed, ret:" << ret << ".");
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
    CLIENT_LOG_INFO("Update client pt version:" << ptVersion << ".");
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

        ret = mNetEngine->SyncCall<IoCtrlRequest, BResult>(INVALID_NID, 0, opCode, req, rsp);
        if (UNLIKELY(ret != MMS_OK)) {
            CLIENT_LOG_ERROR("Send request failed, ret:" << ret << ".");
        }
    } while (true);

    return ret;
}
}
}

