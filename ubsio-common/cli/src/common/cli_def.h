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

#ifndef MMS_CLI_DEF_H
#define MMS_CLI_DEF_H

#include <stdint.h>

typedef char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef int SOCKET;

#ifndef RETURN_OK
#define RETURN_OK 0
#endif

#ifndef RETURN_ERROR
#define RETURN_ERROR (-1)
#endif

#ifndef UNREFERENCE_PARAM
#define UNREFERENCE_PARAM(x) ((void)(x))
#endif

#endif
