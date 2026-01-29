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

int32_t BioServerInit();

void BioServerExit(void);

uintptr_t GetBioServerNet();

bool GetCrcFlag();

bool GetCliFlag();

bool GetPrometheusToggle();

const char *GetPrometheusListenAddress(void);

uint32_t GetNegoWorkIoTimeOut();

uint32_t GetPrometheusScrapeIntervalSec();

int32_t GetLocalNid(GetLocalNidResponse *rsp);

int32_t GetQuotaInfo(QueryQuotaRequest *req, QueryQuotaResponse *rsp);

int32_t AllocQuota(AllocQuotaRequest *req, AllocQuotaResponse *rsp);

int32_t FreeQuota(FreeQuotaRequest *req);

int32_t GetNodeView(QueryNodeViewRequest *req, QueryNodeViewResponse *rsp);

int32_t GetPtView(QueryPtViewRequest *req, QueryPtViewResponse *rsp);

int32_t CreateFlowMaster(CreateFlowRequest *req, CreateFlowResponse *rsp);

int32_t CreateFlowSlave(CreateFlowRequest *req);

int32_t DestroyFlow(DestroyFlowRequest *req);

int32_t GetSlice(GetSliceRequest *req, GetSliceResponse **rsp);

int32_t Put(PutRequest *req, PutResponse *rsp);

int32_t Get(GetRequest *req, GetResponse *rsp);

int32_t Delete(DeleteRequest *req);

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