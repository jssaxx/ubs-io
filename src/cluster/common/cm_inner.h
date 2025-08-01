/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef CM_INNER_H
#define CM_INNER_H

#include <stdint.h>
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CM_OK = 0,
    CM_ERR = -1,
    CM_PARAM_INVALID = -1100,
    CM_NOT_EXIST = -1099,
    CM_EXIST = -1098,
    CM_BUSY = -1097,
} CmReturnCode;

#define PT_MAX_COPY_NUM (3)                       /* 最大副本数[REP:1-3] */
#define PT_MAX_COPY_INDEX (PT_MAX_COPY_NUM * 2UL) /* 中间视图最大副本索引 */
#define PT_COPY_NUM_3 (3)                         /* 3副本 */
#define PT_COPY_NUM_2 (2)                         /* 2副本 */
#define PT_COPY_NUM_1 (1)                         /* 1副本 */
#define MAX_PT_ENTRY (1024)                       /* PT最大数量 */
#define MIN_PT_ENTRY (3)                          /* PT最小数量 */

#define MAX_NODE_NUM (1024) /* POOL管理节点的最大数量 */
#define MIN_NODE_NUM (3)    /* POOL管理节点的最小数量 */
#define MAX_POOL_NUM (512)  /* POOL的数量上限 */
#define DISK_LIST_NUM (16)  /* 每个节点上最大的磁盘数量 */
#define NET_LIST_NUM (4)    /* 每个节点上最大IO面IP列表数量 */
#define POOL_NAME_LEN (32)  /* POOL名长度 */
#define DISK_NAME_LEN (64)  /* 磁盘名长度 */
#define DISK_SN_LEN (64)    /* 磁盘SN长度 */
#define IP_ADDR_LEN (16)    /* IP地址长度 */

#define NODE_META_BUFF_LEN (4096)
#define NODE_ID_INVALID (0xFFFF)
#define DISK_ID_INVALID (0xFFFF)

typedef enum {
    CONFIG_ROLE_CMM = 1,
    CONFIG_ROLE_DATA = 2,
    CONFIG_ROLE_TOGETHER = 3,
} ConfigRole;

typedef enum {
    PT_STATE_INIT = 0,          // init
    PT_STATE_NORMAL = 1,        // OK
    PT_STATE_DEGRADE_LOSS1 = 2, // degrade 1
    PT_STATE_DEGRADE_LOSS2 = 3, // degrade 2
    PT_STATE_FAULT = 4,         // fault
    PT_STATE_LOSS1_BYPASS = 5,  // async
    PT_STATE_BUTT
} PtState;

typedef enum {
    PT_COPY_STATE_INIT = 0,
    PT_COPY_STATE_RUNNING = 1,
    PT_COPY_STATE_DOWN = 2,
    PT_COPY_STATE_OUT = 3,
    PT_COPY_STATE_RECOVERY = 4,
    PT_COPY_STATE_BUTT
} PtCopyState;

typedef enum {
    PT_NONE = 0,
    PT_REP_SINGLE = 1,
    PT_REP_DOUBLE = 2,
    PT_REP_TRIPLE = 3,
    PT_REDUNDANCE_BUTT
} PtRedundanceMode;

typedef struct {
    uint16_t nodeId;    /* pt副本所在node的nodeId */
    uint16_t diskId;    /* pt副本所在node上对应的diskId */
    uint16_t keepAlive; /* pt副本保活标识，当可用副本数降低到minCopyNum时激活，预防数据丢失 */
    uint16_t state;     /* pt中副本的状态，见 PtCopyState         */
} PtEntryCopy;

typedef struct {
    uint64_t birthVersion;  /* pt版本号 */
    uint16_t ptId;          /* pt id */
    uint16_t state;         /* PT状态，见 PtState     */
    uint16_t masterNodeId;  /* 分区主node id */
    uint16_t masterDiskId;  /* 分区主disk Id */
    uint64_t referNum : 56; /* pt副本临时故障/恢复时计数递增 */
    uint64_t copyNum : 8;
    PtEntryCopy copyList[PT_MAX_COPY_INDEX];
} PtEntry;

typedef struct {
    uint16_t poolId;
    uint16_t ptNum;
    uint16_t maxCopyNum;
    uint16_t minCopyNum;
    uint64_t globalVersion; /* pt全局版本号:视图计算时递增 */
    uint64_t changeVersion; /* pt更新版本号:视图更新时递增 */
    PtEntry ptEntryList[];
} PtEntryList;

