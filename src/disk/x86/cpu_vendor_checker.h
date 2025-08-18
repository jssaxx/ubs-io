/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
 * Description: omniruntime_cpu_checker
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

int32_t GetCpuVendorId(char* vendorId)
{
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

int32_t CheckCpuVendor()
{
    char vendorId[VENDOR_NAME_MAX_LEN];
    int32_t ret = GetCpuVendorId(vendorId);
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
