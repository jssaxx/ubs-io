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

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <semaphore.h>
#include <string>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "gtest/gtest.h"
#include "securec.h"
#include "bdm_core.h"
#include "bdm_disk.h"

namespace {
constexpr uint32_t URING_TEST_BDM_ID = 27;
constexpr uint64_t URING_TEST_DISK_LEN = 1073741824UL;
constexpr uint64_t URING_TEST_CHUNK_LEN = 4194304UL;
constexpr uint64_t URING_TEST_IO_LEN = 4096UL;
constexpr char URING_CHILD_ENV[] = "BOOSTIO_URING_TEST_CHILD";

class TempUringDisk {
public:
    TempUringDisk()
        : path("./bio_ut_bdm_uring_" + std::to_string(getpid()))
    {
        static_cast<void>(unlink(path.c_str()));
    }

    ~TempUringDisk()
    {
        static_cast<void>(unlink(path.c_str()));
    }

    bool Create() const
    {
        int fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd < 0) {
            return false;
        }
        int ret = ftruncate(fd, URING_TEST_DISK_LEN);
        static_cast<void>(close(fd));
        return ret == 0;
    }

    std::string path;
};

struct UringCallbackContext {
    sem_t semaphore;
    int32_t result{ BDM_CODE_ERR };
    uint32_t count{ 0 };
};

void UringCallback(void *context, int32_t result)
{
    auto *callback = static_cast<UringCallbackContext *>(context);
    callback->result = result;
    ++callback->count;
    static_cast<void>(sem_post(&callback->semaphore));
}

bool WaitForCallback(UringCallbackContext &context)
{
    timespec deadline{};
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        return false;
    }
    deadline.tv_sec += 10;
    int ret;
    do {
        ret = sem_timedwait(&context.semaphore, &deadline);
    } while (ret != 0 && errno == EINTR);
    return ret == 0;
}
}

extern "C" void __gcov_dump(void);

