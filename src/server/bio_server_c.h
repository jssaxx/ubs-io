/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BIO_SERVER_C_H
#define BIO_SERVER_C_H

#include "cstdint"
#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t BioServerInit();

void BioServerExit();

uintptr_t GetBioServerNet();

bool GetCrcFlag();

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

int32_t ReportHb(uint64_t *curNodeTimes, uint64_t *curPtTimes);

#ifdef __cplusplus
}
#endif
#endif // BIO_SERVER_C_H