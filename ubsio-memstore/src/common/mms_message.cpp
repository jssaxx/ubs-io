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

#include "mms_message.h"
#include "securec.h"
#include "mms_crc_util.h"
#include "mms_trace.h"

namespace ock {
namespace mms {

bool gCrcSwitch = false;

std::atomic<uint64_t> gPtVersion;

void UpdateCrcSwitch(bool crcSwitch)
{
    gCrcSwitch = crcSwitch;
}

void UpdateLocalPtVersion(uint64_t ptVersion)
{
    gPtVersion.store(ptVersion, std::memory_order_release);
}

struct EncodeBufferState {
    uint16_t numaId = 0;
    uintptr_t buff = 0;
    IoDataRequest *req = nullptr;
    uint64_t offset = IO_DATA_REQUEST_LEN;
    uint32_t curItemNum = 0;
};

static void FinishRequestBlock(EncodeBufferState &state, std::vector<IOCtxItem> &ctxItems)
{
    state.req->num = state.curItemNum;
    if (gCrcSwitch) {
        static uint32_t skip = sizeof(state.req->head) + sizeof(state.req->seqNo) + sizeof(state.req->negoSeqNo) +
            sizeof(state.req->crc);
        state.req->crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(state.buff + skip), state.offset - skip);
    }
    ctxItems.emplace_back(state.buff, state.offset);
}

static BResult AllocRequestBlock(const AllocFunc &allocFunc, uint32_t ioCtxBuffLen, EncodeBufferState &state)
{
    auto ret = allocFunc(ioCtxBuffLen, state.numaId, state.buff);
    if (UNLIKELY(ret != MMS_OK)) {
        return MMS_ALLOC_FAIL;
    }

    state.req = reinterpret_cast<IoDataRequest *>(state.buff);
    state.req->crc = 0;
    state.offset = IO_DATA_REQUEST_LEN;
    state.curItemNum = 0;
    return MMS_OK;
}

static BResult CopyKey(uintptr_t buff, uint64_t offset, uint64_t keyLen, uint32_t ioCtxBuffLen, const char *key)
{
    auto *dst = reinterpret_cast<char *>(buff + offset);
    auto ret = memcpy_s(dst, ioCtxBuffLen - offset, key, keyLen);
    if (UNLIKELY(ret != EOK)) {
        return MMS_ERR;
    }
    dst[keyLen] = '\0';
    return MMS_OK;
}

BResult EncodeSinglePutKV(uintptr_t buff, uint64_t &offset, uint64_t keyLen, uint32_t ioCtxBuffLen,
                          const PutItems &item)
{
    auto desc = reinterpret_cast<IoLocDesc *>(buff + offset);
    desc->keyLen = keyLen + NO_1;
    desc->valueLen = item.valueLen;
    desc->offset = item.isNotify;
    desc->version = gPtVersion.load(std::memory_order_acquire);
    desc->result = MMS_MAX;
    desc->reserved = 0;
    desc->valueAddr = 0;
    offset += sizeof(IoLocDesc);

    auto ret = CopyKey(buff, offset, keyLen, ioCtxBuffLen, item.key);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }
    offset += (keyLen + NO_1);

    ret = memcpy_s(reinterpret_cast<void *>(buff + offset), ioCtxBuffLen - offset, item.value, item.valueLen);
    if (UNLIKELY(ret != EOK)) {
        return MMS_ERR;
    }
    offset += item.valueLen;

    return MMS_OK;
}

