/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-engine is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_ENGINE_SSU_H
#define UBS_ENGINE_SSU_H

#include <stdint.h>
#include <sys/types.h>
#include "ubs_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UBS_SSU_MAX_NAME_LENGTH 48          // 请求标识最大48个字符, 含结尾字符'\0'
#define UBS_SSU_MAX_TENANT_LENGTH 17        // 请求方UPI(租户隔离标识)最大长度, 含结尾字符'\0'
#define UBS_SSU_MAX_NQN_LENGTH 69           // NVMe NQN最大长度69个字符, 含结尾字符'\0'
#define UBS_SSU_MAX_EID_LENGTH 17           // EID最大长度, 含结尾字符'\0'
#define UBS_SSU_MAX_UUID_LENGTH 37          // UUID标准长度37个字符, 含结尾字符'\0'
#define UBS_SSU_MAX_DEV_PATH_LENGTH 63      // 设备路径最大长度, 含结尾字符'\0'
#define UBS_SSU_MAX_DEV_NAME_LENGTH 33      // 聚合块设备名称最大长度, 含结尾字符'\0'
#define UBS_SSU_RAID5_MIN_MEMBER_NUM 3      // RAID5最少成员设备数
#define UBS_SSU_GUID_LENGTH 32          // GUID字节数组大小(定长, 非字符串)

// LBA格式, 值为对应字节数
typedef enum {
    UBS_SSU_LBA_FORMAT_512 = 512, // 512B
    UBS_SSU_LBA_FORMAT_4K = 4096, // 4K
} ubs_ssu_lba_format_t;

// 聚合RAID级别
typedef enum {
    UBS_SSU_RAID0 = 0, // RAID0条带化
    UBS_SSU_RAID5 = 5, // RAID5条带化带校验
} ubs_ssu_raid_level_t;

// 条带化chunk大小, 值为对应KB数, 需满足ubs_ssu_lba_format_t的整数倍
typedef enum {
    UBS_SSU_CHUNK_SIZE_4K = 4,     // 4KB
    UBS_SSU_CHUNK_SIZE_16K = 16,   // 16KB
    UBS_SSU_CHUNK_SIZE_32K = 32,   // 32KB
    UBS_SSU_CHUNK_SIZE_64K = 64,   // 64KB
    UBS_SSU_CHUNK_SIZE_128K = 128, // 128KB
    UBS_SSU_CHUNK_SIZE_256K = 256, // 256KB
    UBS_SSU_CHUNK_SIZE_512K = 512, // 512KB
} ubs_ssu_chunk_size_t;

// 分配策略
typedef enum {
    UBS_SSU_ALLOC_STRATEGY_STRIPED = 0, // 分布式策略, 尽量从多个设备均等分配, 适用于条带化编址使用场景
    UBS_SSU_ALLOC_STRATEGY_LINEAR = 1, // 顺序策略, 尽量从单个设备分配, 适用于线性编址使用场景
} ubs_ssu_alloc_strategy_t;

// 分配存储空间请求参数
typedef struct {
    char name[UBS_SSU_MAX_NAME_LENGTH];     // 请求标识, 最大48个字符
    uint64_t ns_size;                       // 申请总容量, 单位字节; 条带化策略时需整除ns_num,
                                            // 且整除后需为chunk_size的整数倍
    uint32_t ns_num;                        // 命名空间数量, 等于1时strategy不生效
    ubs_ssu_lba_format_t lba_format;        // LBA格式
    ubs_ssu_alloc_strategy_t strategy;      // 分配策略
    char tenant[UBS_SSU_MAX_TENANT_LENGTH]; // 请求方租户隔离标识
} ubs_ssu_alloc_space_req_t;

