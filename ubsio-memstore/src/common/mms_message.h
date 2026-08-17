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

#include <semaphore.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>
#include <functional>
#include <vector>
#include "mms_c.h"
#include "mms_err.h"
#include "mms_types.h"

namespace ock {
namespace mms {
enum MmsOpCode : uint16_t
{
    MMS_OP_C_BASIC = 0,
    MMS_OP_C_SERVICEABLE,
    MMS_OP_C_PUT,
    MMS_OP_C_UPDATE,
    MMS_OP_C_DELETE,
    MMS_OP_C_REPLACE,
    MMS_OP_C_UPDATE_PT_VERSION,
    MMS_OP_C_CRB_START_CATCH_UP,
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
    MMS_OP_C_GET,
    MMS_OP_C_GET_ROUTE_VIEW,
    MMS_OP_S_PUT_ONESIDE,
    MMS_OP_S_UPDATE_ONESIDE,
    MMS_OP_S_REPLACE_ONESIDE,
    MMS_OP_C_BATCH_GET
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

constexpr uint16_t MMS_ROUTE_MAX_PT_NUM = NO_1024;

typedef struct {
    uint16_t nodeId;
    uint16_t status;
    uint16_t port;
    uint16_t reserved;
    char ip[IP_SIZE];
} RouteViewNodeInfo;

typedef struct {
    uint16_t nodeId;
    uint16_t state;
} RouteViewPtCopy;

typedef struct {
    uint64_t version;
    uint16_t ptId;
    uint16_t state;
    uint16_t masterNodeId;
    uint16_t copyNum;
    RouteViewPtCopy copys[MAX_NODES_NUM];
} RouteViewPtInfo;

typedef struct {
    uint16_t localNid;
    uint16_t nodeNum;
    uint16_t ptNum;
    uint16_t replicaNum;
    uint16_t rpcProtocol;
    uint16_t rpcConnCount;
    uint16_t rpcGroupNum;
    uint16_t reserved;
    uint64_t ptVersion;
    RouteViewNodeInfo nodes[MAX_NODES_NUM];
    RouteViewPtInfo pts[MMS_ROUTE_MAX_PT_NUM];
} RouteViewResponse;

constexpr uint32_t MMS_IOCTX_PROTOCOL_VERSION = NO_2;

typedef struct {
    int32_t serverPid;
    uint32_t ioCtxProtocolVersion;
    uint64_t clientGeneration;
    uint16_t memNum;
    uint16_t memNumaId[MAX_NUMAS_NUM];
    uint64_t memSize[MAX_NUMAS_NUM];
    uint64_t ioCtxNumaSize[MAX_NUMAS_NUM];
    uint32_t ioTimeOut;
    uint32_t netTimeOut;
    int32_t logLevel;
    uint32_t maxMsgBuffSize;
    uint32_t valueBlockSize;
    bool traceSwitch;
    bool enableCrc;
    bool keyRouteEnabled;
    bool dataChangeCallbackSwitch;
} BasicResponse;

typedef struct {
    ReqHead head;
    uint64_t clientGeneration;
    uint64_t ioNumaId : 16;
    uint64_t ioNumaOffset : 48;
    uint64_t ioLength;
} IoCtrlRequest;

constexpr uint32_t MMS_TWOSIDE_IO_THRESHOLD = IO_SIZE_64K - IO_SIZE_4K;
constexpr uint32_t MMS_MAX_VALUE_SIZE = MAX_VALUE_SIZE;
constexpr uint32_t MMS_ONESIDE_STAGING_SIZE = MMS_MAX_VALUE_SIZE + IO_SIZE_64K;
constexpr uint32_t MMS_GET_FLAG_ONESIDE = 0x1;
constexpr uint32_t MMS_GET_FLAG_PROXY_BUFFER = 0x2;
constexpr uint32_t MMS_GET_FLAG_PROXY_FORWARDED = 0x4;
constexpr uint32_t MMS_GET_FLAG_ROUTE_FORWARDED = 0x10;
constexpr uint16_t MMS_BATCH_GET_FLAG_FORWARDED = 0x1;

constexpr uint32_t MMS_MEMORY_KEY_RAIL_NUM = 4;
constexpr uint32_t MMS_MEMORY_KEY_EID_LEN = 16;

typedef struct {
    uint64_t keys[MMS_MEMORY_KEY_RAIL_NUM];
    uint64_t tokens[MMS_MEMORY_KEY_RAIL_NUM];
    uint8_t eid[MMS_MEMORY_KEY_EID_LEN];
} MmsMemoryKey;

typedef struct {
    ReqHead head;
    uint64_t clientGeneration;
    uint64_t offset;
    uint64_t length;
    uint64_t valueAddr;
    MmsMemoryKey valueKey;
    uint32_t flags;
    uint32_t reserved;
    char key[MAX_KEY_SIZE];
} GetValueRequest;

typedef struct {
    int32_t result;
    uint32_t reserved;
    uint64_t realLength;
    char value[0];
} GetValueResponse;

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint16_t keyLen;
    uint16_t reserved;
    char key[MAX_KEY_SIZE];
} BatchGetItemRequest;

typedef struct {
    ReqHead head;
    uint64_t clientGeneration;
    uint64_t responseOffset;
    uint64_t valueAddr;
    MmsMemoryKey valueKey;
    uint32_t responseCapacity;
    uint32_t itemNum;
    uint16_t targetNid;
    uint16_t flags;
    uint32_t reserved;
    BatchGetItemRequest items[0];
} BatchGetRequest;

typedef struct {
    int32_t result;
    uint32_t realLength;
    uint32_t valueOffset;
    uint32_t reserved;
} BatchGetItemResponse;

typedef struct {
    uint32_t itemNum;
    uint32_t dataLength;
    BatchGetItemResponse items[0];
} BatchGetResponse;

typedef struct {
    uint64_t keyLen : 16;
    uint64_t valueLen : 24;
    uint64_t offset : 24;
    uint64_t version;
    int32_t result;
    uint32_t reserved;
    uint64_t valueAddr;
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
    uint64_t seqNo;
    uint64_t negoSeqNo;
    uint64_t remoteAddr;
    MmsMemoryKey remoteKey;
    uint32_t ioLength;
    uint32_t reserved;
} OneSideIoRequest;

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
static constexpr uint16_t IO_DESCRIPTION_LEN = sizeof(IoLocDesc);

struct KvCbCtx {
    std::atomic<uint16_t> quota;
    std::atomic<int32_t> result;

