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

#ifndef MMS_MESSAGE_H
#define MMS_MESSAGE_H

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include <atomic>
#include <vector>
#include <functional>
#include "mms_c.h"
#include "mms_err.h"
#include "mms_types.h"

namespace ock {
namespace mms {
enum MmsOpCode : uint16_t {
    MMS_OP_C_BASIC = 0,
    MMS_OP_C_SERVICEABLE,
    MMS_OP_C_PUT,
    MMS_OP_C_UPDATE,
    MMS_OP_C_DELETE,
    MMS_OP_C_REPLACE,
    MMS_OP_C_UPDATE_PT_VERSION,
    MMS_OP_C_CRB_START_CATCH_UP,
    MMS_OP_C_GET_BY_PREFIX,
    MMS_OP_C_GET_BY_RANGE,
    MMS_OP_C_DELETE_BY_RANGE,
    MMS_OP_S_CRB_START_RECOVER,
    MMS_OP_S_CRB_RECEIVE_DATA,
    MMS_OP_S_PUT,
    MMS_OP_S_MULTI_PUT,
    MMS_OP_S_UPDATE,
    MMS_OP_S_MULTI_UPDATE,
    MMS_OP_S_DELETE,
    MMS_OP_S_MULTI_DELETE,
    MMS_OP_S_REPLACE,
    MMS_OP_S_MULTI_REPLACE,
    MMS_OP_S_GET_SEQNO_LIST,
    MMS_OP_S_GET_SEQNO_DATA,
    MMS_OP_S_DELETE_BY_RANGE,
    MMS_OP_S_MULTI_DELETE_BY_RANGE,
    MMS_OP_BUTT
};

typedef struct {
    uint16_t nodeId;
    uint16_t opcode;
    uint16_t groupIndex;
    uint16_t ptId;
    uint64_t ptv;
} ReqHead;

typedef struct {
    ReqHead head;
} ServiceRequest;

typedef struct {
    bool serviceable;
} ServiceResponse;

typedef struct {
    ReqHead head;
} BasicRequest;

typedef struct {
    uint64_t ptVersion;
} UpdatePtVRsp;

typedef struct {
    int32_t serverPid;
    uint16_t memNum;
    uint16_t memNumaId[MAX_NUMAS_NUM];
    uint64_t memSize[MAX_NUMAS_NUM];
    uint32_t ioTimeOut;
    uint32_t netTimeOut;
    int32_t logLevel;
    uint32_t maxMsgBuffSize;
    uint16_t minBlockSize;
    uint16_t maxBlockSize;
    uint8_t minBlockSizeRate;
    bool traceSwitch;
    bool enableCrc;
} BasicResponse;

typedef struct {
    ReqHead head;
    uint64_t userId;
    uint64_t ioNumaId : 16;
    uint64_t ioNumaOffset : 48;
    uint64_t ioLength;
} IoCtrlRequest;

typedef struct {
    uint64_t keyLen : 16;
    uint64_t valueLen : 24;
    uint64_t offset : 24;
    uint64_t version;
} IoLocDesc;

typedef struct {
    ReqHead head;
    uint64_t seqNo = 0;
    uint64_t negoSeqNo = 0;
    uint32_t crc;
    uint32_t num;
} IoDataRequest;

typedef struct {
    ReqHead head;
} GetSeqListRequest;

typedef struct {
    uint64_t seqList[SEQ_QUEUE_LEN];
    uint32_t seqNum;
} GetSeqListResponse;

typedef struct {
    ReqHead head;
    uint64_t seqNo;
} GetSeqDataRequest;

typedef struct {
    ReqHead head;
} CrbStartRequest;

static constexpr uint16_t IOCTX_HEADER_LEN = sizeof(IoDataRequest) + sizeof(IoLocDesc);
static constexpr uint16_t IO_DATA_REQUEST_LEN = sizeof(IoDataRequest);
static constexpr uint16_t IO_DESCRIPTION_LEN  = sizeof(IoLocDesc);

struct KvCbCtx {
    std::atomic<uint16_t> quota;
    std::atomic<int32_t> result;

    KvCbCtx() = default;
    KvCbCtx(uint16_t q, int32_t r) : quota(q), result(r) {}
};

typedef struct {
    ReqHead head;
    char prefix[MAX_KEY_SIZE];
} PrefixSearchReq;

typedef struct {
    uint64_t keyLen : 16;
    uint64_t valueLen : 48;
} ValueDesInfo;

typedef struct {
    uint64_t itemNum;
    uint64_t totalSize;
    ValueDesInfo values[0];
} PrefixSearchDes;

typedef struct {
    uint64_t totalSize;
} PrefixSearchRsp;

typedef struct {
    ReqHead head;
    char startKey[MAX_KEY_SIZE];
    char endKey[MAX_KEY_SIZE];
} RangeSearchReq;

typedef struct {
    IoDataRequest dataReq;
    char startKey[MAX_KEY_SIZE];
    char endKey[MAX_KEY_SIZE];
} RangeDeleteDataRequest;

void UpdateCrcSwitch(bool crcSwitch);
void UpdateLocalPtVersion(uint64_t ptVersion);

using AllocFunc = std::function<BResult(uint64_t, uint16_t &, uintptr_t &)>;

struct IOCtxItem {
    uint64_t buff;
    uint64_t reqLen;

    IOCtxItem(uint64_t buff, uint64_t reqLen) : buff(buff), reqLen(reqLen){};
};

BResult EncodePutRequest(PutItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                         const AllocFunc &allocFunc, uint32_t ioCtxBuffLen);
BResult DeCodePutRequest(std::vector<PutItems> &itemList, uint32_t &itemNum, uint64_t buff, uint64_t realLen);

BResult EncodeUpdateRequest(UpdateItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                            const AllocFunc &allocFunc, uint32_t ioCtxBuffLen);
BResult DeCodeUpdateRequest(std::vector<UpdateItems> &itemList, uint32_t &itemNum, uint64_t buff, uint64_t realLen);

BResult EncodeDeleteRequest(DeleteItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                            const AllocFunc &allocFunc, uint32_t ioCtxBuffLen);
BResult DeCodeDeleteRequest(std::vector<DeleteItems> &itemList, uint32_t &itemNum, uint64_t buff, uint64_t realLen);

BResult EncodeReplaceRequest(ReplaceItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                             const AllocFunc &allocFunc, uint32_t ioCtxBuffLen);
BResult DeCodeReplaceRequest(std::vector<ReplaceItems> &itemList, uint32_t &itemNum, uint64_t buff, uint64_t realLen);

BResult EncodeRangeDeleteRequest(const char *start, const char *end, std::vector<IOCtxItem> &ctxItems,
                                 const AllocFunc &allocFunc, uint32_t ioCtxBuffLen);
BResult DeCodeRangeDeleteRequest(const char *&start, const char *&end, uint64_t buff, uint64_t realLen);
}
}
#endif // MMS_MESSAGE_H