// 命名空间信息
typedef struct {
    char tgt_eid[UBS_SSU_MAX_EID_LENGTH];          // Target EID
    char tgt_nqn[UBS_SSU_MAX_NQN_LENGTH];          // 子系统NQN
    char ns_uuid[UBS_SSU_MAX_UUID_LENGTH];         // 物理设备UUID
    uint32_t ns_id;                                // 命名空间ID
    char ns_dev_path[UBS_SSU_MAX_DEV_PATH_LENGTH]; // 命名空间设备路径
    uint64_t ns_size;                              // 分配的容量, 单位字节
    ubs_ssu_lba_format_t lba_format;               // LBA格式
    uint32_t nqn_count;                            // hostNqn数量
    char** host_nqns;                              // hostNqn列表
} ubs_ssu_namespace_info_t;
// 分配存储空间结果
typedef struct {
    char name[UBS_SSU_MAX_NAME_LENGTH]; // 请求标识, 最大48个字符
    ubs_ssu_alloc_strategy_t strategy;  // 分配策略
    uint32_t namespace_cnt;             // 命名空间信息数量
    ubs_ssu_namespace_info_t *namespaces; // 命名空间信息列表, 由SDK内部动态分配, 需通过释放接口回收
} ubs_ssu_alloc_result_t;

// 挂载|卸载存储空间请求参数
typedef struct {
    char name[UBS_SSU_MAX_NAME_LENGTH];   // 需挂载|卸载的存储空间标识, 最大48个字符
    char nqn[UBS_SSU_MAX_NQN_LENGTH];     // Host 的 NVMe Qualified Name
    char src_eid[UBS_SSU_MAX_EID_LENGTH]; // 源EID
} ubs_ssu_space_req_t;

// 挂载|卸载线性编址存储空间请求参数
typedef struct {
    char name[UBS_SSU_MAX_NAME_LENGTH];         // 需挂载|卸载的存储空间标识, 最大48个字符
    char nqn[UBS_SSU_MAX_NQN_LENGTH];           // Host 的 NVMe Qualified Name
    char src_eid[UBS_SSU_MAX_EID_LENGTH];       // 源EID
    char dev_name[UBS_SSU_MAX_DEV_NAME_LENGTH]; // 聚合后的块设备名称, 由外部指定
} ubs_ssu_linear_space_req_t;

// 挂载|卸载条带化编址存储空间请求参数
typedef struct {
    char name[UBS_SSU_MAX_NAME_LENGTH];         // 需挂载|卸载的存储空间标识, 最大48个字符
    char nqn[UBS_SSU_MAX_NQN_LENGTH];           // Host 的 NVMe Qualified Name
    char src_eid[UBS_SSU_MAX_EID_LENGTH];       // 源EID
    char dev_name[UBS_SSU_MAX_DEV_NAME_LENGTH]; // 聚合后的块设备名称, 由外部指定
    ubs_ssu_raid_level_t level;                 // RAID级别(UBS_SSU_RAID0或UBS_SSU_RAID5)
    ubs_ssu_chunk_size_t chunk_size; // 条带化的chunk大小, 仅支持4KB/16KB/32KB/64KB/128KB/256KB/512KB
} ubs_ssu_striped_space_req_t;

// 虚拟功能单元(VFE)信息
typedef struct {
    uint8_t slot_id;  // 槽位ID
    uint8_t chip_id;  // 芯片ID
    uint8_t die_id;   // Die ID
    uint16_t pfe_id;  // 物理功能单元ID
    uint16_t vfe_id;  // 虚拟功能单元ID
    uint8_t vfe_guid[UBS_SSU_GUID_LENGTH];               // vfe GUID
    uint8_t bind_bus_instance_guid[UBS_SSU_GUID_LENGTH]; // 绑定的总线实例GUID
} ubs_ub_vfe_t;

// 功能单元(FE)信息, 包含所属PFE及其下的VFE列表
typedef struct {
    uint8_t slot_id;        // 槽位ID
    uint8_t chip_id;        // 芯片ID
    uint8_t die_id;         // Die ID
    uint16_t pfe_id;        // 物理功能单元ID
    uint8_t pfe_guid[UBS_SSU_GUID_LENGTH]; // pfe GUID
    uint32_t vfe_cnt;        // VFE数量
    ubs_ub_vfe_t *vfe_list; // VFE列表, 由SDK内部动态分配, 需通过释放接口回收
} ubs_ub_fe_t;

