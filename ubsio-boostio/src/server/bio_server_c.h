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

#ifndef BIO_SERVER_C_H
#define BIO_SERVER_C_H

#include "cstdint"
#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start server with the normal cluster module chain: Net, Cache, MirrorServer, CM.
int32_t BioServerInit();

// Start server as an embedded single-node runtime.
// No Net/CM/Zookeeper module is created; NodeView/PtView is built locally.
int32_t BioServerStandaloneInit();

// Set local standalone device information before BioServerStandaloneInit.
void SetStandaloneDeviceInfo(uint32_t deviceId);

void BioServerExit(void);

// Returns 0 in STANDALONE mode because no NetEngine exists.
uintptr_t GetBioServerNet();

bool GetCrcFlag();

bool GetCliFlag();

bool GetPrometheusToggle();

const char *GetPrometheusListenAddress(void);

uint32_t GetNegoWorkIoTimeOut();

uint32_t GetPrometheusScrapeIntervalSec();

// Return standalone runtime config without IPC shm negotiation.
// The response is caller-owned and does not carry IPC shm fields.
int32_t GetRuntimeConfig(StandaloneRuntimeConfigResponse *rsp);

int32_t GetLocalNid(GetLocalNidResponse *rsp);

int32_t GetQuotaInfo(QueryQuotaRequest *req, QueryQuotaResponse *rsp);

int32_t AllocQuota(AllocQuotaRequest *req, AllocQuotaResponse *rsp);

int32_t FreeQuota(FreeQuotaRequest *req);

int32_t GetNodeView(QueryNodeViewRequest *req, QueryNodeViewResponse *rsp);

int32_t GetPtView(QueryPtViewRequest *req, QueryPtViewResponse *rsp);

int32_t CreateFlowMaster(CreateFlowRequest *req, CreateFlowResponse *rsp);

int32_t CreateFlowSlave(CreateFlowRequest *req);

int32_t DestroyFlow(DestroyFlowRequest *req);

// Variable-length direct-call response. Server allocates *rsp with new[];
// caller must release it after copying the slice metadata.
int32_t GetSlice(GetSliceRequest *req, GetSliceResponse **rsp);

int32_t Put(PutRequest *req, PutResponse *rsp);

int32_t Get(GetRequest *req, GetResponse *rsp);

int32_t BatchGet(BatchGetRequest *req, BatchGetResponse *rsp);

int32_t BatchExist(BatchExistRequest *req, BatchExistResponse *rsp);

int32_t Delete(DeleteRequest *req);

// Variable-length direct-call response. Server allocates *rsp with new[];
// caller must release it after copying object stats.
int32_t List(ListRequest *req, ListResponse **rsp);

int32_t Stat(StatRequest *req, StatResponse *rsp);

int32_t Load(LoadRequest *req);

int32_t NotifyUpdate(NotifyUpdateRequest *req);

int32_t CheckUpdateReady(CheckUpdateReadyRequest *req, CheckUpdateReadyResponse *rsp);

int32_t GetCacheHitLocal(CacheHitResponse *rsp);

int32_t CalcCacheResourceLocal(CacheResourceResponse *rsp);

int32_t GetTracePointsLocal(GetTracePointsResponse *rsp);

int32_t AddDisk(AddDiskRequest *req, AddDiskResponse *rsp);
#ifdef __cplusplus
}
#endif
#endif // BIO_SERVER_C_H
