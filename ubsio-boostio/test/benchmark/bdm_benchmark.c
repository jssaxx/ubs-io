/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "bdm_core.h"

#define UNREFERENCE_PARAM(param) (void)(param)

#define TEST_CHUNK_SIZE (1024 * 1024)  // 1MB
#define TEST_CHUNK_NUM 10
#define TEST_IO_SIZE (4 * 1024)  // 4KB
#define TEST_IO_NUM 1000

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void async_callback(void *ctx, int32_t ret) {
    UNREFERENCE_PARAM(ctx);
    UNREFERENCE_PARAM(ret);
}

int main(int argc, char *argv[]) {
    int32_t ret;
    uint32_t bdmId;
    uint64_t chunkIds[TEST_CHUNK_NUM];
    char *buf = malloc(TEST_IO_SIZE);
    if (!buf) {
        fprintf(stderr, "malloc failed\n");
        return -1;
    }
    memset(buf, 0xAA, TEST_IO_SIZE);

    // 检查命令行参数
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_path>\n", argv[0]);
        free(buf);
        return -1;
    }
    char *diskPath = argv[1];
    printf("Using disk: %s\n", diskPath);

    // 初始化bdm
    ret = BdmInit();
    if (ret != BDM_CODE_OK) {
        fprintf(stderr, "BdmInit failed, ret=%d\n", ret);
        free(buf);
        return -1;
    }

    // 配置磁盘设备
    DiskDevices diskDevices = {
        .num = 1,
        .diskCaps = {TEST_CHUNK_SIZE * TEST_CHUNK_NUM * 2},
        .list = {{""}}
    };
    strncpy(diskDevices.list[0].path, diskPath, DISK_PATH_LEN - 1);
    diskDevices.list[0].path[DISK_PATH_LEN - 1] = '\0';

    // 启动bdm，指定磁盘设备
    ret = BdmStart(&diskDevices, TEST_CHUNK_SIZE);
    if (ret != BDM_CODE_OK) {
        fprintf(stderr, "BdmStart failed, ret=%d\n", ret);
        free(buf);
        return -1;
    }

    // 创建bdm实例
    BdmCreatePara createPara = {
        .name = "bdm_test",
        .sn = "bdm_test_sn",
        .offset = 0,
        .length = TEST_CHUNK_SIZE * TEST_CHUNK_NUM * 2,
        .bdmId = 0,
        .minChunkSize = TEST_CHUNK_SIZE,
        .maxChunkSize = TEST_CHUNK_SIZE
    };
    ret = BdmCreate(&createPara, &bdmId);
    if (ret != BDM_CODE_OK) {
        fprintf(stderr, "BdmCreate failed, ret=%d\n", ret);
        free(buf);
        return -1;
    }
    printf("Created bdm with id=%u\n", bdmId);

    // 分配chunk
    for (int i = 0; i < TEST_CHUNK_NUM; i++) {
        ret = BdmAlloc(bdmId, 0, 0, TEST_CHUNK_SIZE, &chunkIds[i]);
        if (ret != BDM_CODE_OK) {
            fprintf(stderr, "BdmAlloc failed, ret=%d\n", ret);
            free(buf);
            return -1;
        }
        printf("Allocated chunk %d: %lu\n", i, chunkIds[i]);
    }

    // 测试同步写
    printf("\nTesting BdmWrite...\n");
    double start_time = get_time_ms();
    for (int i = 0; i < TEST_IO_NUM; i++) {
        uint64_t chunkId = chunkIds[i % TEST_CHUNK_NUM];
        uint64_t offset = (i * TEST_IO_SIZE) % (TEST_CHUNK_SIZE - TEST_IO_SIZE);
        ret = BdmWrite(chunkId, offset, buf, TEST_IO_SIZE);
        if (ret != BDM_CODE_OK) {
            fprintf(stderr, "BdmWrite failed, ret=%d\n", ret);
            free(buf);
            return -1;
        }
    }
    double end_time = get_time_ms();
    double write_sync_time = end_time - start_time;
    printf("BdmWrite: %d IOs took %.2f ms, %.2f IOPS\n", 
           TEST_IO_NUM, write_sync_time, TEST_IO_NUM / (write_sync_time / 1000));

    // 测试异步写
    printf("\nTesting BdmWriteAsync...\n");
    start_time = get_time_ms();
    for (int i = 0; i < TEST_IO_NUM; i++) {
        uint64_t chunkId = chunkIds[i % TEST_CHUNK_NUM];
        uint64_t offset = (i * TEST_IO_SIZE) % (TEST_CHUNK_SIZE - TEST_IO_SIZE);
        BdmIoCtx ioCtx = {
            .cb = async_callback,
            .ctx = NULL
        };
        ret = BdmWriteAsync(chunkId, offset, buf, TEST_IO_SIZE, &ioCtx);
        if (ret != BDM_CODE_OK) {
            fprintf(stderr, "BdmWriteAsync failed, ret=%d\n", ret);
            free(buf);
            return -1;
        }
    }
    // 等待所有异步操作完成
    usleep(1000000);  // 1秒
    end_time = get_time_ms();
    double write_async_time = end_time - start_time;
    printf("BdmWriteAsync: %d IOs took %.2f ms, %.2f IOPS\n", 
           TEST_IO_NUM, write_async_time, TEST_IO_NUM / (write_async_time / 1000));

    // 测试同步读
    printf("\nTesting BdmRead...\n");
    start_time = get_time_ms();
    for (int i = 0; i < TEST_IO_NUM; i++) {
        uint64_t chunkId = chunkIds[i % TEST_CHUNK_NUM];
        uint64_t offset = (i * TEST_IO_SIZE) % (TEST_CHUNK_SIZE - TEST_IO_SIZE);
        ret = BdmRead(chunkId, offset, buf, TEST_IO_SIZE);
        if (ret != BDM_CODE_OK) {
            fprintf(stderr, "BdmRead failed, ret=%d\n", ret);
            free(buf);
            return -1;
        }
    }
    end_time = get_time_ms();
    double read_sync_time = end_time - start_time;
    printf("BdmRead: %d IOs took %.2f ms, %.2f IOPS\n", 
           TEST_IO_NUM, read_sync_time, TEST_IO_NUM / (read_sync_time / 1000));

    // 测试异步读
    printf("\nTesting BdmReadAsync...\n");
    start_time = get_time_ms();
    for (int i = 0; i < TEST_IO_NUM; i++) {
        uint64_t chunkId = chunkIds[i % TEST_CHUNK_NUM];
        uint64_t offset = (i * TEST_IO_SIZE) % (TEST_CHUNK_SIZE - TEST_IO_SIZE);
        BdmIoCtx ioCtx = {
            .cb = async_callback,
            .ctx = NULL
        };
        ret = BdmReadAsync(chunkId, offset, buf, TEST_IO_SIZE, &ioCtx);
        if (ret != BDM_CODE_OK) {
            fprintf(stderr, "BdmReadAsync failed, ret=%d\n", ret);
            free(buf);
            return -1;
        }
    }
    // 等待所有异步操作完成
    usleep(1000000);  // 1秒
    end_time = get_time_ms();
    double read_async_time = end_time - start_time;
    printf("BdmReadAsync: %d IOs took %.2f ms, %.2f IOPS\n", 
           TEST_IO_NUM, read_async_time, TEST_IO_NUM / (read_async_time / 1000));

    // 释放chunk
    for (int i = 0; i < TEST_CHUNK_NUM; i++) {
        ret = BdmFree(bdmId, TEST_CHUNK_SIZE, chunkIds[i]);
        if (ret != BDM_CODE_OK) {
            fprintf(stderr, "BdmFree failed, ret=%d\n", ret);
        }
    }

    // 销毁bdm实例
    ret = BdmDestroy(bdmId);
    if (ret != BDM_CODE_OK) {
        fprintf(stderr, "BdmDestroy failed, ret=%d\n", ret);
    }

    free(buf);
    printf("\nBenchmark completed.\n");
    return 0;
}