// 存储空间连接信息
typedef struct {
    char src_eid[UBS_SSU_MAX_EID_LENGTH];  // Source EID
    char tgt_eid[UBS_SSU_MAX_EID_LENGTH];  // Target EID
    char tgt_nqn[UBS_SSU_MAX_NQN_LENGTH];  // Target NQN
    char host_nqn[UBS_SSU_MAX_NQN_LENGTH]; // 默认NQN, 例: nqn.2024-01.com.huawei:uuid:12345678-...
    char ns_uuid[UBS_SSU_MAX_UUID_LENGTH]; // 物理设备UUID
    uint32_t ns_id;                        // 命名空间ID
} ubs_ssu_connect_info_t;

// 存储空间状态
typedef struct {
    char ns_uuid[UBS_SSU_MAX_UUID_LENGTH]; // 物理设备UUID
    uint32_t ns_id;                        // 命名空间ID
    uint64_t total_size;                   // 总容量, 单位字节
    uint64_t used_size;                    // 已用容量, 单位字节
} ubs_ssu_ns_stats_t;

/**
 * @brief 列出所有已分配的存储空间信息
 *
 * 获取系统中所有已分配的SSU存储空间详细信息, 包括命名空间列表、容量、LBA格式和使用类型等。
 *
 * @param results [OUT] 已分配空间信息列表, 调用成功时由SDK内部动态分配, 调用方需通过
 *                      ubs_ssu_alloc_info_list_free接口主动释放内存
 * @param result_cnt [OUT] 已分配空间信息数量
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 */
int32_t ubs_ssu_alloc_info_list(ubs_ssu_alloc_result_t **results, uint32_t *result_cnt);

/**
 * @brief 释放ubs_ssu_alloc_info_list返回的存储空间信息列表
 *
 * @param results [IN] ubs_ssu_alloc_info_list返回的列表指针
 * @param result_cnt [IN] 列表元素数量, 与ubs_ssu_alloc_info_list输出的result_cnt一致
 */
void ubs_ssu_alloc_info_list_free(ubs_ssu_alloc_result_t **results, uint32_t result_cnt);

/**
 * @brief 释放命名空间设备路径列表
 *
 * @param ns_dev_paths 命名空间设备路径列表指针
 * @param ns_dev_path_cnt 命名空间设备路径数量
 */
void ubs_ssu_ns_dev_paths_free(char ***ns_dev_paths, uint32_t ns_dev_path_cnt);

/**
 * @brief 获取存储空间的命名空间统计信息
 *
 * 查询指定存储空间下各命名空间的容量使用情况, 包括总容量和已用容量。
 *
 * @param name [IN] 存储空间标识
 * @param ns_stats_list [OUT] 命名空间统计信息列表, 调用成功时由SDK内部动态分配, 调用方需主动释放内存
 * @param ns_stats_cnt [OUT] 命名空间统计信息数量
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 */
int32_t ubs_ssu_ns_stats_get(const char *name, ubs_ssu_ns_stats_t **ns_stats_list, uint32_t *ns_stats_cnt);

/**
 * @brief 释放ubs_ssu_ns_stats_get返回的命名空间统计信息列表
 *
 * @param ns_stats_list [IN] ubs_ssu_ns_stats_get返回的列表指针
 */
void ubs_ssu_ns_stats_free(ubs_ssu_ns_stats_t **ns_stats_list);

