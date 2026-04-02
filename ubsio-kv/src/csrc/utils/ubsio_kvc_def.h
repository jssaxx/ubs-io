/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBSIO_KVC_DEF_H
#define UBSIO_KVC_DEF_H

#include <dlfcn.h>
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UBSIO_API __attribute__((visibility("default")))

#define DL_LOAD_SYM(TARGET_FUNC_VAR, TARGET_FUNC_TYPE, FILE_HANDLE, SYMBOL_NAME)           \
    do {                                                                                   \
        TARGET_FUNC_VAR = (TARGET_FUNC_TYPE)dlsym(FILE_HANDLE, SYMBOL_NAME);               \
        if ((TARGET_FUNC_VAR) == nullptr) {                                                \
            LOG_ERROR("Failed to call dlsym to load SYMBOL_NAME, error" << dlerror());     \
            dlclose(FILE_HANDLE);                                                          \
            return DFC_ERR;                                                                \
        }                                                                                  \
    } while (0)

#ifdef __cplusplus
}
#endif
#endif // UBSIO_KVC_DEF_H