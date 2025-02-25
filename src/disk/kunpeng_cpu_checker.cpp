/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 * Description: omniruntime_cpu_checker
 */

#include "kunpeng_cpu_checker.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

int KunpengCpuCheck(void)
{
#ifdef DISABLE_CPU_CHECKER
    printf("KunpengCpuCheck()\n");
    return 0;
#endif
    unsigned long int midrEl1;
    asm("mrs %0, MIDR_EL1" : "=r"(midrEl1));
    unsigned int partId = (midrEl1 >> PART_NUM_SHIFT) & PART_NUM_MASK;
    unsigned int vendorId = (midrEl1 >> IMPLEMENTER_SHIFT) & IMPLEMENTER_MASK;
    if (vendorId == HISILICON_VENDOR_ID || (vendorId == ARM_VENDOR_ID && partId == KUNPENG_PART_ID)) {
        return 0;
    }
    return -1;
}

int DoQingsongCpuCheck(std::string file)
{
#ifdef DISABLE_CPU_CHECKER
    return 0;
#endif
    int fd;
    int rsp;

    // register address
    unsigned char regAddr = REG_ADDR;
    // read result from I2C device
    unsigned char readResult[DEVICE_ID_LEN] = {0};

    unsigned int result;

    struct i2c_rdwr_ioctl_data packets;
    struct i2c_msg messages[I2C_MSG_NUM];
    fd = open(file.c_str(), O_RDONLY);
    if (fd < 0) {
        /* Fail to open i2c devices */
        return -1;
    }
    rsp = ioctl(fd, I2C_TENBIT, 0); // 7bit
    if (rsp != 0) {
        close(fd);
        /* Fail to set 7 bit addresse */
        return -1;
    }
    rsp = ioctl(fd, I2C_SLAVE, SLAVE_ADDR); // set slave device
    if (rsp != 0) {
        close(fd);
        /* Fail to set slave address */
        return -1;
    }
    messages[0].addr = SLAVE_ADDR;
    messages[0].flags = 0; // write data
    messages[0].len = REG_ADDR_LEN;
    messages[0].buf = &regAddr;

    messages[1].addr = SLAVE_ADDR;
    messages[1].flags = I2C_M_RD; // read data
    messages[1].len = DEVICE_ID_LEN;
    messages[1].buf = readResult;

    packets.msgs = messages;
    packets.nmsgs = I2C_MSG_NUM;

    rsp = ioctl(fd, I2C_RDWR, &packets);
    if (rsp < 0) {
        close(fd);
        /* failed to WRITE & READ I2C Device */
        return -1;
    }
    close(fd);
    result = *reinterpret_cast<unsigned int*>(readResult);

    if (result == DEVICE_ID_VALUE) {
        /* matched */
        return 0;
    }

    return -1;
}

int QingsongCpuCheck(void)
{
    std::string dev0 = I2C_DEV_0;
    std::string dev1 = I2C_DEV_1;
    return DoQingsongCpuCheck(dev0) == 0 || DoQingsongCpuCheck(dev1) == 0 ? 0 : -1;
}
