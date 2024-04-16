/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include "bio_c.h"

#ifdef __cplusplus
extern "C" {
#endif

const uint16_t MESSAGE_MAGIC = 0xABCD;
const uint32_t KEY_MAX_SIZE = 256;
const uint32_t IP_MAX_SIZE = 32;
const uint32_t DISK_MAX_SIZE = 8;
const uint32_t CLUSTER_NODE_SIZE = 32;
const uint32_t PT_COPY_MAX_SIZE = 2;
const uint32_t PT_SIZE = 64;
const uint32_t SLICE_ADDR_MAX_SIZE = 16;
const uint32_t SLICE_ADDR_SIZE = 4;

typedef struct {
    uint16_t magic;
    uint16_t ptId;
    uint64_t ptv;
    uint32_t srcNid;
    pid_t pid;
} RequestComm;

/* Shm init */
typedef struct {
    RequestComm comm;
} ShmInitRequest;

typedef struct {
    int32_t memFd;
    int32_t serverPid;
    uint64_t offset;
    uint64_t length;
    uint32_t mKey;
} ShmInitResponse;

/* Query node view */
typedef struct {
    uint16_t diskId;
    uint16_t diskStatus;
} DiskInfoDesc;

typedef struct {
    uint16_t groupId;
    uint16_t nodeId;
    char ip[IP_MAX_SIZE];
    uint16_t port;
    uint16_t status;
    uint32_t num;
    DiskInfoDesc diskDesc[DISK_MAX_SIZE];
} NodeInfoDesc;

typedef struct {
    RequestComm comm;
    uint32_t bar;
} QueryNodeViewRequest;

typedef struct {
    int32_t flag;
    uint64_t curNodeTimes;
    uint32_t num;
    NodeInfoDesc desc[CLUSTER_NODE_SIZE];
} QueryNodeViewResponse; // size:2456

/* Query pt view */
typedef struct {
    uint16_t nodeId;
    uint16_t diskId;
    uint16_t state;
} PtCopyDesc;

typedef struct {
    uint64_t version;
    uint16_t ptId;
    uint16_t state;
    uint16_t masterNodeId;
    uint16_t masterDiskId;
    PtCopyDesc copys[PT_COPY_MAX_SIZE];
} PtInfoDesc;

typedef struct {
    uint16_t masterPtId;
    uint16_t slavePtId;
} FileLocationQueryReq;

typedef struct {
    char hostMaster[16];
    char hostSlave[16];
    uint16_t portMaster;
    uint16_t portSlave;
} FileLocationQueryRsp;

typedef struct {
    RequestComm comm;
    uint32_t bar;
} QueryPtViewRequest;

typedef struct {
    int32_t flag;
    uint64_t curPtTimes;
    uint32_t num;
    PtInfoDesc desc[PT_SIZE];
} QueryPtViewResponse; // size:2072

/* Query local node info */
typedef struct {
    RequestComm comm;
} GetLocalNidRequest;

typedef struct {
    uint16_t groupId;
    uint16_t nodeId;
    uint16_t protocol;
} GetLocalNidResponse;

/* Create flow */
typedef struct {
    RequestComm comm;
    uint16_t opType;
    uint64_t flowId;
} CreateFlowRequest;

typedef struct {
    uint64_t flowId;
} CreateFlowResponse;

/* Get slice */
typedef struct {
    RequestComm comm;
    uint64_t flowId;
    uint64_t flowOffset;
    uint64_t flowIndex;
    uint64_t length;
} GetSliceRequest;

typedef struct {
    uint64_t chunkId;
    uint32_t chunkOffset;
    uint32_t chunkLen;
} SliceAddrDesc;

typedef struct {
    uint64_t addrNum;
    SliceAddrDesc addr[SLICE_ADDR_MAX_SIZE];
    uint64_t addrOffset[SLICE_ADDR_MAX_SIZE];
    uint64_t sliceLen;
    char sliceBuf[0];
} GetSliceResponse;

/* Put */
typedef struct {
    RequestComm comm;
    uint64_t tenantId;
    uint8_t affinity;
    uint8_t strategy;
    char key[KEY_MAX_SIZE];
    uint64_t length;
    uint64_t flowId;
    uint64_t flowOffset;
    uint64_t flowIndex;
    uintptr_t mrAddress;
    uint64_t mrSize;
    uint32_t mrKey;
    bool isExistLocal;
    uint8_t copyFree;
    uint32_t sliceLen;
    char sliceBuf[0];
} PutRequest;

/* Get */
typedef struct {
    RequestComm comm;
    char key[KEY_MAX_SIZE];
    uint16_t ptId;
    uint64_t offset;
    uint64_t length;
    uint8_t isMr;
    uintptr_t address;
    uint64_t size;
    uint32_t mrKey;
    bool isConvDeploy;
} GetRequest;

typedef struct {
    bool isAlloc;
    uint32_t num;
    uintptr_t address[SLICE_ADDR_SIZE];
    uint64_t addrOffset[SLICE_ADDR_SIZE];
    uint64_t addrLen[SLICE_ADDR_SIZE];
    uint64_t realLen;
} GetResponse;

/* Delete */
typedef struct {
    RequestComm comm;
    char key[KEY_MAX_SIZE];
} DeleteRequest;

/* Stat */
typedef struct {
    RequestComm comm;
    char key[KEY_MAX_SIZE];
} StatRequest;

typedef struct {
    uint32_t size;
    time_t time;
} StatResponse;

/* List */
typedef struct {
    RequestComm comm;
    char prefix[KEY_MAX_SIZE];
    bool isListUnderFs;
    uintptr_t address;
    uint64_t size;
    uint32_t mrKey;
} ListRequest;

typedef struct {
    uintptr_t addr;
    uint64_t addrOffset;
    uint32_t num;
    uint32_t buffLen;
    char statBuf[0];
} ListResponse;

/* Load */
typedef struct {
    RequestComm comm;
    char key[KEY_MAX_SIZE];
    uint64_t offset;
    uint64_t length;
} LoadRequest;

/* Report Hb */
typedef struct {
    RequestComm comm;
} HbRequest;

typedef struct {
    uint64_t curNodeTimes;
    uint64_t curPtTimes;
} HbResponse;

/* Sync data */
typedef struct {
    RequestComm comm;
} SyncDataRequest;

/* Evict */
typedef struct {
    RequestComm comm;
    uint64_t flowId;
} GetEvictRequest;

/* Free server memory */
typedef struct {
    RequestComm comm;
    uint32_t num;
    uintptr_t addr[SLICE_ADDR_SIZE];
} FreeMemRequest;

typedef struct {
    int32_t result;
    uint32_t quota;
    sem_t sem;
    void *resp;
    uint32_t respLen;
} ClientCallbackCtx;

/* AllocSpace */
typedef struct {
    RequestComm comm;
    uint16_t ptId;
    uint32_t length;
    uint64_t flowId;
    uint64_t offset;
    uint64_t index;
    ObjLocation location;
} AllocSpaceRequest;

typedef struct {
    uint32_t pid;
    uint64_t length;
    uint64_t startTime;
} InterceptorAllocPageReq;

typedef struct {
    uint64_t offset;
    uint64_t size;
} InterceptorAllocPage;

typedef struct {
    uint32_t pid;
    uint64_t addrOffset[CACHE_SPACE_ADDRESS_SIZE];
    CacheSpaceInfo address;
} InterceptorAllocPageRsp;

typedef struct {
    uint32_t pid;
    int32_t fd;
    uint64_t inode;
    uint64_t nbytes;
    int64_t offset;
    uint64_t startTime;
} InterceptorPreadIn;

typedef struct {
    int32_t ret;
    uint32_t unused;
    uint64_t dataLen;
    char data[];
} InterceptorPreadOut;

typedef struct {
    uint32_t pid;
    uint64_t fd;
    uint64_t inode;
    uint64_t nbytes;
    int64_t offset;
    uint64_t startTime;
    char data[];
} InterceptorPwriteIn;

typedef struct {
    uint32_t pid;
    uint64_t fd;
    uint64_t inode;
    uint64_t nbytes;
    int64_t offset;
    uint64_t startTime;
    CacheSpaceInfo address;
} InterceptorLargePwriteIn;

typedef struct {
    int32_t ret;
    uint32_t unused;
    int64_t dataLen;
} InterceptorPwriteOut;
#ifdef __cplusplus
}
#endif
#endif // MESSAGE_H