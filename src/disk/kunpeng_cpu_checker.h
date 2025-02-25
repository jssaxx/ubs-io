/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 * Description: omniruntime_cpu_checker
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

// for qingsong CPU check
#define I2C_DEV_0 "/dev/i2c-0"     // I2C-0 device name
#define I2C_DEV_1 "/dev/i2c-1"     // I2C-1 device name
#define SLAVE_ADDR 0x43            // slave I2C slave address
#define REG_ADDR 0x2b              // I2C register address
#define REG_ADDR_LEN 1             // I2C register address len
#define DEVICE_ID_VALUE 0xEAC76903 // value of device ID
#define DEVICE_ID_LEN 4            // length of device ID

#define FORTIFY_SRC_STR_LEN 10
#define FORTIFY_SRC_IDX 1

#define I2C_MSG_NUM 2
int KunpengCpuCheck(void);
int QingsongCpuCheck(void);

#ifdef __cplusplus
}
#endif
#endif // OMNI_RUNTIME_OMNIRUNTIME_CPU_CHECKER_H