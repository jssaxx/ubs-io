/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include <semaphore.h>
#include "bio_mock.h"
#include "bio_file_util.h"
#include "bio_types.h"
#include "bdm_core.h"
#include "bio_err.h"
#include "bio_log.h"
#include "test_disk.h"

using namespace ock::bio;

bool TestDisk::gSetup = false;

void TestDisk::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestDisk::TearDown()
{
    return;
}

void TestDisk::Stub()
{
    MOCKER_CPP(&FileUtil::GetDiskCapacity, int64_t(*)(std::string & diskPath)).stubs().will(returnValue(1073741824));
}

struct AsyncTaskCtx {
    sem_t sem;
    int32_t ret;
};

struct AsyncCbCtx {
    int32_t ret;
    AsyncTaskCtx tsk;
};

static void UtAsyncProcCb(void *ctx, int retCode)
{
    auto *cbCtx = (AsyncCbCtx *)ctx;
    cbCtx->ret = retCode;
    sem_post(&cbCtx->tsk.sem);
}

TEST_F(TestDisk, test_disk_read_async_case_return_ok)
{
    LOG_INFO("test_disk_read_async_case_return_ok");
    char *buff = nullptr;
    int32_t ret = posix_memalign((void **)&buff, NO_4194304, NO_4194304);
    EXPECT_EQ(0, ret);
    EXPECT_FALSE(buff == nullptr);

    AsyncCbCtx cbCtx;
    cbCtx.ret = BIO_OK;
    sem_init(&cbCtx.tsk.sem, 0, 0);

    BdmIoCtx ioCtx;
    ioCtx.cb = UtAsyncProcCb;
    ioCtx.ctx = (void *)&cbCtx;
    ret = BdmReadAsync(0, 0, buff, NO_4194304, &ioCtx);
    EXPECT_EQ(BDM_CODE_OK, ret);
    if (ret == BDM_CODE_OK) {
        sem_wait(&cbCtx.tsk.sem);
        sem_destroy(&cbCtx.tsk.sem);
    }
    EXPECT_EQ(BDM_CODE_OK, cbCtx.ret);
    free(buff);
}

TEST_F(TestDisk, test_disk_write_async_case_return_ok)
{
    LOG_INFO("test_disk_write_async_case_return_ok");
    char *buff = nullptr;
    int32_t ret = posix_memalign((void **)&buff, NO_4194304, NO_4194304);
    EXPECT_EQ(0, ret);
    EXPECT_FALSE(buff == nullptr);

    AsyncCbCtx cbCtx;
    cbCtx.ret = BIO_OK;
    sem_init(&cbCtx.tsk.sem, 0, 0);

    BdmIoCtx ioCtx;
    ioCtx.cb = UtAsyncProcCb;
    ioCtx.ctx = (void *)&cbCtx;
    ret = BdmWriteAsync(0, 0, buff, NO_4194304, &ioCtx);
    EXPECT_EQ(BDM_CODE_OK, ret);
    if (ret == BDM_CODE_OK) {
        sem_wait(&cbCtx.tsk.sem);
        sem_destroy(&cbCtx.tsk.sem);
    }
    EXPECT_EQ(BDM_CODE_OK, cbCtx.ret);
    free(buff);
}
