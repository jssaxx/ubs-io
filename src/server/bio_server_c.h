/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BIO_SERVER_C_H
#define BIO_SERVER_C_H

#include "stdint.h"
#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t BioServerInit();

void BioServerUninit();

uintptr_t GetBioServerNet();

int32_t GetLocalNid(GetLocalNidResponse *rsp);

int32_t GetNodeView(QueryNodeViewResponse *rsp);

int32_t GetPtView(QueryPtViewResponse *rsp);

int32_t CreateFlowMaster(CreateFlowRequest *req, CreateFlowResponse *rsp);

int32_t CreateFlowSlave(CreateFlowRequest *req);

int32_t GetSlice(GetSliceRequest *req, GetSliceResponse **rsp);

int32_t Put(PutRequest *req);

int32_t Get(GetRequest *req, GetResponse *rsp);

int32_t Delete(DeleteRequest *req);

int32_t List(ListRequest *req, ListResponse **rsp);

int32_t Stat(StatRequest *req, StatResponse *rsp);

int32_t Load(LoadRequest *req);

#ifdef __cplusplus
}
#endif
#endif // BIO_SERVER_C_H