typedef struct {
    uint64_t birthVersion;
    uint16_t ptId;
    uint16_t resv[3L];
} PtFinish;

typedef enum {
    NODE_STATUS_OK = 0,
    NODE_STATUS_UNOK = 1,
} NodeStatus;

typedef enum {
    NODE_STATE_INVALID = 0,
    NODE_STATE_UP = 1,
    NODE_STATE_DOWN = 2,
    NODE_STATE_BUTT
} NodeState;

typedef enum {
    NODE_CLUSTER_STATE_INVALID = 0,
    NODE_CLUSTER_STATE_OUT = 1,
    NODE_CLUSTER_STATE_IN = 2,
    NODE_CLUSTER_STATE_BUTT
} NodeClusterState;

typedef enum {
    NET_STATE_NORMAL = 0,
    NET_STATE_FAULT = 1,
    NET_STATE_BUTT
} NetState;

typedef struct {
    uint32_t ipv4Addr;
    uint16_t port;
    uint16_t state; // 见 NetState
} NetInfo;

typedef enum {
    DISK_TYPE_DRAM = 0,
    DISK_TYPE_SCM = 1,
    DISK_TYPE_SSD = 2,
    DISK_TYPE_BUTT
} DiskType;

typedef enum {
    DISK_STATE_NORMAL = 0,
    DISK_STATE_FAULT = 1,
    DISK_STATE_BUTT
} DiskState;

typedef enum {
    DISK_CLUSTER_STATE_INVALID = 0,
    DISK_CLUSTER_STATE_OUT = 1,
    DISK_CLUSTER_STATE_IN = 2,
    DISK_CLUSTER_STATE_BUTT
} DiskClusterState;

typedef struct {
    uint16_t diskId;
    uint16_t state; // 见 DiskState
} DiskInfo;

typedef struct {
    uint16_t type; // 见 DiskType
    uint16_t num;
    DiskInfo list[DISK_LIST_NUM];
} DiskList;

typedef struct {
    uint16_t num;
    uint16_t resv; // 对齐
    NetInfo list[NET_LIST_NUM];
} NetList;

typedef struct {
    uint16_t nodeId;
    uint16_t port;
    uint16_t status; // 见 NodeStatus
    uint16_t resv;   // 对齐
    char ipv4AddrStr[IP_ADDR_LEN];
    DiskList diskList;
    NetList netList;
} NodeInfo;

typedef struct {
    uint16_t poolId;
    uint16_t nodeNum;
    NodeInfo nodeList[];
} NodeInfoList;

typedef struct {
    uint16_t diskId;
    uint16_t clusterState; // 见 DiskClusterState
} NodeDiskState;

typedef struct {
    uint64_t sessionId;
    uint16_t nodeId;
    uint16_t state;        // 见 NodeState
    uint16_t clusterState; // 见 NodeClusterState
    uint16_t diskNum;
    NodeDiskState diskList[DISK_LIST_NUM];
} NodeStateInfo;

typedef struct {
    uint16_t poolId;
    uint16_t nodeNum;
    uint16_t masterNodeId;
    uint16_t resv;
    NodeStateInfo nodeList[];
} NodeStateList;

typedef struct {
    uint16_t nodeId;
    uint16_t len;
    char buf[];
} NodeMetaBuff;

typedef struct {
    char poolName[POOL_NAME_LEN];
    uint16_t poolId;
    DiskType type;
    PtRedundanceMode redundance;
    uint16_t initialNodeNum;
    uint16_t maxNodeNum;
    uint16_t maxPtNum;
} PoolInfo;

typedef struct {
    int32_t (*queryLocalNodeInfo)(NodeInfo *nodeInfo, void *ctx);
    int32_t (*queryLocalNodeMr)(NodeMetaBuff *mr, void *ctx);
    void *ctx;
} LocalNodeQueryOpHandle;

typedef struct {
    int32_t (*notifyNodeListChange)(NodeStateList *nodeList, void *ctx);
    void *ctx;
} NodeListChangeOpHandle;

typedef struct {
    int32_t (*notifyPtListChange)(PtEntryList *ptList, void *ctx);
    void *ctx;
} PtViewChangeOpHandle;

