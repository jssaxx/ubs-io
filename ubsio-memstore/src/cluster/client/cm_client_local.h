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

#ifndef CM_CLIENT_LOCAL_H
#define CM_CLIENT_LOCAL_H

#include "cm_inner.h"

#ifdef __cplusplus
extern "C" {
#endif

uint16_t CmClientLocalGetNodeId(uint16_t poolId);

int32_t CmClientLocalGetNodeInfo(uint16_t poolId, NodeInfo *nodeInfo);

int32_t CmClientLocalGetNode(uint16_t poolId, NodeInfo *nodeInfo);

void CmClientLocalUpdateNodeInfo(uint16_t poolId, NodeInfo *nodeInfo);

int32_t CmClientLocalInit(void);

void CmClientLocalExit(void);

#ifdef __cplusplus
}
#endif

#endif