/**
 * @brief 获取存储空间的连接信息
 *
 * 查询指定存储空间在指定VFE上的NVMe连接信息, 包括子系统NQN、Host NQN、命名空间ID等。
 *
 * @param name [IN] 存储空间标识
 * @param vfe [IN] VFE信息, 可选参数，如果指定vfe，连接信息里的src_eid为指定vfe的eid，否则src_eid为host侧分配给ssu的fe的eid
 * @param connect_info_list [OUT] 连接信息列表, 调用成功时由SDK内部动态分配, 调用方需主动释放内存
 * @param connect_info_cnt [OUT] 连接信息数量
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间或VFE不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 */
int32_t ubs_ssu_connect_info_get(const char *name, ubs_ub_vfe_t *vfe, ubs_ssu_connect_info_t **connect_info_list,
                                 uint32_t *connect_info_cnt);

/**
 * @brief 释放ubs_ssu_connect_info_get返回的连接信息列表
 *
 * @param connect_info_list [IN] ubs_ssu_connect_info_get返回的列表指针
 */
void ubs_ssu_connect_info_free(ubs_ssu_connect_info_t **connect_info_list);

/**
 * @brief 分配SSU存储空间
 *
 * 根据请求参数分配指定数量和大小的命名空间, 支持顺序分配和分布式分配两种策略。
 *
 * @param req [IN] 分配请求参数
 * @param result [OUT] 分配结果指针的地址, 由SDK内部动态分配整个 ubs_ssu_alloc_result_t 对象;
 *                     调用方需通过 ubs_ssu_alloc_info_free 接口主动释放
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:参数超出范围(ns_size不满足整除条件或name超长等);
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_EXISTED:存储空间已存在;
 * UBS_ENGINE_ERR_ALLOCATE:算法分配失败;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 *
 * @note 当ns_num为1时, strategy参数不生效
 */
int32_t ubs_ssu_space_alloc(const ubs_ssu_alloc_space_req_t *req, ubs_ssu_alloc_result_t **result);
/**
 * @brief 释放ubs_ssu_space_alloc返回的分配结果
 *
 * 释放result指向的整个 ubs_ssu_alloc_result_t 对象(包括其内部动态分配的 namespaces 数组),
 * 并将调用方的指针置为 nullptr。
 *
 * @param result [IN/OUT] 指向 ubs_ssu_alloc_result_t* 的指针,可为 nullptr
 */
void ubs_ssu_alloc_info_free(ubs_ssu_alloc_result_t **result);
/**
 * @brief 释放已分配的存储空间
 *
 * 释放之前通过ubs_ssu_space_alloc分配的存储空间及其关联的所有命名空间。
 *
 * @param name [IN] 要释放的存储空间标识, 与ubs_ssu_space_alloc时的name参数一致
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 *
 * @note 释放操作具有幂等性, 释放不存在的空间应返回成功
 */
int32_t ubs_ssu_space_free(const char *name);

/**
 * @brief 添加存储空间访问权限
 *
 * 为指定的Host授予对已分配存储空间的访问权限, 在Target侧将Host NQN添加到子系统的
 * 允许主机列表中, 使该Host可以通过NVMe-oF协议访问对应命名空间。
 *
 * @param name [IN] 存储空间标识, 与ubs_ssu_space_alloc时的name参数一致
 * @param nqn [IN] Host的NVMe Qualified Name, 标识被授权的主机
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name或nqn参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 *
 * @note 重复添加同一Host的访问权限应返回成功(幂等性保证)
 */
int32_t ubs_ssu_access_permission_add(const char *name, const char *nqn);

/**
 * @brief 移除存储空间访问权限
 *
 * 撤销指定Host对已分配存储空间的访问权限, 在Target侧将Host NQN从子系统的
 * 允许主机列表中移除, 使该Host无法再通过NVMe-oF协议访问对应命名空间。
 *
 * @param name [IN] 存储空间标识, 与ubs_ssu_space_alloc时的name参数一致
 * @param nqn [IN] Host的NVMe Qualified Name, 标识被撤销权限的主机
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name或nqn参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 *
 * @note 移除不存在的访问权限应返回成功(幂等性保证)
 * @note 移除权限前应确保该Host已断开与对应命名空间的连接
 */