void RunUringScenario()
{
    ASSERT_EQ(BdmExit(), BDM_CODE_OK);
    ASSERT_EQ(BdmSetIoEngine("io_uring"), BDM_CODE_OK);
    ASSERT_EQ(BdmSetUringSqpollMode("disabled"), BDM_CODE_OK);
    ASSERT_EQ(BdmSetSyncWorkerNum(1), BDM_CODE_OK);

    int32_t initRet = BdmInit();
    if (initRet != BDM_CODE_OK) {
        return;
    }

    BdmSetNormalDiskNum(1);
    TempUringDisk disk;
    ASSERT_TRUE(disk.Create());

    BdmCreatePara para = {};
    ASSERT_EQ(strncpy_s(para.name, sizeof(para.name), disk.path.c_str(), disk.path.size()), EOK);
    ASSERT_GE(sprintf_s(para.sn, sizeof(para.sn), "uring_%u", URING_TEST_BDM_ID), 0);
    para.length = URING_TEST_DISK_LEN;
    para.bdmId = URING_TEST_BDM_ID;
    para.minChunkSize = URING_TEST_CHUNK_LEN;
    para.maxChunkSize = URING_TEST_CHUNK_LEN;

    uint32_t createdId = BDM_INVALID_ID;
    ASSERT_EQ(BdmCreate(&para, &createdId), BDM_CODE_OK);
    ASSERT_EQ(createdId, URING_TEST_BDM_ID);

    char path[BDM_NAME_LEN] = {};
    EXPECT_EQ(BdmGetDiskPath(createdId, nullptr, sizeof(path)), BDM_CODE_ERR);
    EXPECT_EQ(BdmGetDiskPath(createdId, path, 0), BDM_CODE_ERR);
    EXPECT_EQ(BdmGetDiskPath(BDM_INVALID_ID, path, sizeof(path)), BDM_CODE_NOT_EXIST);
    ASSERT_EQ(BdmGetDiskPath(createdId, path, sizeof(path)), BDM_CODE_OK);
    EXPECT_STREQ(path, disk.path.c_str());

    uint64_t chunkId = 0;
    ASSERT_EQ(BdmAlloc(createdId, 7, 9, URING_TEST_CHUNK_LEN, &chunkId), BDM_CODE_OK);
    uint64_t parsedOffset = 0;
    char parsedPath[BDM_NAME_LEN] = {};
    EXPECT_EQ(BdmParseChunkId(chunkId, &parsedOffset, nullptr), BDM_CODE_INVALID_PARAM);
    ASSERT_EQ(BdmParseChunkId(chunkId, &parsedOffset, parsedPath), BDM_CODE_OK);
    EXPECT_STREQ(parsedPath, disk.path.c_str());

    void *writeBuffer = nullptr;
    void *readBuffer = nullptr;
    ASSERT_EQ(posix_memalign(&writeBuffer, URING_TEST_IO_LEN, URING_TEST_IO_LEN * 2), 0);
    ASSERT_EQ(posix_memalign(&readBuffer, URING_TEST_IO_LEN, URING_TEST_IO_LEN * 2), 0);
    ASSERT_NE(writeBuffer, nullptr);
    ASSERT_NE(readBuffer, nullptr);
    ASSERT_EQ(memset_s(writeBuffer, URING_TEST_IO_LEN * 2, 0x5A, URING_TEST_IO_LEN * 2), EOK);
    ASSERT_EQ(memset_s(readBuffer, URING_TEST_IO_LEN * 2, 0, URING_TEST_IO_LEN * 2), EOK);

    ASSERT_EQ(BdmWrite(chunkId, 0, writeBuffer, URING_TEST_IO_LEN), BDM_CODE_OK);
    ASSERT_EQ(BdmRead(chunkId, 0, readBuffer, URING_TEST_IO_LEN), BDM_CODE_OK);
    EXPECT_EQ(memcmp(writeBuffer, readBuffer, URING_TEST_IO_LEN), 0);

    UringCallbackContext singleWrite;
    ASSERT_EQ(sem_init(&singleWrite.semaphore, 0, 0), 0);
    BdmIoCtx singleWriteIo{ UringCallback, &singleWrite };
    ASSERT_EQ(BdmWriteAsync(chunkId, URING_TEST_IO_LEN, writeBuffer, URING_TEST_IO_LEN, &singleWriteIo), BDM_CODE_OK);
    ASSERT_TRUE(WaitForCallback(singleWrite));
    EXPECT_EQ(singleWrite.result, BDM_CODE_OK);
    EXPECT_EQ(sem_destroy(&singleWrite.semaphore), 0);

    UringCallbackContext singleRead;
    ASSERT_EQ(sem_init(&singleRead.semaphore, 0, 0), 0);
    BdmIoCtx singleReadIo{ UringCallback, &singleRead };
    ASSERT_EQ(BdmReadAsync(chunkId, URING_TEST_IO_LEN, readBuffer, URING_TEST_IO_LEN, &singleReadIo), BDM_CODE_OK);
    ASSERT_TRUE(WaitForCallback(singleRead));
    EXPECT_EQ(singleRead.result, BDM_CODE_OK);
    EXPECT_EQ(sem_destroy(&singleRead.semaphore), 0);

    UringCallbackContext batchContexts[2];
    BdmIoCtx batchIoContexts[2] = {
        { UringCallback, &batchContexts[0] }, { UringCallback, &batchContexts[1] }
    };
    BdmBatchIo batch[2] = {};
    for (uint32_t i = 0; i < 2; ++i) {
        ASSERT_EQ(sem_init(&batchContexts[i].semaphore, 0, 0), 0);
        batch[i].chunkId = chunkId;
        batch[i].offset = i * URING_TEST_IO_LEN;
        batch[i].buf = static_cast<char *>(readBuffer) + i * URING_TEST_IO_LEN;
        batch[i].len = URING_TEST_IO_LEN;
        batch[i].ioCtx = &batchIoContexts[i];
    }
    ASSERT_EQ(BdmReadBatchAsync(batch, 2), BDM_CODE_OK);
    for (uint32_t i = 0; i < 2; ++i) {
        ASSERT_TRUE(WaitForCallback(batchContexts[i]));
        EXPECT_EQ(batchContexts[i].result, BDM_CODE_OK);
        EXPECT_EQ(sem_destroy(&batchContexts[i].semaphore), 0);
    }

    EXPECT_EQ(BdmFree(createdId, URING_TEST_CHUNK_LEN, chunkId), BDM_CODE_OK);
    EXPECT_EQ(BdmDestroy(createdId), BDM_CODE_OK);
    free(writeBuffer);
    free(readBuffer);
    EXPECT_EQ(BdmExit(), BDM_CODE_OK);
}

TEST(TestDiskUring, ReadWriteAndBatchIo)
{
    if (getenv(URING_CHILD_ENV) != nullptr) {
        RunUringScenario();
        __gcov_dump();
        _exit(testing::Test::HasFailure() ? 1 : 0);
    }

    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        if (setenv(URING_CHILD_ENV, "1", 1) != 0) {
            _exit(126);
        }
        execl("/proc/self/exe", "bio_test", "--gtest_filter=TestDiskUring.*", nullptr);
        _exit(127);
    }

    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