// 内存布局:[IoDataRequest][IoLocDesc_1][key_1][value_1][IoLocDesc_2][key_2][value_2]...[IoLocDesc_n][key_n][value_n]
BResult EncodePutRequest(PutItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                         const AllocFunc &allocFunc, uint32_t ioCtxBuffLen)
{
    EncodeBufferState state;
    auto ret = AllocRequestBlock(allocFunc, ioCtxBuffLen, state);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    for (uint32_t index = 0; index < itemNum; index++) {
        uint64_t keyLen = itemList[index].keyLen;
        if (UNLIKELY((keyLen + NO_1 + itemList[index].valueLen) > (ioCtxBuffLen - IOCTX_HEADER_LEN))) {
            ctxItems.emplace_back(state.buff, state.offset);
            return MMS_INVALID_PARAM;
        }

        uint64_t needLen = state.offset + sizeof(IoLocDesc) + keyLen + NO_1 + itemList[index].valueLen;
        if (needLen > ioCtxBuffLen) {  // 超出一个块了，重新申请一个块
            FinishRequestBlock(state, ctxItems);
            ret = AllocRequestBlock(allocFunc, ioCtxBuffLen, state);
            if (UNLIKELY(ret != MMS_OK)) {
                return ret;
            }
        }

        ret = EncodeSinglePutKV(state.buff, state.offset, keyLen, ioCtxBuffLen, itemList[index]);
        if (UNLIKELY(ret != MMS_OK)) {
            ctxItems.emplace_back(state.buff, state.offset);
            return ret;
        }
        state.curItemNum++;
    }

    FinishRequestBlock(state, ctxItems);
    return MMS_OK;
}

BResult DeCodePutRequest(std::vector<DecodePutItem> &itemList, uint32_t &itemNum, uint64_t buff, uint64_t realLen)
{
    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(buff);
    itemNum = req->num;
    static uint32_t maxSize = static_cast<uint32_t>(itemList.size());
    if (itemNum > maxSize) {
        return MMS_INVALID_PARAM;
    }

    if (gCrcSwitch) {
        static uint32_t skip = sizeof(req->head) + sizeof(req->seqNo) + sizeof(req->negoSeqNo) + sizeof(req->crc);
        uint32_t crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(buff + skip), realLen - skip);
        if (req->crc != crc) {
            return MMS_CRC_ERR;
        }
    }

    uint64_t offset = sizeof(IoDataRequest);
    for (uint32_t index = 0; index < itemNum; index++) {
        auto desc = reinterpret_cast<IoLocDesc *>(buff + offset);
        offset += sizeof(IoLocDesc);
        itemList[index].key = reinterpret_cast<char *>(buff + offset);
        itemList[index].keyLen = static_cast<uint16_t>(desc->keyLen - NO_1);
        offset += desc->keyLen;
        itemList[index].value = reinterpret_cast<char *>(buff + offset);
        itemList[index].valueLen = desc->valueLen;
        itemList[index].isNotify = static_cast<uint16_t>(desc->offset);
        itemList[index].version = desc->version;
        itemList[index].result = &desc->result;
        itemList[index].valueAddr = &desc->valueAddr;
        offset += desc->valueLen;
        if (offset > realLen) {
            return MMS_ERR;
        }
    }

    return MMS_OK;
}

BResult EncodeSingleUpdateKV(uintptr_t buff, uint64_t &offset, uint64_t keyLen, uint32_t ioCtxBuffLen,
                             const UpdateItems &item)
{
    auto desc = reinterpret_cast<IoLocDesc *>(buff + offset);
    desc->keyLen = keyLen + NO_1;
    desc->valueLen = item.valueLen;
    desc->offset = item.offset;
    desc->version = gPtVersion.load(std::memory_order_acquire);
    desc->result = MMS_MAX;
    desc->reserved = 0;
    offset += sizeof(IoLocDesc);

    auto ret = CopyKey(buff, offset, keyLen, ioCtxBuffLen, item.key);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }
    offset += (keyLen + NO_1);

    ret = memcpy_s(reinterpret_cast<void *>(buff + offset), ioCtxBuffLen - offset, item.value, item.valueLen);
    if (UNLIKELY(ret != EOK)) {
        return MMS_ERR;
    }
    offset += item.valueLen;

    return MMS_OK;
}

