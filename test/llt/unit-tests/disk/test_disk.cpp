/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include <semaphore.h>
#include "securec.h"
#include "tracepoint.h"
#include "bio_mock.h"
#include "bio_file_util.h"
#include "bio_types.h"
#include "bdm_core.h"
#include "bio_err.h"
#include "bio_log.h"
#include "test_disk.h"

using namespace ock::bio;

bool TestDisk::gSetup = false;
static uint32_t g_bdmId = 0;

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
    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(buff == nullptr);

    AsyncCbCtx cbCtx;
    cbCtx.ret = BIO_OK;
    sem_init(&cbCtx.tsk.sem, 0, 0);

    BdmIoCtx ioCtx;
    ioCtx.cb = UtAsyncProcCb;
    ioCtx.ctx = (void *)&cbCtx;
    ret = BdmReadAsync(NO_MAX_VALUE64, 0, buff, NO_4194304, &ioCtx);
    EXPECT_EQ(BDM_CODE_NOT_EXIST, ret);
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
    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(buff == nullptr);

    AsyncCbCtx cbCtx;
    cbCtx.ret = BIO_OK;
    sem_init(&cbCtx.tsk.sem, 0, 0);

    BdmIoCtx ioCtx;
    ioCtx.cb = UtAsyncProcCb;
    ioCtx.ctx = (void *)&cbCtx;
    ret = BdmWriteAsync(NO_MAX_VALUE64, 0, buff, NO_4194304, &ioCtx);
    EXPECT_EQ(BDM_CODE_NOT_EXIST, ret);
    ret = BdmWriteAsync(0, 0, buff, NO_4194304, &ioCtx);
    EXPECT_EQ(BDM_CODE_OK, ret);
    if (ret == BDM_CODE_OK) {
        sem_wait(&cbCtx.tsk.sem);
        sem_destroy(&cbCtx.tsk.sem);
    }
    EXPECT_EQ(BDM_CODE_OK, cbCtx.ret);
    free(buff);
}

TEST_F(TestDisk, test_disk_free_case_return_fail)
{
    LOG_INFO("test_disk_free_return_fail");
    uint32_t bdmId = NO_1024;
    uint64_t len = NO_4194304;
    uint64_t chunkId = 0;
    auto ret = BdmFree(bdmId, len, chunkId);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
}

TEST_F(TestDisk, test_disk_read_case_return_fail)
{
    LOG_INFO("test_disk_read_return_fail");
    uint64_t chunkId = NO_MAX_VALUE64;
    uint64_t offset = 0;
    uint64_t len = NO_4194304;
    auto ret = BdmRead(chunkId, offset, nullptr, len);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
}

TEST_F(TestDisk, test_disk_write_case_return_fail)
{
    LOG_INFO("test_disk_write_return_fail");
    uint64_t chunkId = NO_MAX_VALUE64;
    uint64_t offset = 0;
    uint64_t len = NO_4194304;
    auto ret = BdmWrite(chunkId, offset, nullptr, len);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
}

TEST_F(TestDisk, test_disk_reset_scan_pool_case_return_fail)
{
    LOG_INFO("test_disk_reset_scan_pool_case_return_fail");
    uint32_t bdmId = NO_1024;
    auto ret = BdmResetScanPool(bdmId);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
}

TEST_F(TestDisk, test_disk_get_next_used_chunk_id_case_return_fail)
{
    LOG_INFO("test_disk_get_next_used_chunk_id_case_return_fail");
    uint32_t bdmId = NO_1024;
    uint64_t chunkId = 0;
    uint64_t chunkSize = NO_4194304;
    uint64_t bucketId = 0;
    uint64_t bucketOffset = 0;
    auto ret = BdmGetNextUsedChunkId(bdmId, &chunkId, &chunkSize, &bucketId, &bucketOffset);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
}

TEST_F(TestDisk, test_disk_bdm_creat)
{
    LOG_INFO("test_disk_bdm_creat");
    BdmCreatePara para = {0};
    int32_t ret = strncpy_s(para.name, BDM_NAME_LEN, "", 1);
    EXPECT_EQ(ret, BDM_CODE_OK);
    ret = strncpy_s(para.sn, BDM_SN_LEN, "", 1);
    EXPECT_EQ(ret, BDM_CODE_OK);
    para.offset = 0UL;
    para.length = 0UL;
    para.bdmId = 11U;
    para.pad = 0U;
    para.minChunkSize = 0UL;
    para.maxChunkSize = 0UL;

    ret = BdmCreate(&para, &g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    const char *name = "disk_name";
    const char *sn = "disk_sn";
    ret = strncpy_s(para.name, BDM_NAME_LEN, name, (BDM_NAME_LEN - 1));
    EXPECT_EQ(ret, BDM_CODE_OK);
    ret = strncpy_s(para.sn, BDM_SN_LEN, sn, (BDM_SN_LEN - 1));
    EXPECT_EQ(ret, BDM_CODE_OK);

    ret = BdmCreate(&para, &g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    para.length = 1073741824UL;
    ret = BdmCreate(&para, &g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    para.minChunkSize = NO_4194304;
    ret = BdmCreate(&para, &g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    para.maxChunkSize = NO_4194304;
    ret = BdmCreate(&para, &g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_OK);
}

TEST_F(TestDisk, test_disk_alloc_case_return_fail)
{
    LOG_INFO("test_disk_alloc_case_return_fail");
    uint32_t bdmId = NO_1024;
    uint64_t bucketId = 11UL;
    uint64_t bucketOffset = 0UL;
    uint64_t len = 0UL;
    uint64_t chunkId = 11UL;
    auto ret = BdmAlloc(bdmId, bucketId, bucketOffset, len, &chunkId);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);

    ret = BdmAlloc(g_bdmId, bucketId, bucketOffset, len, &chunkId);
    EXPECT_EQ(ret, BDM_CODE_INVALID_PARAM);

    len = NO_4194304;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "BDM_ALLOC_BLOCK_FAIL", 0, 1, userParam);
    ret = BdmAlloc(g_bdmId, bucketId, bucketOffset, len, &chunkId);
    EXPECT_EQ(ret, BDM_CODE_ERR);
    LVOS_HVS_deactiveTracePoint(0, "BDM_ALLOC_BLOCK_FAIL");
}

TEST_F(TestDisk, test_disk_bdm_destory)
{
    LOG_INFO("test_disk_bdm_destory");
    auto ret = BdmDestory(BDM_MAX_NUM);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);

    ret = BdmDestory(g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_OK);

    ret = BdmDestory(g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
}

TEST_F(TestDisk, test_disk_get_bdm_status_case_return_fail)
{
    LOG_INFO("test_disk_get_bdm_status_case_return_fail");
    auto ret = BdmGetDiskStatus(BDM_MAX_NUM);
    EXPECT_EQ(ret, BDM_DISK_STATE_FAULT);
}
