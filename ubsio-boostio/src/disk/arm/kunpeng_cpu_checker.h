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

#ifndef OMNI_RUNTIME_OMNIRUNTIME_CPU_CHECKER_H
#define OMNI_RUNTIME_OMNIRUNTIME_CPU_CHECKER_H

#ifdef __cplusplus
extern "C" {
#endif
#define IMPLEMENTER_SHIFT 24
#define IMPLEMENTER_MASK 0xFF
#define PART_NUM_SHIFT 4
#define PART_NUM_MASK 0xFFF

// 0x41 is ARM
#define ARM_VENDOR_ID 0x41

// 0x48 is HiSilicon
#define HISILICON_VENDOR_ID 0x48

// 0xd01 is Kunpeng 920, 0xd08 is Kunpeng 916
#define KUNPENG_PART_ID 0xd08

int KunpengCpuCheck(void)
{
    unsigned long int midrEl1;
    asm("mrs %0, MIDR_EL1" : "=r"(midrEl1));
    unsigned int partId = (midrEl1 >> PART_NUM_SHIFT) & PART_NUM_MASK;
    unsigned int vendorId = (midrEl1 >> IMPLEMENTER_SHIFT) & IMPLEMENTER_MASK;
    if (vendorId == HISILICON_VENDOR_ID || (vendorId == ARM_VENDOR_ID && partId == KUNPENG_PART_ID)) {
        return 0;
    }
    return -1;
}

#ifdef __cplusplus
}
#endif
#endif // OMNI_RUNTIME_OMNIRUNTIME_CPU_CHECKER_H