// 内存布局:[IoDataRequest][IoLocDesc_1][key_1][value_1][IoLocDesc_2][key_2][value_2]...[IoLocDesc_n][key_n][value_n]
BResult EncodeUpdateRequest(UpdateItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                            const AllocFunc &allocFunc, uint32_t ioCtxBuffLen)
{
    EncodeBufferState state;
    auto ret = AllocRequestBlock(allocFunc, ioCtxBuffLen, state);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    for (uint32_t index = 0; index < itemNum; index++) {
        uint64_t keyLen = itemList[index].keyLen;
        if (UNLIKELY((keyLen + NO_1 + itemList[index].valueLen) > (ioCtxBuffLen - IOCTX_HEADER_LEN))) {
            ctxItems.emplace_back(state.buff, state.offset);
            return MMS_INVALID_PARAM;
        }

        uint64_t needLen = state.offset + sizeof(IoLocDesc) + keyLen + NO_1 + itemList[index].valueLen;
        if (needLen > ioCtxBuffLen) {  // 超出一个块了，重新申请一个块
            FinishRequestBlock(state, ctxItems);
            ret = AllocRequestBlock(allocFunc, ioCtxBuffLen, state);
            if (UNLIKELY(ret != MMS_OK)) {
                return ret;
            }
        }

        ret = EncodeSingleUpdateKV(state.buff, state.offset, keyLen, ioCtxBuffLen, itemList[index]);
        if (UNLIKELY(ret != MMS_OK)) {
            ctxItems.emplace_back(state.buff, state.offset);
            return ret;
        }
        state.curItemNum++;
    }

    FinishRequestBlock(state, ctxItems);
    return MMS_OK;
}

BResult DeCodeUpdateRequest(std::vector<DecodeUpdateItem> &itemList, uint32_t &itemNum, uint64_t buff,
                            uint64_t realLen)
{
    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(buff);
    itemNum = req->num;
    static uint32_t maxSize = static_cast<uint32_t>(itemList.size());
    if (itemNum > maxSize) {
        return MMS_INVALID_PARAM;
    }

    if (gCrcSwitch) {
        static uint32_t skip = sizeof(req->head) + sizeof(req->seqNo) + sizeof(req->negoSeqNo) + sizeof(req->crc);
        uint32_t crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(buff + skip), realLen - skip);
        if (req->crc != crc) {
            return MMS_CRC_ERR;
        }
    }

    uint64_t offset = sizeof(IoDataRequest);
    for (uint32_t index = 0; index < itemNum; index++) {
        auto desc = reinterpret_cast<IoLocDesc *>(buff + offset);
        offset += sizeof(IoLocDesc);
        itemList[index].key = reinterpret_cast<char *>(buff + offset);
        itemList[index].keyLen = static_cast<uint16_t>(desc->keyLen - NO_1);
        offset += desc->keyLen;
        itemList[index].value = reinterpret_cast<char *>(buff + offset);
        itemList[index].offset = desc->offset;
        itemList[index].valueLen = desc->valueLen;
        itemList[index].version = desc->version;
        itemList[index].result = &desc->result;
        offset += desc->valueLen;
        if (offset > realLen) {
            return MMS_ERR;
        }
    }

    return MMS_OK;
}

BResult EncodeSingleDeleteKV(uintptr_t buff, uint64_t &offset, uint64_t keyLen, uint32_t ioCtxBuffLen,
                             const DeleteItems &item)
{
    auto desc = reinterpret_cast<IoLocDesc *>(buff + offset);
    desc->keyLen = keyLen + NO_1;
    desc->valueLen = 0;
    desc->offset = item.isNotify;
    desc->version = gPtVersion.load(std::memory_order_acquire);
    desc->result = MMS_MAX;
    desc->reserved = 0;
    offset += sizeof(IoLocDesc);

    auto ret = CopyKey(buff, offset, keyLen, ioCtxBuffLen, item.key);
    if (UNLIKELY(ret != MMS_OK)) {
        return ret;
    }

    offset += (keyLen + NO_1);
    return MMS_OK;
}

