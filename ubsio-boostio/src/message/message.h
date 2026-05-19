/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include "bio_c.h"
#include "bio_config_instance.h"

#ifdef __cplusplus
extern "C" {
#endif

const uint16_t MESSAGE_MAGIC = 0xABCD;
const uint32_t KEY_MAX_SIZE = 256;
const uint32_t IP_MAX_SIZE = 32;
const uint32_t DISK_MAX_SIZE = 16;
const uint32_t CLUSTER_NODE_SIZE = 32;
const uint32_t PT_COPY_MAX_SIZE = 3;
const uint32_t PT_SIZE = 64;
const uint32_t SLICE_ADDR_MAX_SIZE = 16;
const uint32_t SLICE_ADDR_SIZE = 64;
const uint32_t MAX_EVICT_CONSULT_SIZE = 50;
const uint32_t MAX_LISTEN_ADDRESS_LENGTH = 32;
const uint32_t FILE_PATH_MAX_LEN = 256;
const uint32_t MAX_INTERCEPTOR_IO_SIZE = 4 * 1024 * 1024;
const uint64_t INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE = MAX_INTERCEPTOR_IO_SIZE;
const uint32_t INTERCEPTOR_DIRECT_SPACE_MAX_SEGMENTS = 2;
const uint64_t INTERCEPTOR_JUICEFS_CHUNK_SIZE = 64ULL * 1024ULL * 1024ULL;
const uint32_t INTERCEPTOR_SHM_FD_MAX_COUNT = 2;
const uint32_t INTERCEPTOR_SHM_FD_WITH_READ_INDEX_COUNT = 2;
const uint32_t INTERCEPTOR_SHM_FD_WITHOUT_READ_INDEX_COUNT = 1;
const uint32_t INTERCEPTOR_WRITE_MODE_LOCAL = 0;  // Prepared space is owned by local WCache after commit.
const uint32_t INTERCEPTOR_WRITE_MODE_REMOTE = 1; // Prepared space is only a source buffer and can be released.

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
    uint64_t mKey;
    uint32_t scene;
    uint32_t alignSize;
    uint32_t ioTimeOut;
    uint32_t netTimeOut;
    int32_t logLevel;
    bool enableCrc;
    bool enableCli;
    bool enablePrometheus;
    char listenAddress[MAX_LISTEN_ADDRESS_LENGTH];
    uint32_t scrapeIntervalSec;
    uint32_t wcacheMemEvictLevel;
    uint64_t segment;
    int32_t readIndexFd;
    uint32_t readIndexEntryCount;
    uint64_t readIndexLength;
} ShmInitResponse;

/* Query cache resource quota */
typedef struct {
    RequestComm comm;
} QueryQuotaRequest;

typedef struct {
    bool enable;
    uint64_t preloadSize;
} QueryQuotaResponse;

/* Alloc cache quota */
typedef struct {
    RequestComm comm;
    uint32_t nid;
    uint64_t cid;
    uint64_t allocQuota;
} AllocQuotaRequest;

typedef struct {
    uint64_t exceptQuota;
} AllocQuotaResponse;

/* Free cache quota */
typedef struct {
    RequestComm comm;
    uint32_t nid;
    uint64_t cid;
    uint64_t quota;
} FreeQuotaRequest;

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
    char hostMaster[NODE_DESC_SIZE];
    char hostSlave[NODE_DESC_SIZE];
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
    uint32_t copyNum;
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
    bool isDegrade;
} CreateFlowRequest;

typedef struct {
    uint64_t flowId;
    bool isDegrade;
} CreateFlowResponse;

/* Destroy flow */
typedef struct {
    RequestComm comm;
    uint64_t flowId;
} DestroyFlowRequest;

typedef struct {
    uint64_t flowId;
} DestroyFlowResponse;

/* Clear wcache */
typedef struct {
    RequestComm comm;
} ClearWcacheRequest;

