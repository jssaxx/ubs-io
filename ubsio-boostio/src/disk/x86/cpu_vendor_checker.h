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

#ifndef CPU_VENDOR_CHECKER_H
#define CPU_VENDOR_CHECKER_H

#include <stdint.h>
#include <string.h>
#include "securec.h"
#include "bdm_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VENDOR_NAME_MAX_LEN 13
#define VENDOR_REGS_COUNT 3
#define UNSUPPORTED_VENDOR_ID "HygonGenuine"

int32_t GetCpuVendorId(char* vendorId, size_t vendorLen)
{
    if (vendorId == NULL || vendorLen == 0) {
        BDM_LOGERROR(0, "Invalid parameter: vendorId or vendorIdLen");
        return BDM_CODE_ERR;
    }

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));

    uint32_t regs[VENDOR_REGS_COUNT] = {ebx, edx, ecx};
    int ret = memcpy_s(vendorId, VENDOR_NAME_MAX_LEN, regs, sizeof(regs));
    if (ret != BDM_CODE_OK) {
        BDM_LOGERROR(0, "Memcpy failed.");
        return BDM_CODE_ERR;
    }
    vendorId[VENDOR_NAME_MAX_LEN - 1] = '\0';

    BDM_LOGINFO(0, "Get cpu vendor success, vendor id:%s", vendorId);
    return BDM_CODE_OK;
}

int32_t CheckCpuVendor(void)
{
    char vendorId[VENDOR_NAME_MAX_LEN];
    int32_t ret = GetCpuVendorId(vendorId, sizeof(vendorId));
    if (ret != BDM_CODE_OK) {
        return ret;
    }

    if (strcmp(vendorId, UNSUPPORTED_VENDOR_ID) == 0) {
        BDM_LOGERROR(0, "Unsupported cpu vendor:%s", vendorId);
        return BDM_CODE_ERR;
    }

    return BDM_CODE_OK;
}

#ifdef __cplusplus
}
#endif
#endif  // CPU_VENDOR_CHECKER_H