// 内存布局:[IoDataRequest][IoLocDesc_1][key_1][IoLocDesc_2][key_2]...[IoLocDesc_n][key_n]
BResult EncodeDeleteRequest(DeleteItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                            const AllocFunc &allocFunc, uint32_t ioCtxBuffLen)
{
    uint16_t numaId;
    uintptr_t buff;
    uint64_t ctxOffset = IO_DATA_REQUEST_LEN;

    auto allocRet = allocFunc(ioCtxBuffLen, numaId, buff);
    if (UNLIKELY(allocRet != MMS_OK)) {
        return MMS_ALLOC_FAIL;
    }

    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(buff);
    req->crc = 0;

    uint32_t curItemNum = 0;
    uint64_t offset = ctxOffset;

    for (uint32_t index = 0; index < itemNum; index++) {
        uint64_t keyLen = itemList[index].keyLen;
        uint64_t needLen = offset + sizeof(IoLocDesc) + keyLen + NO_1;
        if (needLen > ioCtxBuffLen) {  // 超出一个块了
            req->num = curItemNum;
            if (gCrcSwitch) {
                static uint32_t skip =
                    sizeof(req->head) + sizeof(req->seqNo) + sizeof(req->negoSeqNo) + sizeof(req->crc);
                req->crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(buff + skip), offset - skip);
            }
            ctxItems.emplace_back(buff, offset);

            // 申请一个新的块
            auto ret = allocFunc(ioCtxBuffLen, numaId, buff);
            if (UNLIKELY(ret != MMS_OK)) {
                return MMS_ALLOC_FAIL;
            }
            req = reinterpret_cast<IoDataRequest *>(buff);
            req->crc = 0;
            offset = ctxOffset;
            curItemNum = 0;
        }

        auto ret = EncodeSingleDeleteKV(buff, offset, keyLen, ioCtxBuffLen, itemList[index]);
        if (UNLIKELY(ret != MMS_OK)) {
            ctxItems.emplace_back(buff, offset);
            return ret;
        }
        curItemNum++;
    }

    req->num = curItemNum;
    if (gCrcSwitch) {
        static uint32_t skip = sizeof(req->head) + sizeof(req->seqNo) + sizeof(req->negoSeqNo) + sizeof(req->crc);
        req->crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(buff + skip), offset - skip);
    }

    ctxItems.emplace_back(buff, offset);
    return MMS_OK;
}

BResult DeCodeDeleteRequest(std::vector<DecodeDeleteItem> &itemList, uint32_t &itemNum, uint64_t buff,
                            uint64_t realLen)
{
    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(buff);
    itemNum = req->num;
    static uint32_t maxSize = static_cast<uint32_t>(itemList.size());
    if (itemNum > maxSize) {
        return MMS_INVALID_PARAM;
    }

    if (gCrcSwitch) {
        static uint32_t skip = sizeof(req->head) + sizeof(req->seqNo) + sizeof(req->negoSeqNo) + sizeof(req->crc);
        uint32_t crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(buff + skip), realLen - skip);
        if (req->crc != crc) {
            return MMS_CRC_ERR;
        }
    }

    uint64_t offset = sizeof(IoDataRequest);
    for (uint32_t index = 0; index < itemNum; index++) {
        auto desc = reinterpret_cast<IoLocDesc *>(buff + offset);
        offset += sizeof(IoLocDesc);
        itemList[index].key = reinterpret_cast<char *>(buff + offset);
        itemList[index].keyLen = static_cast<uint16_t>(desc->keyLen - NO_1);
        itemList[index].isNotify = static_cast<uint16_t>(desc->offset);
        itemList[index].version = desc->version;
        itemList[index].result = &desc->result;
        offset += desc->keyLen;
        if (offset > realLen) {
            return MMS_ERR;
        }
    }
    return MMS_OK;
}

BResult EncodeReplaceRequest(ReplaceItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                             const AllocFunc &allocFunc, uint32_t ioCtxBuffLen)
{
    return EncodeUpdateRequest(itemList, itemNum, ctxItems, allocFunc, ioCtxBuffLen);
}

BResult DeCodeReplaceRequest(std::vector<DecodeUpdateItem> &itemList, uint32_t &itemNum, uint64_t buff,
                             uint64_t realLen)
{
    return DeCodeUpdateRequest(itemList, itemNum, buff, realLen);
}

static uint32_t FillResults(uint32_t itemIndex, const std::vector<IOCtxItem> &ctxItems,
                            const std::function<void(uint32_t, const IoLocDesc &)> &fillFunc, bool withValue)
{
    uint32_t curIndex = itemIndex;
    for (const auto &ctxItem : ctxItems) {
        auto *req = reinterpret_cast<IoDataRequest *>(ctxItem.buff);
        uint64_t offset = sizeof(IoDataRequest);
        for (uint32_t index = 0; index < req->num; index++) {
            auto *desc = reinterpret_cast<IoLocDesc *>(ctxItem.buff + offset);
            offset += sizeof(IoLocDesc) + desc->keyLen;
            if (withValue) {
                offset += desc->valueLen;
            }
            fillFunc(curIndex, *desc);
            curIndex++;
        }
    }
    return curIndex;
}