typedef struct {
    uint32_t clearedCount;
} ClearWcacheResponse;

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
    uint64_t mrKey;
    bool memFromServer;
    bool sourceCopyRequired;
    bool isDegrade;
    uint32_t ioStrategy;
    uint64_t sliceLen;
    uint64_t quotaNid;
    uint64_t quotaCid;
    uint32_t dataCrc;
    char sliceBuf[0];
} PutRequest;

typedef struct {
    uint32_t ioStrategy;
} PutResponse;

constexpr uint8_t GET_REQUEST_MR_NORMAL = 0;
constexpr uint8_t GET_REQUEST_MR_REMOTE = 1;
constexpr uint8_t GET_REQUEST_MR_CACHE_SPACE = 2;
constexpr uint8_t GET_REQUEST_MR_TO_SHM_SPACE = 3;

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
    uint64_t mrKey;
    bool enableCrc;
    bool isConvDeploy;
} GetRequest;

typedef struct {
    bool isAlloc;
    uint32_t num;
    uint64_t updateQuota;
    uintptr_t address[SLICE_ADDR_SIZE];
    uint64_t addrOffset[SLICE_ADDR_SIZE];
    uint64_t addrLen[SLICE_ADDR_SIZE];
    uint64_t realLen;
    uint32_t dataCrc;
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
    uint64_t mrKey;
} ListRequest;

/* Add disk */
typedef struct {
    RequestComm comm;
    char diskPath[FILE_PATH_MAX_LEN];
} AddDiskRequest;

typedef struct {
    uint32_t result;
} AddDiskResponse;

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
    uint8_t memFreeType;
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

/* Notify Update */
typedef struct {
    RequestComm comm;
    bool flag;
} NotifyUpdateRequest;

/* Check Update Ready */
typedef struct {
    RequestComm comm;
} CheckUpdateReadyRequest;

typedef struct {
    bool flag;
} CheckUpdateReadyResponse;

typedef struct {
    RequestComm comm;
} CheckRemoteUpdateReadyRequest;

typedef struct {
    bool flag;
} CheckRemoteUpdateReadyResponse;

typedef struct {
    uint32_t pid;
    int32_t fd;
    uint64_t inode;
    uint64_t nbytes;
    int64_t offset;
    uint64_t startTime;
} InterceptorPwritePrepareSpaceIn;

typedef struct {
    int32_t ret;
    uint32_t mode;
    uint32_t addrNum;
    uint64_t offset;
    uint64_t nbytes;
    CacheSpaceDesc space;
    uint64_t addrOffset[CACHE_SPACE_ADDRESS_SIZE];
    uint64_t addrLen[CACHE_SPACE_ADDRESS_SIZE];
} InterceptorPwriteSpaceSegment;

typedef struct {
    int32_t ret;
    uint32_t segNum;
    InterceptorPwriteSpaceSegment segs[INTERCEPTOR_DIRECT_SPACE_MAX_SEGMENTS];
} InterceptorPwritePrepareSpaceOut;

typedef struct {
    uint32_t pid;
    int32_t fd;
    uint64_t inode;
    uint64_t nbytes;
    int64_t offset;
    uint64_t startTime;
    uint32_t segNum;
    uint32_t abortOnly;
    InterceptorPwriteSpaceSegment segs[INTERCEPTOR_DIRECT_SPACE_MAX_SEGMENTS];
} InterceptorPwriteCommitSpaceIn;

typedef struct {
    int32_t ret;
    uint64_t committedBytes;
} InterceptorPwriteCommitSpaceOut;

typedef struct {
    uint64_t inode;
    uint64_t offset;
    uint64_t length;
    uint32_t broadcastRemote;
} InterceptorReadIndexInvalidateIn;

typedef struct {
    uint32_t pid;
    uint64_t addrOffset;
    uint64_t length;
} InterceptorReadBufferReleaseIn;

