/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
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

BResult EncodeSinglePutKV(uintptr_t buff, uint64_t &offset, uint64_t keyLen, uint32_t ioCtxBuffLen,
                          const PutItems &item)
{
    auto desc = reinterpret_cast<IoLocDesc *>(buff + offset);
    desc->keyLen = keyLen + NO_1;
    desc->valueLen = item.length;
    desc->version = gPtVersion.load(std::memory_order_acquire);
    offset += sizeof(IoLocDesc);

    auto ret = strncpy_s(reinterpret_cast<char *>(buff + offset), ioCtxBuffLen - offset, item.key, keyLen);
    if (ret != 0) {
        return MMS_ERR;
    }
    offset += (keyLen + NO_1);

    ret = memcpy_s(reinterpret_cast<void *>(buff + offset), ioCtxBuffLen - offset, item.value, item.length);
    if (ret != 0) {
        return MMS_ERR;
    }
    offset += item.length;

    return MMS_OK;
}

BResult EncodeSingleDeleteKV(uintptr_t buff, uint64_t &offset, uint64_t keyLen, uint32_t ioCtxBuffLen,
                             const DeleteItems &item)
{
    auto desc = reinterpret_cast<IoLocDesc *>(buff + offset);
    desc->keyLen = keyLen + NO_1;
    desc->version = gPtVersion.load(std::memory_order_acquire);
    offset += sizeof(IoLocDesc);

    auto ret = strncpy_s(reinterpret_cast<char *>(buff + offset), ioCtxBuffLen - offset, item.key, keyLen);
    if (ret != 0) {
        return MMS_ERR;
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
        uint64_t keyLen = strlen(itemList[index].key);
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

BResult DeCodeDeleteRequest(std::vector<DeleteItems> &itemList, uint32_t &itemNum, uint64_t buff, uint64_t realLen)
{
    IoDataRequest *req = reinterpret_cast<IoDataRequest *>(buff);
    itemNum = req->num;

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
        itemList[index].version = desc->version;
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

BResult DeCodeReplaceRequest(std::vector<ReplaceItems> &itemList, uint32_t &itemNum, uint64_t buff, uint64_t realLen)
{
    return DeCodeUpdateRequest(itemList, itemNum, buff, realLen);
}
}
}

