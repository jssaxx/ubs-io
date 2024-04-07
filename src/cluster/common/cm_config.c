/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 */

#include "cm_config.h"
#include "cm_log.h"
#include "securec.h"

#define ZK_SERVER_LIST_LEN (128)

#define CM_LINK_MIN_TIMEOUT 5000   /* 心跳超时时间，最小值 */
#define CM_LINK_MAX_TIMEOUT 120000 /* 心跳超时时间，最大值 */

#define CM_PERM_FAULT_MIN_TIMEOUT 30000   /* 永久超时时间，最小值 */
#define CM_PERM_FAULT_MAX_TIMEOUT 1200000 /* 永久超时时间，最大值 */

typedef struct {
    uint16_t used;
    PoolInfo pool;
} CmPoolConfig;

typedef struct {
    ConfigRole role;
    char ipv4AddrStr[IP_ADDR_LEN];
    char zkServerList[ZK_SERVER_LIST_LEN];
    uint64_t timeOut;
    uint64_t permFaultTimeOut;
    uint64_t diskPermFaultTimeOut;
    CmPoolConfig poolList[MAX_POOL_NUM];
} CmConfig;

static CmConfig g_cmConfig = {
    .timeOut = 30000,
    .permFaultTimeOut = 120000,
    .diskPermFaultTimeOut = 0,
};

int32_t CmConfigHasCfgPoolC(void)
{
    return g_cmConfig.role == CONFIG_ROLE_DATA || g_cmConfig.role == CONFIG_ROLE_TOGETHER;
}

PoolInfo *CmConfigGetPoolInfo(uint16_t poolId)
{
    if (poolId >= MAX_POOL_NUM) {
        return NULL;
    }

    if (g_cmConfig.poolList[poolId].used == FALSE) {
        return NULL;
    }

    return &g_cmConfig.poolList[poolId].pool;
}

uint32_t CmConfigGetTimeOut(void)
{
    return (uint32_t)g_cmConfig.timeOut;
}

uint32_t CmConfigGetPermFaultTimeOut(void)
{
    return (uint32_t)g_cmConfig.permFaultTimeOut;
}

uint32_t CmConfigGetDiskPermFaultTimeOut(void)
{
    return (uint32_t)g_cmConfig.diskPermFaultTimeOut;
}

const char *CmConfigGetIpv4AddrStr(void)
{
    return (const char *)g_cmConfig.ipv4AddrStr;
}

const char *CmConfigGetZkServerList(void)
{
    return (const char *)g_cmConfig.zkServerList;
}

int32_t CmConfigHasCfgPoolS(void)
{
    return g_cmConfig.role == CONFIG_ROLE_CMM || g_cmConfig.role == CONFIG_ROLE_TOGETHER;
}

static int32_t CmConfigReset(PoolInfo *pools, uint16_t num)
{
    uint16_t index, poolId;
    for (poolId = 0; poolId < MAX_POOL_NUM; poolId++) {
        int32_t ret = memset_s(&g_cmConfig.poolList[poolId].pool, sizeof(PoolInfo), 0, sizeof(PoolInfo));
        if (ret != 0) {
            return CM_ERR;
        }
        g_cmConfig.poolList[poolId].used = FALSE;
        g_cmConfig.poolList[poolId].pool.poolId = poolId;
        g_cmConfig.poolList[poolId].pool.type = DISK_TYPE_DRAM;
        g_cmConfig.poolList[poolId].pool.redundance = PT_NONE;
        g_cmConfig.poolList[poolId].pool.initialNodeNum = MIN_NODE_NUM;
        g_cmConfig.poolList[poolId].pool.maxNodeNum = MIN_NODE_NUM;
        g_cmConfig.poolList[poolId].pool.maxPtNum = 0;
    }

    for (index = 0; index < num; index++) {
        if (pools[index].poolId >= MAX_POOL_NUM) {
            CM_LOGERROR("Invalid poolId(%u).", pools[index].poolId);
            return CM_ERR;
        }
        poolId = pools[index].poolId;
        g_cmConfig.poolList[poolId].pool.type = pools[index].type;
        g_cmConfig.poolList[poolId].pool.redundance = pools[index].redundance;
        g_cmConfig.poolList[poolId].pool.initialNodeNum = pools[index].initialNodeNum;
        g_cmConfig.poolList[poolId].pool.maxNodeNum = pools[index].maxNodeNum;
        g_cmConfig.poolList[poolId].pool.maxPtNum = pools[index].maxPtNum;
        g_cmConfig.poolList[poolId].used = TRUE;
    }

    return CM_OK;
}

int32_t CmConfigInit(ConfigRole role, PoolInfo *pools, uint16_t num, const CmCfgInfo *cfgInfo)
{
    int32_t ret;

    g_cmConfig.role = role;

    ret = CmConfigReset(pools, num);
    if (ret != CM_OK) {
        return ret;
    }

    g_cmConfig.timeOut = cfgInfo->regTimeOut;
    g_cmConfig.permFaultTimeOut = cfgInfo->regPermTimeOut;

    ret = strcpy_s(g_cmConfig.zkServerList, sizeof(g_cmConfig.zkServerList), cfgInfo->zkIpMask);
    if (ret != 0) {
        CM_LOGERROR("Strcpy_s failed, zkIpMask %s.", cfgInfo->zkIpMask);
        return CM_ERR;
    }

    ret = strcpy_s(g_cmConfig.ipv4AddrStr, sizeof(g_cmConfig.ipv4AddrStr), cfgInfo->ipStr);
    if (ret != 0) {
        CM_LOGERROR("Strcpy_s failed, ipStr %s.", cfgInfo->ipStr);
        return CM_ERR;
    }

    CM_LOGINFO("Cm pool init succeed.");
    return RETURN_OK;
}