constexpr uint32_t INTERCEPTOR_PREAD_FLAG_BIO_FALLBACK = 1U << 3U;
constexpr uint32_t INTERCEPTOR_PREAD_FLAG_PREFETCH = 1U << 4U;
constexpr uint32_t INTERCEPTOR_PREAD_DATA_BIO_SHM = 0;
constexpr uint32_t INTERCEPTOR_PREAD_DATA_READ_SHM = 2;

typedef struct {
    uint32_t pid;
    int32_t fd;
    uint64_t inode;
    uint64_t nbytes;
    int64_t offset;
    uint64_t startTime;
    uint32_t flags;
} InterceptorPreadIn;

typedef struct {
    int32_t ret;
    uint64_t dataLen;
    uint32_t addrNum;
    uint32_t dataSource;
    uint64_t addrOffset[SLICE_ADDR_SIZE];
    uint64_t addrLen[SLICE_ADDR_SIZE];
    uint64_t windowOffset;
    uint64_t windowDataLen;
    uint64_t windowAddrOffset;
    uint64_t windowAddrLen;
} InterceptorPreadOut;

typedef struct {
    RequestComm comm;
} GetUnderFsConfigRequest;

typedef struct {
    char cfgPath[KEY_MAX_SIZE];
    char cluster[KEY_MAX_SIZE];
    char user[KEY_MAX_SIZE];
    char pool[KEY_MAX_SIZE];
} CephConfigResponse;

typedef struct {
    char nameNode[KEY_MAX_SIZE];
    char workingPath[KEY_MAX_SIZE];
} HdfsConfigResponse;

typedef struct {
    char underFsType[KEY_MAX_SIZE];
    CephConfigResponse cephConfig;
    HdfsConfigResponse hdfsConfig;
} GetUnderFsConfigResponse;

typedef struct {
    uint64_t flowId;
    uint32_t count;
    uint64_t data[MAX_EVICT_CONSULT_SIZE];
} EvictNegotiateRequest;

typedef struct {
    bool negoResult[MAX_EVICT_CONSULT_SIZE];
} EvictNegotiateResponse;

/* Cache Resource */
typedef struct {
    RequestComm comm;
} CacheResourceRequest;

typedef struct {
    uint64_t rCacheMemCapacity;
    uint64_t rCacheDiskCapacity;
    uint64_t wCacheMemCapacity;
    uint64_t wCacheDiskCapacity;
    uint64_t rCacheMemUsedSize;
    uint64_t rCacheDiskUsedSize;
    uint64_t wCacheMemUsedSize;
    uint64_t wCacheDiskUsedSize;
    uint64_t actualMemUsedSize;
    uint64_t otherMemUsedSize;
    uint16_t nodeId;
} CacheResourceResponse;

/* Cache Hit */
typedef struct {
    RequestComm comm;
} CacheHitRequest;

typedef struct {
    uint64_t rCacheHitMemCount;
    uint64_t rCacheHitDiskCount;
    uint64_t rCacheHitCount;
    uint64_t rCacheTotalCount;
    uint64_t wCacheHitMemCount;
    uint64_t wCacheHitDiskCount;
    uint64_t wCacheHitCount;
    uint64_t wCacheTotalCount;
    uint64_t backendHitCount;
    uint16_t nodeId;
} CacheHitResponse;

typedef struct {
    uint64_t beginData;
    uint64_t goodEnd;
    uint64_t badEnd;
    uint64_t max;
    uint64_t min;
    uint64_t total;
} TraceMetrics;

typedef struct {
    char traceName[MAX_TRACE_NAME_LEN];
    TraceMetrics metrics;
} TraceData;

typedef struct {
    TraceData traces[TRACE_MAX_NUM];
    int count;
} TraceDatabase;

/* Get Trace Points */
typedef struct {
    uint16_t nodeId;
    RequestComm comm;
} GetTracePointsRequest;

typedef struct {
    TraceDatabase traceDatabase;
} GetTracePointsResponse;

#ifdef __cplusplus
}
#endif
#endif // MESSAGE_H