    KvCbCtx() = default;
    KvCbCtx(uint16_t q, int32_t r) : quota(q), result(r) {}
};

void UpdateCrcSwitch(bool crcSwitch);
void UpdateLocalPtVersion(uint64_t ptVersion);

using AllocFunc = std::function<BResult(uint64_t, uint16_t &, uintptr_t &)>;

struct IOCtxItem {
    uint64_t buff;
    uint64_t reqLen;

    IOCtxItem(uint64_t buff, uint64_t reqLen) : buff(buff), reqLen(reqLen){};
};

struct DecodePutItem {
    const char *key;
    const char *value;
    uint32_t valueLen;
    uint16_t keyLen;
    uint16_t isNotify;
    uint64_t version;
    int32_t *result;
    uint64_t *valueAddr;
};

struct DecodeUpdateItem {
    const char *key;
    const char *value;
    uint32_t valueLen;
    uint32_t offset;
    uint16_t keyLen;
    uint64_t version;
    int32_t *result;
};

struct DecodeDeleteItem {
    const char *key;
    uint16_t keyLen;
    uint16_t isNotify;
    uint64_t version;
    int32_t *result;
};

BResult EncodePutRequest(PutItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                         const AllocFunc &allocFunc, uint32_t ioCtxBuffLen);
BResult DeCodePutRequest(std::vector<DecodePutItem> &itemList, uint32_t &itemNum, uint64_t buff, uint64_t realLen);

BResult EncodeUpdateRequest(UpdateItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                            const AllocFunc &allocFunc, uint32_t ioCtxBuffLen);
BResult DeCodeUpdateRequest(std::vector<DecodeUpdateItem> &itemList, uint32_t &itemNum, uint64_t buff,
                            uint64_t realLen);

BResult EncodeDeleteRequest(DeleteItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                            const AllocFunc &allocFunc, uint32_t ioCtxBuffLen);
BResult DeCodeDeleteRequest(std::vector<DecodeDeleteItem> &itemList, uint32_t &itemNum, uint64_t buff,
                            uint64_t realLen);

BResult EncodeReplaceRequest(ReplaceItems *itemList, uint32_t itemNum, std::vector<IOCtxItem> &ctxItems,
                             const AllocFunc &allocFunc, uint32_t ioCtxBuffLen);
BResult DeCodeReplaceRequest(std::vector<DecodeUpdateItem> &itemList, uint32_t &itemNum, uint64_t buff,
                             uint64_t realLen);

uint32_t FillPutItemResults(PutItems *itemList, uint32_t itemIndex, const std::vector<IOCtxItem> &ctxItems);
uint32_t FillUpdateItemResults(UpdateItems *itemList, uint32_t itemIndex, const std::vector<IOCtxItem> &ctxItems);
uint32_t FillDeleteItemResults(DeleteItems *itemList, uint32_t itemIndex, const std::vector<IOCtxItem> &ctxItems);
uint32_t FillReplaceItemResults(ReplaceItems *itemList, uint32_t itemIndex, const std::vector<IOCtxItem> &ctxItems);

}
}
#endif // MMS_MESSAGE_H
