/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#ifndef CM_COMM_H
#define CM_COMM_H

#include "cm_inner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CM_NODE_STATE(state) (((state) == NODE_STATUS_OK) ? "ok" : "unok")
#define CM_NET_STATE(state) (((state) == NET_STATE_NORMAL) ? "norm" : "fault")
#define CM_DISK_STATE(state) (((state) == DISK_STATE_NORMAL) ? "norm" : "fault")

typedef enum {
    CM_SERVER_MASTER,
    CM_SERVER_SLAVE
} CmServerRole;

typedef struct {
    uint16_t poolId;
    uint16_t nodeNum;
    uint16_t nodeList[];
} CmNodeIdList;

typedef enum {
    CM_EVENT_NET = 0,
    CM_EVENT_DISK = 1,
    CM_EVENT_PT_FINISH = 2,
    CM_EVENT_ADD_DISK = 3,
    CM_EVENT_BUTT
} CmEventType;

typedef struct {
    uint16_t eventType;
    uint16_t poolId;
    uint16_t nodeId;
} CmNodeEvent;

typedef struct {
    uint64_t birthVersion;
    uint16_t ptId;
    uint16_t resv;
} CmPtFinish;

typedef struct {
    uint16_t eventType;
    uint16_t poolId;
    uint16_t nodeId;
    uint16_t ptNum;
    CmPtFinish ptList[];
} CmPtEvent;

uint64_t CmGetNanoTime(void);

uint64_t CmGetMilliTime(void);

uint64_t CmGetSecondsTime(void);

#ifdef __cplusplus
}
#endif

#endif