int32_t ubs_ssu_access_permission_remove(const char *name, const char *nqn);

/**
 * @brief 挂载已分配的存储空间
 *
 * 将指定的存储空间挂载到系统, 使其可被主机访问。
 *
 * @param req [IN] 挂载请求参数, 包含存储空间标识、Host的NVMe Qualified Name和源EID
 * @param ns_dev_paths [OUT] 命名空间设备路径列表, 由SDK内部动态分配, 调用方需通过ubs_ssu_ns_dev_paths_free接口释放
 * @param ns_dev_path_cnt [OUT] 命名空间设备路径数量
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 */
int32_t ubs_ssu_space_attach(const ubs_ssu_space_req_t *req, char ***ns_dev_paths,
                             uint32_t *ns_dev_path_cnt);

/**
 * @brief 卸载已分配的存储空间
 *
 * 将指定的存储空间从系统卸载, 释放设备占用。
 *
 * @param req [IN] 卸载请求参数, 包含存储空间标识、Host的NVMe Qualified Name和源EID
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 *
 * @note 卸载前需确保没有进程正在使用该存储空间
 */
int32_t ubs_ssu_space_detach(const ubs_ssu_space_req_t *req);

/**
 * @brief 挂载线性编址的存储空间
 *
 * 将多个命名空间设备以线性拼接方式聚合为一个逻辑块设备并挂载。
 *
 * @param req [IN] 挂载请求参数, 包含存储空间标识、Host的NVMe Qualified Name、源EID和聚合后的块设备名称
 * @param ns_dev_paths [OUT] 命名空间设备路径列表, 由SDK内部动态分配, 调用方需通过ubs_ssu_ns_dev_paths_free接口释放
 * @param ns_dev_path_cnt [OUT] 命名空间设备路径数量
 * @param dev_path [OUT] 挂载后的聚合设备路径, 调用方需分配不小于UBS_SSU_MAX_DEV_PATH_LENGTH字节的缓冲区
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name或dev_name参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 *
 * @note 线性编址模式下, 数据按顺序填充各成员设备
 */
int32_t ubs_ssu_linear_space_attach(const ubs_ssu_linear_space_req_t *req,
                                    char ***ns_dev_paths, uint32_t *ns_dev_path_cnt,
                                    char dev_path[UBS_SSU_MAX_DEV_PATH_LENGTH]);

/**
 * @brief 卸载线性编址的存储空间
 *
 * 将线性聚合的块设备卸载并释放。
 *
 * @param req [IN] 卸载请求参数, 包含存储空间标识、Host的NVMe Qualified Name、源EID和聚合后的块设备名称
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name或dev_name参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 */
int32_t ubs_ssu_linear_space_detach(const ubs_ssu_linear_space_req_t *req);

/**
 * @brief 挂载条带化编址的存储空间
 *
 * 将多个命名空间设备以条带化方式聚合为一个逻辑块设备并挂载, 支持RAID0和RAID5两种级别。
 *
 * @param req [IN] 条带化挂载请求参数, 包含存储空间标识、Host的NVMe Qualified Name、源EID、
 *                 聚合后的块设备名称、RAID级别和chunk大小
 * @param ns_dev_paths [OUT] 命名空间设备路径列表, 由SDK内部动态分配, 调用方需通过ubs_ssu_ns_dev_paths_free接口释放
 * @param ns_dev_path_cnt [OUT] 命名空间设备路径数量
 * @param dev_path [OUT] 挂载后的聚合设备路径, 调用方需分配不小于UBS_SSU_MAX_DEV_PATH_LENGTH字节的缓冲区
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name参数超出范围;
 * UBS_ERR_INVALID_ARG:level或chunk_size参数无效;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 *
 * @note RAID5至少需要3个成员设备(UBS_SSU_RAID5_MIN_MEMBER_NUM)
 */
