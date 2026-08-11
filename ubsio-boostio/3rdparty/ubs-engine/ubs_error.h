/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * ubs-engine is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_ERROR_H
#define UBS_ERROR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 统一错误码类型
typedef enum
{
    /* ====================== 公共错误码 ====================== */
    UBS_SUCCESS = 0,

    // 参数错误 (1-9)
    UBS_ERR_INVALID_ARG = 1,      // 无效参数
    UBS_ERR_OUT_OF_RANGE = 2,     // 参数超出范围
    UBS_ERR_NULL_POINTER = 3,     // 空指针
    UBS_ERR_BUFFER_TOO_SMALL = 4, // 缓冲区不足

    // 资源错误 (10-19)
    UBS_ERR_OUT_OF_MEMORY = 10,      // 内存不足
    UBS_ERR_RESOURCE_BUSY = 11,      // 资源忙
    UBS_ERR_RESOURCE_EXHAUSTED = 12, // 资源耗尽
    UBS_ERR_QUOTA_EXCEEDED = 13,     // 配额超出

    // IPC通信错误 (20-29)
    UBS_ERR_IPC_CONNECTION_FAILED = 20,             // IPC连接失败
    UBS_ERR_IPC_TIMEOUT = 21,                       // IPC超时
    UBS_ERR_IPC_SERVICE_UNAVAILABLE = 22,           // 服务不可用
    UBS_ERR_IPC_CONNECTION_FAILED_PATH_LENGTH = 23, // 由于socket path长度导致IPC连接失败

    // 权限错误 (30-39)
    UBS_ERR_PERMISSION_DENIED = 30,     // 权限不足
    UBS_ERR_AUTHENTICATION_FAILED = 31, // 认证失败
    UBS_ERR_ACCESS_DENIED = 32,         // 访问被拒

    // 操作错误 (40-49)
    UBS_ERR_NOT_IMPLEMENTED = 40,  // 功能未实现
    UBS_ERR_NOT_SUPPORTED = 41,    // 不支持的操作
    UBS_ERR_OPERATION_FAILED = 42, // 操作失败
    UBS_ERR_TIMED_OUT = 43,        // 操作超时

    // 守护进程错误 (50-59)
    UBS_ERR_DAEMON_UNREACHABLE = 50, // 守护进程不可达
    UBS_ERR_DAEMON_BUSY = 51,        // 守护进程忙
    UBS_ERR_DAEMON_CRASHED = 52,     // 守护进程崩溃
    UBS_ERR_DAEMON_INTERNEL = 53,    // 守护进程内部错误

    /* ====================== UBSE错误码 (1000-1099) ====================== */
    UBS_ENGINE_ERR_OUT_OF_RANGE = 1000,      // 参数超范围
    UBS_ENGINE_ERR_RESOURCE = 1001,          // 资源申请错误
    UBS_ENGINE_ERR_CONNECTION_FAILED = 1002, // 连接UBSE服务端错误
    UBS_ENGINE_ERR_AUTH_FAILED = 1003,       // UBSE服务端认证鉴权不通过
    UBS_ENGINE_ERR_TIMEOUT = 1004,           // UBSE服务端处理超时
    UBS_ENGINE_ERR_INTERNAL = 1005,          // UBSE服务端内部错误
    UBS_ENGINE_ERR_EXISTED = 1006,           // 实例已存在
    UBS_ENGINE_ERR_NOT_EXIST = 1007,         // 实例不存在
    UBS_ENGINE_ERR_UDSINFO_MISMATCH = 1008,  // UDS INFO信息不匹配
    UBS_ENGINE_ERR_IMPORT_ABSENT = 1009,     // IMPORT不在位
    UBS_ENGINE_ERR_CREATING = 1010,          // 正在创建过程中
    UBS_ENGINE_ERR_DELETING = 1011,          // 正在删除过程中
    UBS_ENGINE_ERR_UNIMPORT_SUCCESS = 1012, // unimport已经成功, unexport失败, 资源没有释放完全, 后续对账能自动回收
    UBS_ENGINE_ERR_ALLOCATE = 1013,                     // 算法分配失败
    UBS_ENGINE_ERR_SHM_NO_CREATE = 1014,                // 共享内存未创建
    UBS_ENGINE_ERR_SHM_NO_ATTACH = 1015,                // 共享内存未导入
    UBS_ENGINE_ERR_SHM_ATTACHING = 1016,                // 正在导入共享内存过程中
    UBS_ENGINE_ERR_SHM_DETACHING = 1017,                // 正在删除导入共享内存过程中
    UBS_ENGINE_ERR_LINK_NOT_ALLOWED = 1018,             // 非poc组网不允许指定链路;poc组网必须指定链路
    UBS_ENGINE_ERR_LINK_NOT_EXIST = 1019,               // 链路不存在
    UBS_ENGINE_ERR_SHM_NODE_EMPTY = 1020,               // 参数节点信息为空
    UBS_ENGINE_ERR_COM_FAILED = 1021,                   // UBSE通信失败
    UBS_ENGINE_ERR_FIND_SRC_NUMA = 1022,                // 指定链路借用无法填充srcNuma
    UBS_ENGINE_ERR_SHM_DESTROYED = 1023,                // 共享内存被主动清理
    UBS_ENGINE_ERR_SHM_ATTACH_USING = 1024,             // 共享内存正在被使用无法被删除
    UBS_ENGINE_ERR_SHM_AFFINITY_PARAMS_ABNORMAL = 1025, // 指定CPU平面参数异常
    UBS_ENGINE_ERR_NUMA_ID_IS_NOT_IN_SOCKET = 1026,     // 当前的numaId不在socketId中
    UBS_ENGINE_ERR_NODE_NOT_EXIST = 1027,               // 节点不存在
    UBS_ENGINE_ERR_NODE_FAULT = 1028,                   // 节点故障
    UBS_ENGINE_ERR_EXPORT_LEDGERING = 1029,             // 导出节点对账中
    UBS_ENGINE_ERR_INVALID_PARAM = 1030,                // 参数错误
    UBS_ENGINE_ERR_CONNECT_UBM_FAILED = 1031,           // 访问MTI接口失败

    /* ====================== URMA Controller错误码 (1031-1040) ====================== */
    UBS_ENGINE_ERR_URMA_QOS_EXIST = 1032,                 // 优先级组已存在
    UBS_ENGINE_ERR_URMA_QOS_NOT_EXISTED = 1033,           // ETS模板未创建
    UBS_ENGINE_ERR_URMA_QUERY_PORTS_STATUS_FAILED = 1035, // 查询端口状态失败
    UBS_ENGINE_ERR_URMA_GET_NODE_INFO_FAILED = 1036,      // 查询节点信息失败
    UBS_ENGINE_ERR_URMA_CREATE_DEV_FAILED = 1037,         // 创建URMA设备失败
    UBS_ENGINE_ERR_URMA_DEV_NOT_INACTIVE = 1038,          // URMA设备状态异常，无法分配
    UBS_ENGINE_ERR_URMA_DEV_NOT_EXIST = 1039,             // URMA设备在内存中不存在
    UBS_ENGINE_ERR_URMA_DEV_NAME_INVALID = 1040,          // URMA设备名称无效

    UBS_ENGINE_ERR_IMPORT_LEDGERING = 1041, // 导入节点对账中

    /* ====================== 重复操作错误码 (2000-2099) ====================== */
    UBS_ENGINE_ERR_ALREADY_ALLOCATED = 2000, // 空间已分配, 重复分配报错
    UBS_ENGINE_ERR_ALREADY_ATTACHED = 2001,  // 空间已挂载, 重复挂载报错
    UBS_ENGINE_ERR_NO_NEED_FREE = 2002,      // 空间不存在或已释放, 无需释放报错
    UBS_ENGINE_ERR_NO_NEED_DETACH = 2003,    // 空间已卸载或未挂载, 无需卸载报错
} ubs_error_t;

/* ====================== 错误处理接口 ====================== */
const char* ubs_error_name(int32_t error);
const char* ubs_error_string(int32_t error);

#ifdef __cplusplus
}
#endif

#endif // UBS_ERROR_H
