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

#ifndef CM_SERVER_VIEW_H
#define CM_SERVER_VIEW_H

#include "cm_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t CmServerViewGetNodeState(uint16_t poolId, NodeStateInfo *state);

int32_t CmServerViewInit(void);

void CmServerViewExit(void);

int32_t CmServerViewRoleChange(CmServerRole role);

int32_t CmServerViewNodeListChange(CmNodeIdList *watchList);

int32_t CmServerViewNodeEvent(CmNodeEvent *nodeEvent);

#ifdef __cplusplus
}
#endif

#endif