typedef struct {
    int32_t (*notifyDataInfoChange)(const char *key, void *value, uint32_t valLen, void *ctx);
    void *ctx;
} DataInfoChangeOpHandle;

typedef struct {
    char *zkIpMask;
    char *ipStr;
    uint32_t regTimeOut;     // 毫秒
    uint32_t regPermTimeOut; // 毫秒
} CmCfgInfo;

/*
 * 功能描述：CM初始化
 * 参数说明：ockPath，配置文件路径
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_Init(ConfigRole role, PoolInfo *pools, uint16_t num, const CmCfgInfo *cfgInfo);

/*
 * 功能描述：获取指定节点的基本信息
 * 参数说明：poolId: {in}，pool ID
 *       nodeInfo: {in/out}, NodeInfo结构体指针
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_GetNodeInfo(uint16_t poolId, NodeInfo *nodeInfo);

/*
 * 功能描述：获取指定节点的mr信息
 * 参数说明：poolId: {in}，pool ID
 *             mr: {in/out}, MetaBuffInfo结构体指针
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_GetNodeMr(uint16_t poolId, NodeMetaBuff *mr);

/*
 * 功能描述：获取当前CN节点的nodeId
 * 参数说明：poolId: {in}，pool ID
 * 返回值：nodeId，如果为NODE_ID_INVALID的话，则表示无效的nodeId
 */
uint16_t CM_GetLocalNodeId(uint16_t poolId);

/*
 * 功能描述：向CM注册本地节点信息获取钩子函数
 * 参数说明：poolId: {in}，pool ID
           handle: {in}, CM加载本地信息时，调用业务侧注册的查询接口
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_RegLocalNodeQueryOpHandle(uint16_t poolId, LocalNodeQueryOpHandle *handle);

/*
 * 功能描述：向CM注册CN集群节点视图更新时通知回调函数
 * 参数说明：poolId: {in}，pool ID
           handle: {in}, 回调函数
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_RegNodeListChangeNotifyHandle(uint16_t poolId, NodeListChangeOpHandle *handle);

/*
 * 功能描述：设置磁盘状态
 * 参数说明：poolId: {in}，pool ID
           diskId: {in}, 磁盘在当前节点上的编号（来源于BDM）
            state: {in}, 磁盘状态
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_SetDiskStatus(uint16_t poolId, uint16_t diskId, DiskState state);

/*
 * 功能描述：添加新磁盘
 * 参数说明：poolId: {in}，pool ID
            state: {in}, 磁盘状态
           diskId: {in}, 磁盘在当前节点上的编号（来源于BDM）
        isNewDisk: {in}, 是否新加盘标志
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_UpdateNodeInfo(uint16_t poolId, DiskState state, uint16_t diskId, bool isNewDisk);

/*
 * 功能描述：设置PT副本数据恢复完成
 * 参数说明：poolId: {in}，pool ID
            ptNum: {in}, pt数量
        eventList: {in}, pt列表
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_SetPtFinishStatus(uint16_t poolId, uint16_t ptNum, PtFinish *ptList);

/*
 * 功能描述：注册PTview更新通知与回调函数
 * 参数说明：poolId: {in}，pool ID
           handle: {in}, PTView更新回调函数，在PTView更之后，则会调用该回调函数
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_RegPtViewChangeOpHandle(uint16_t poolId, PtViewChangeOpHandle *handle);

/*
 * 功能描述：往指定POOL写入DataInfo数据
 * 参数说明：poolId: {in}，pool ID
              key: {in}, 数据索引键值
            value: {in}, 写入的数据
           valLen: {in}, 写入数据长度
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_WriteDataInfo(uint16_t poolId, const char *key, void *value, uint32_t valLen);

/*
 * 功能描述：注册DataInfo更新通知与回调函数
 * 参数说明：poolId: {in}，pool ID
           handle: {in}, DataInfo更新回调函数，在DataInfo更之后，则会调用该回调函数
 * 返回值：0表示成功，非0表示失败
 */
int32_t CM_RegDataInfoHandle(uint16_t poolId, const char *key, void *value, uint32_t valLen,
    DataInfoChangeOpHandle *handle);

/*
 * 功能描述：CM模块注销函数
 * 参数说明：无参数
 * 返回值：无返回值
 */
void CM_Exit(void);

#ifdef __cplusplus
}
#endif

#endif
