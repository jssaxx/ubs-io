/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#ifndef HYPERIO_COMMON_TRACEPOINT_TYPE_H
#define HYPERIO_COMMON_TRACEPOINT_TYPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * OSP开始的是兼容性定义，后续不要使用
 */
#ifndef _OSP_TYPES_
#define _OSP_TYPES_
/**
 * OSP_VOID类型定义
 */
typedef void            OSP_VOID;
/**
 * OSP_BOOL类型定义
 */
typedef  int            OSP_BOOL;
/**
 * OSP_ULONG类型定义
 * Linux, 在32位下是32位，在64位下是64位
 * Windows 32和64位环境都是32位
 */
typedef unsigned long   OSP_ULONG;
/**
 * OSP_LONG类型定义
 * Linux, 在32位下是32位，在64位下是64位
 * Windows 32和64位环境都是32位
 */
typedef long            OSP_LONG;
/**
 * OSP_CHAR类型定义
 */
typedef char            OSP_CHAR;


typedef int8_t   OSP_S8;
typedef int16_t  OSP_S16;
typedef int32_t  OSP_S32;
typedef int32_t  s32;
typedef int64_t  OSP_S64;
typedef int64_t  s64;

typedef uint8_t   OSP_U8;
typedef uint16_t  OSP_U16;
typedef uint32_t  OSP_U32;
typedef uint32_t  u32;
typedef uint64_t  OSP_U64;
typedef uint64_t  u64;
#endif

#define PID_OSP_NULL        0
#define BSP_SOFT_REBOOT     0

void dpax_Reboot(unsigned long reset_reason, unsigned int pid, char *message);

#ifdef __cplusplus
}
#endif
#endif // HYPERIO_COMMON_TRACEPOINT_TYPE_H