uint32_t FillPutItemResults(PutItems *itemList, uint32_t itemIndex, const std::vector<IOCtxItem> &ctxItems)
{
    return FillResults(itemIndex, ctxItems, [itemList](uint32_t index, const IoLocDesc &desc) {
        if (itemList[index].result != nullptr) {
            *itemList[index].result = desc.result;
        }
        if (itemList[index].valueAddr != nullptr) {
            *itemList[index].valueAddr = reinterpret_cast<char *>(desc.valueAddr);
        }
    }, true);
}

uint32_t FillUpdateItemResults(UpdateItems *itemList, uint32_t itemIndex, const std::vector<IOCtxItem> &ctxItems)
{
    return FillResults(itemIndex, ctxItems, [itemList](uint32_t index, const IoLocDesc &desc) {
        if (itemList[index].result != nullptr) {
            *itemList[index].result = desc.result;
        }
    }, true);
}

uint32_t FillDeleteItemResults(DeleteItems *itemList, uint32_t itemIndex, const std::vector<IOCtxItem> &ctxItems)
{
    return FillResults(itemIndex, ctxItems, [itemList](uint32_t index, const IoLocDesc &desc) {
        if (itemList[index].result != nullptr) {
            *itemList[index].result = desc.result;
        }
    }, false);
}

uint32_t FillReplaceItemResults(ReplaceItems *itemList, uint32_t itemIndex, const std::vector<IOCtxItem> &ctxItems)
{
    return FillUpdateItemResults(itemList, itemIndex, ctxItems);
}

BResult EncodeRangeDeleteRequest(const char *start, const char *end, std::vector<IOCtxItem> &ctxItems,
                                 const AllocFunc &allocFunc, uint32_t ioCtxBuffLen)
{
    uint16_t numaId;
    uintptr_t buff;
    auto allocRet = allocFunc(ioCtxBuffLen, numaId, buff);
    if (UNLIKELY(allocRet != MMS_OK)) {
        return MMS_ALLOC_FAIL;
    }

    RangeDeleteDataRequest *req = reinterpret_cast<RangeDeleteDataRequest *>(buff);
    *req = {};
    if (UNLIKELY(strncpy_s(req->startKey, MAX_KEY_SIZE, start, strlen(start)) != MMS_OK ||
                 strncpy_s(req->endKey, MAX_KEY_SIZE, end, strlen(end)) != MMS_OK)) {
        ctxItems.emplace_back(buff, sizeof(RangeDeleteDataRequest));
        return MMS_ERR;
    }

    if (gCrcSwitch) {
        static uint32_t skip = sizeof(req->dataReq.head) + sizeof(req->dataReq.seqNo) +
                               sizeof(req->dataReq.negoSeqNo) + sizeof(req->dataReq.crc);
        req->dataReq.crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(buff + skip),
                                             sizeof(RangeDeleteDataRequest) - skip);
    }

    ctxItems.emplace_back(buff, sizeof(RangeDeleteDataRequest));
    return MMS_OK;
}

BResult DeCodeRangeDeleteRequest(const char *&start, const char *&end, uint64_t buff, uint64_t realLen)
{
    RangeDeleteDataRequest *req = reinterpret_cast<RangeDeleteDataRequest *>(buff);
    if (gCrcSwitch) {
        static uint32_t skip = sizeof(req->dataReq.head) + sizeof(req->dataReq.seqNo) +
                               sizeof(req->dataReq.negoSeqNo) + sizeof(req->dataReq.crc);
        uint32_t crc = MmsCrcUtil::Crc32(reinterpret_cast<void *>(buff + skip), realLen - skip);
        if (req->dataReq.crc != crc) {
            return MMS_CRC_ERR;
        }
    }

    start = req->startKey;
    end = req->endKey;
    return MMS_OK;
}

}
}