int32_t ubs_ssu_striped_space_attach(const ubs_ssu_striped_space_req_t *req,
                                     char ***ns_dev_paths, uint32_t *ns_dev_path_cnt,
                                     char dev_path[UBS_SSU_MAX_DEV_PATH_LENGTH]);

/**
 * @brief 卸载条带化编址的存储空间
 *
 * 将条带化聚合的块设备卸载并释放。
 *
 * @param req [IN] 卸载请求参数, 包含存储空间标识、Host的NVMe Qualified Name、源EID和聚合后的块设备名称
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ERR_OUT_OF_RANGE:name或dev_name参数超出范围;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:存储空间不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 */
int32_t ubs_ssu_striped_space_detach(const ubs_ssu_striped_space_req_t *req);

/**
 * @brief 获取功能单元设备列表
 *
 * 查询系统中所有FE设备信息, 包括每个PFE下的VFE列表。
 *
 * @param fe_list [OUT] FE设备信息列表, 调用成功时由SDK内部动态分配, 调用方需通过
 *                      ubs_ssu_fe_device_list_free接口主动释放内存
 * @param fe_cnt [OUT] FE设备数量
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 */
int32_t ubs_ssu_fe_device_list(ubs_ub_fe_t **fe_list, uint32_t *fe_cnt);

/**
 * @brief 释放ubs_ssu_fe_device_list返回的FE设备列表
 *
 * @param fe_list [IN] ubs_ssu_fe_device_list返回的列表指针
 * @param fe_cnt [IN] 列表元素数量, 与ubs_ssu_fe_device_list输出的fe_cnt一致
 */
void ubs_ssu_fe_device_list_free(ubs_ub_fe_t **fe_list, uint32_t fe_cnt);

/**
 * @brief 将VFE绑定到虚拟机
 *
 * 将指定的虚拟功能单元绑定到目标虚拟机, 使虚拟机可通过该VFE访问存储资源。
 *
 * @param upi [IN] 租户隔离标识
 * @param vfe [IN] 要绑定的VFE信息
 * @param bus_instance_guid [IN,OUT] 总线实例GUID, 定长字节数组;
 *                                   调用方需保证缓冲区至少 UBS_SSU_GUID_LENGTH 字节, 不允许传NULL;
 *                                   - IN: 全0(UBS_SSU_GUID_LENGTH字节全为0)表示由ubse内部创建
 *                                          vm busInstance, 并绑定到该VFE;
 *                                          非全0表示绑定到指定的虚拟机。
 *                                   - OUT: 操作成功后写入实际绑定的总线实例GUID
 *                                          (内部创建场景下为新创建的GUID)。
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针(bus_instance_guid为NULL或vfe为NULL);
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:VFE或虚拟机不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 */
int32_t ubs_ssu_fe_device_alloc(uint32_t upi, const ubs_ub_vfe_t *vfe, uint8_t *bus_instance_guid);

/**
 * @brief 释放VFE设备
 *
 * 将已分配的虚拟功能单元从目标虚拟机释放, 回收VFE设备资源。
 *
 * @param upi [IN] 租户隔离标识
 * @param vfe [IN] 要释放的VFE信息
 * @return UBS_SUCCESS:操作成功;
 * UBS_ERR_NULL_POINTER:空指针;
 * UBS_ENGINE_ERR_CONNECTION_FAILED:连接UBSE服务端失败;
 * UBS_ENGINE_ERR_AUTH_FAILED:UBSE服务端鉴权不通过;
 * UBS_ENGINE_ERR_NOT_EXIST:VFE或虚拟机不存在;
 * UBS_ENGINE_ERR_TIMEOUT:UBSE服务端处理超时;
 * UBS_ENGINE_ERR_INTERNAL:UBSE服务端内部错误
 */
int32_t ubs_ssu_fe_device_free(uint32_t upi, const ubs_ub_vfe_t *vfe);

#ifdef __cplusplus
}
#endif

#endif // UBS_ENGINE_SSU_H
