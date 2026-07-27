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

#include <fcntl.h>
#include <mockcpp/mockcpp.hpp>
#include <semaphore.h>
#include <string>
#include <unistd.h>
#include "securec.h"
#include "tracepoint.h"
#include "bio_mock.h"
#include "bio_file_util.h"
#include "bio_types.h"
#include "bdm_core.h"
#include "bdm_disk.h"
#include "bio_err.h"
#include "bio_log.h"
#include "bdm_threadpool.h"
#include "ngx_rbtree.h"
#include "bdm_allocator.h"
#include "test_disk.h"

using namespace ock::bio;

bool TestDisk::gSetup = false;
static uint32_t g_bdmId = 0;

namespace {
constexpr uint64_t TEST_BDM_META_DISK_LEN = 1073741824UL;

uint32_t BuildStandalonePad(uint32_t deviceId)
{
    return BDM_DISK_HEAD_STANDALONE_MAGIC | (deviceId & BDM_DISK_HEAD_DEVICE_ID_MASK);
}

uint32_t BuildVirtualPad(uint32_t deviceId, uint32_t deviceCount)
{
    return (BDM_DISK_HEAD_VIRTUAL_LAYOUT_VERSION << BDM_DISK_HEAD_LAYOUT_VERSION_SHIFT) |
        ((deviceCount << BDM_DISK_HEAD_DEVICE_COUNT_SHIFT) & BDM_DISK_HEAD_DEVICE_COUNT_MASK) |
        BuildStandalonePad(deviceId);
}

std::string MakeBdmMetaPath(uint32_t bdmId)
{
    return "./bio_ut_bdm_meta_" + std::to_string(getpid()) + "_" + std::to_string(bdmId);
}

class TempBdmFile {
public:
    explicit TempBdmFile(uint32_t bdmId) : path(MakeBdmMetaPath(bdmId))
    {
        static_cast<void>(unlink(path.c_str()));
    }

    ~TempBdmFile()
    {
        static_cast<void>(unlink(path.c_str()));
    }

    std::string path;
};

int32_t CreateBdmAndDestroy(const std::string &path, uint32_t bdmId, uint32_t pad, uint64_t offset, uint64_t length)
{
    int fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0 || ftruncate(fd, TEST_BDM_META_DISK_LEN) != 0) {
        if (fd >= 0) {
            close(fd);
        }
        return BDM_CODE_ERR;
    }
    close(fd);

    BdmCreatePara para = {0};
    int32_t ret = strncpy_s(para.name, BDM_NAME_LEN, path.c_str(), path.size());
    if (ret != BDM_CODE_OK) {
        return ret;
    }
    ret = sprintf_s(para.sn, BDM_SN_LEN, "%s_%u", "meta", bdmId);
    if (ret < 0) {
        return BDM_CODE_ERR;
    }
    para.offset = offset;
    para.length = length;
    para.bdmId = bdmId;
    para.pad = pad;
    para.minChunkSize = NO_4194304;
    para.maxChunkSize = NO_4194304;

    uint32_t createdBdmId = BDM_INVALID_ID;
    ret = BdmCreate(&para, &createdBdmId);
    if (ret != BDM_CODE_OK) {
        return ret;
    }
    EXPECT_EQ(createdBdmId, bdmId);
    EXPECT_EQ(BdmDestroy(createdBdmId), BDM_CODE_OK);
    return BDM_CODE_OK;
}

int32_t CreateBdmAndDestroy(const std::string &path, uint32_t bdmId, uint32_t pad)
{
    return CreateBdmAndDestroy(path, bdmId, pad, 0, TEST_BDM_META_DISK_LEN);
}

bool IsFileRangeZero(const std::string &path, uint64_t offset, uint64_t length)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    char buff[4096];
    uint64_t checkedLength = 0;
    while (checkedLength < length) {
        uint64_t remainingLength = length - checkedLength;
        size_t readLength = remainingLength < sizeof(buff) ? static_cast<size_t>(remainingLength) : sizeof(buff);
        ssize_t realLength = pread(fd, buff, readLength, static_cast<off_t>(offset + checkedLength));
        if (realLength != static_cast<ssize_t>(readLength)) {
            close(fd);
            return false;
        }
        for (size_t i = 0; i < readLength; i++) {
            if (buff[i] != 0) {
                close(fd);
                return false;
            }
        }
        checkedLength += readLength;
    }

    close(fd);
    return true;
}

bool WriteFileMarker(const std::string &path, uint64_t offset)
{
    int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        return false;
    }
    char marker = 1;
    bool success = pwrite(fd, &marker, sizeof(marker), static_cast<off_t>(offset)) ==
        static_cast<ssize_t>(sizeof(marker));
    close(fd);
    return success;
}

bool ClearFileMagic(const std::string &path, uint64_t offset)
{
    int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        return false;
    }
    uint64_t invalidMagic = 0;
    bool success = pwrite(fd, &invalidMagic, sizeof(invalidMagic), static_cast<off_t>(offset)) ==
        static_cast<ssize_t>(sizeof(invalidMagic));
    close(fd);
    return success;
}
}

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
    MOCKER_CPP(&FileUtil::ValidateRawDisk, bool(*)(const std::string &, std::string &))
        .stubs()
        .will(returnValue(true));
}

struct AsyncTaskCtx {
    sem_t sem;
    int32_t ret;
};

struct AsyncCbCtx {
    int32_t ret;
    uint32_t count;
    AsyncTaskCtx tsk;
};

static void UtAsyncProcCb(void *ctx, int retCode)
{
    auto *cbCtx = (AsyncCbCtx *)ctx;
    cbCtx->ret = retCode;
    cbCtx->count++;
    sem_post(&cbCtx->tsk.sem);
}

TEST_F(TestDisk, test_bdm_io_engine_config_case_return_ok)
{
    LOG_INFO("test_bdm_io_engine_config_case_return_ok");

    EXPECT_EQ(BdmSetIoEngine(nullptr), BDM_CODE_INVALID_PARAM);

    EXPECT_EQ(BdmSetIoEngine("sync"), BDM_CODE_OK);
    EXPECT_EQ(BdmGetIoEngine(), BDM_IO_ENGINE_SYNC);

    EXPECT_EQ(BdmSetIoEngine("libaio"), BDM_CODE_INVALID_PARAM);
    EXPECT_EQ(BdmGetIoEngine(), BDM_IO_ENGINE_SYNC);

    EXPECT_EQ(BdmSetIoEngine("io_uring"), BDM_CODE_OK);
    EXPECT_EQ(BdmGetIoEngine(), BDM_IO_ENGINE_IO_URING);

    EXPECT_EQ(BdmSetIoEngine("sync"), BDM_CODE_OK);
    EXPECT_EQ(BdmGetIoEngine(), BDM_IO_ENGINE_SYNC);

    EXPECT_EQ(BdmSetUringSqpollMode(nullptr), BDM_CODE_INVALID_PARAM);
    EXPECT_EQ(BdmSetUringSqpollMode("invalid"), BDM_CODE_INVALID_PARAM);
    EXPECT_EQ(BdmSetUringSqpollMode("required"), BDM_CODE_OK);
    EXPECT_EQ(BdmSetUringSqpollMode("disabled"), BDM_CODE_OK);
    EXPECT_EQ(BdmSetUringSqpollMode("auto"), BDM_CODE_OK);

    EXPECT_EQ(BdmSetSyncWorkerNum(0), BDM_CODE_INVALID_PARAM);
    EXPECT_EQ(BdmSetSyncWorkerNum(65), BDM_CODE_INVALID_PARAM);
    EXPECT_EQ(BdmSetSyncWorkerNum(1), BDM_CODE_OK);
    EXPECT_EQ(BdmSetSyncWorkerNum(16), BDM_CODE_OK);
}

TEST_F(TestDisk, test_bdm_batch_async_invalid_param_case_return_ok)
{
    LOG_INFO("test_bdm_batch_async_invalid_param_case_return_ok");

    EXPECT_EQ(BdmReadBatchAsync(nullptr, 1), BDM_CODE_INVALID_PARAM);
    EXPECT_EQ(BdmWriteBatchAsync(nullptr, 1), BDM_CODE_INVALID_PARAM);

    BdmBatchIo io = {0};
    EXPECT_EQ(BdmReadBatchAsync(&io, 0), BDM_CODE_INVALID_PARAM);
    EXPECT_EQ(BdmWriteBatchAsync(&io, 0), BDM_CODE_INVALID_PARAM);

    AsyncCbCtx cbCtx;
    cbCtx.ret = BDM_CODE_OK;
    cbCtx.count = 0;
    ASSERT_EQ(sem_init(&cbCtx.tsk.sem, 0, 0), 0);

    BdmIoCtx ioCtx = {0};
    ioCtx.cb = UtAsyncProcCb;
    ioCtx.ctx = (void *)&cbCtx;
    io.ioCtx = &ioCtx;
    io.len = NO_4194304;

    EXPECT_EQ(BdmReadBatchAsync(&io, 1), BDM_CODE_OK);
    sem_wait(&cbCtx.tsk.sem);
    EXPECT_EQ(cbCtx.ret, BDM_CODE_ERR);
    EXPECT_EQ(cbCtx.count, 1);

    cbCtx.ret = BDM_CODE_OK;
    EXPECT_EQ(BdmWriteBatchAsync(&io, 1), BDM_CODE_OK);
    sem_wait(&cbCtx.tsk.sem);
    EXPECT_EQ(cbCtx.ret, BDM_CODE_ERR);
    EXPECT_EQ(cbCtx.count, 2);

    sem_destroy(&cbCtx.tsk.sem);
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
    cbCtx.count = 0;
    sem_init(&cbCtx.tsk.sem, 0, 0);

    BdmIoCtx ioCtx;
    ioCtx.cb = UtAsyncProcCb;
    ioCtx.ctx = (void *)&cbCtx;
    ret = BdmReadAsync(NO_MAX_VALUE64, 0, buff, 0, &ioCtx);
    EXPECT_EQ(BDM_CODE_INVALID_PARAM, ret);

    ret = BdmReadAsync(NO_MAX_VALUE64, 0, nullptr, NO_4194304, &ioCtx);
    EXPECT_EQ(BDM_CODE_ERR, ret);

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
    cbCtx.count = 0;
    sem_init(&cbCtx.tsk.sem, 0, 0);

    BdmIoCtx ioCtx;
    ioCtx.cb = UtAsyncProcCb;
    ioCtx.ctx = (void *)&cbCtx;
    ret = BdmWriteAsync(NO_MAX_VALUE64, 0, buff, 0, &ioCtx);
    EXPECT_EQ(BDM_CODE_INVALID_PARAM, ret);

    ret = BdmWriteAsync(NO_MAX_VALUE64, 0, nullptr, NO_4194304, &ioCtx);
    EXPECT_EQ(BDM_CODE_ERR, ret);

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

    len = NO_4194304;
    ret = BdmFree(g_bdmId, len, chunkId);
    EXPECT_EQ(ret, BDM_CODE_OK);
}

TEST_F(TestDisk, test_disk_read_case_return_fail)
{
    LOG_INFO("test_disk_read_return_fail");
    uint64_t chunkId = NO_MAX_VALUE64;
    uint64_t offset = 0;
    uint64_t len = NO_4194304;
    auto ret = BdmRead(chunkId, offset, nullptr, 0);
    EXPECT_EQ(ret, BDM_CODE_INVALID_PARAM);

    ret = BdmRead(chunkId, offset, nullptr, len);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    char *buf = new char[1024];
    ret = BdmRead(chunkId, offset, buf, len);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
}

TEST_F(TestDisk, test_disk_write_case_return_fail)
{
    LOG_INFO("test_disk_write_return_fail");
    uint64_t chunkId = NO_MAX_VALUE64;
    uint64_t offset = 0;
    uint64_t len = NO_4194304;
    auto ret = BdmWrite(chunkId, offset, nullptr, 0);
    EXPECT_EQ(ret, BDM_CODE_INVALID_PARAM);

    ret = BdmWrite(chunkId, offset, nullptr, len);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    char *buf = new char[1024];
    ret = BdmWrite(chunkId, offset, buf, len);
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
    auto ret = BdmGetNextUsedChunkId(bdmId, nullptr, &chunkSize, &bucketId, &bucketOffset);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    ret = BdmGetNextUsedChunkId(bdmId, &chunkId, &chunkSize, &bucketId, &bucketOffset);
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

    ret = BdmCreate(&para, nullptr);
    EXPECT_EQ(ret, BDM_CODE_ERR);

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

TEST_F(TestDisk, test_disk_bdm_reset)
{
    LOG_INFO("test_disk_bdm_reset");
    BdmCreatePara para = {0};
    const char *name = "disk_name";
    const char *sn = "disk_sn";
    auto ret = strncpy_s(para.name, BDM_NAME_LEN, name, (BDM_NAME_LEN - 1));
    EXPECT_EQ(ret, BDM_CODE_OK);
    ret = strncpy_s(para.sn, BDM_SN_LEN, sn, (BDM_SN_LEN - 1));
    EXPECT_EQ(ret, BDM_CODE_OK);
    para.offset = 0UL;
    para.length = 1073741824UL;
    para.bdmId = 13U;
    para.pad = 0U;
    para.minChunkSize = NO_4194304;
    para.maxChunkSize = NO_4194304;
    ret = BdmCreate(&para, &g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_OK);

    ret = BdmResetDisk(para.bdmId);
}

TEST_F(TestDisk, test_set_disk_used_status)
{
    LOG_INFO("test_set_disk_used_status");
    BdmSetDiskUsedStatus(NO_1, true);

    BdmCreatePara para = {0};
    const char *name = "disk_name";
    const char *sn = "disk_sn";
    auto ret = strncpy_s(para.name, BDM_NAME_LEN, name, (BDM_NAME_LEN - 1));
    EXPECT_EQ(ret, BDM_CODE_OK);
    ret = strncpy_s(para.sn, BDM_SN_LEN, sn, (BDM_SN_LEN - 1));
    EXPECT_EQ(ret, BDM_CODE_OK);
    para.offset = 0UL;
    para.length = 1073741824UL;
    para.bdmId = 17U;
    para.pad = 0U;
    para.minChunkSize = NO_4194304;
    para.maxChunkSize = NO_4194304;
    ret = BdmCreate(&para, &g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_OK);

    BdmSetDiskUsedStatus(NO_1, true);
}

TEST_F(TestDisk, test_disk_alloc_case_return_fail)
{
    LOG_INFO("test_disk_alloc_case_return_fail");
    uint32_t bdmId = NO_1024;
    uint64_t bucketId = 11UL;
    uint64_t bucketOffset = 0UL;
    uint64_t len = 0UL;
    uint64_t chunkId = 11UL;
    auto ret = BdmAlloc(bdmId, bucketId, bucketOffset, BDM_MAX_CHUNK_LENGTH+1, &chunkId);
    EXPECT_EQ(ret, BDM_CODE_INVALID_PARAM);

    ret = BdmAlloc(bdmId, bucketId, bucketOffset, len, nullptr);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    ret = BdmAlloc(bdmId, bucketId, bucketOffset, len, &chunkId);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);

    ret = BdmAlloc(g_bdmId, bucketId, bucketOffset, len, &chunkId);
    EXPECT_EQ(ret, BDM_CODE_INVALID_PARAM);

    len = NO_4194304;
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "BDM_ALLOC_BLOCK_FAIL", 0, 1, userParam);
    ret = BdmAlloc(g_bdmId, bucketId, bucketOffset, len, &chunkId);
    EXPECT_EQ(ret, BDM_CODE_ERR);
    BioHvsDeactiveTracePoint(0, "BDM_ALLOC_BLOCK_FAIL");
}

TEST_F(TestDisk, test_disk_bdm_destroy)
{
    LOG_INFO("test_disk_bdm_destroy");
    auto ret = BdmDestroy(BDM_MAX_NUM);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);

    ret = BdmDestroy(g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_OK);

    ret = BdmDestroy(g_bdmId);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
}

TEST_F(TestDisk, test_bdm_metadata_restores_with_same_standalone_device_id)
{
    constexpr uint32_t bdmId = 700;
    TempBdmFile diskFile(bdmId);
    uint32_t pad = BuildStandalonePad(3);

    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, pad), BDM_CODE_OK);
    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, pad), BDM_CODE_OK);
}

TEST_F(TestDisk, test_bdm_metadata_rejects_different_standalone_device_id_without_rewrite)
{
    constexpr uint32_t bdmId = 701;
    TempBdmFile diskFile(bdmId);
    uint32_t originalPad = BuildStandalonePad(1);
    uint32_t mismatchedPad = BuildStandalonePad(2);

    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, originalPad), BDM_CODE_OK);
    EXPECT_NE(CreateBdmAndDestroy(diskFile.path, bdmId, mismatchedPad), BDM_CODE_OK);
    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, originalPad), BDM_CODE_OK);
}

TEST_F(TestDisk, test_bdm_metadata_cluster_pad_keeps_restore_behavior)
{
    constexpr uint32_t bdmId = 702;
    TempBdmFile diskFile(bdmId);

    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, 0), BDM_CODE_OK);
    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, 0), BDM_CODE_OK);
}

TEST_F(TestDisk, test_force_new_disk_skips_old_allocator_recovery)
{
    constexpr uint32_t bdmId = 708;
    TempBdmFile diskFile(bdmId);
    int fd = open(diskFile.path.c_str(), O_CREAT | O_RDWR, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(ftruncate(fd, TEST_BDM_META_DISK_LEN), 0);
    close(fd);

    BdmCreatePara para = {0};
    ASSERT_EQ(strncpy_s(para.name, BDM_NAME_LEN, diskFile.path.c_str(), diskFile.path.size()), BDM_CODE_OK);
    ASSERT_GE(sprintf_s(para.sn, BDM_SN_LEN, "%s_%u", "meta", bdmId), 0);
    para.length = TEST_BDM_META_DISK_LEN;
    para.bdmId = bdmId;
    para.pad = BuildVirtualPad(0, 1);
    para.minChunkSize = NO_4194304;
    para.maxChunkSize = NO_4194304;

    uint32_t createdBdmId = BDM_INVALID_ID;
    ASSERT_EQ(BdmCreate(&para, &createdBdmId), BDM_CODE_OK);
    uint64_t chunkId = 0;
    ASSERT_EQ(BdmAlloc(bdmId, 1, 2, NO_4194304, &chunkId), BDM_CODE_OK);
    ASSERT_EQ(BdmDestroy(bdmId), BDM_CODE_OK);

    ASSERT_EQ(BdmCreate(&para, &createdBdmId), BDM_CODE_OK);
    uint64_t totalCapacity = 0;
    uint64_t usedCapacity = 0;
    ASSERT_EQ(BdmGetCapacity(bdmId, &totalCapacity, &usedCapacity), BDM_CODE_OK);
    EXPECT_EQ(usedCapacity, NO_4194304);
    ASSERT_EQ(BdmDestroy(bdmId), BDM_CODE_OK);

    BdmDiskSetForceNew(1);
    int32_t ret = BdmCreate(&para, &createdBdmId);
    BdmDiskSetForceNew(0);
    ASSERT_EQ(ret, BDM_CODE_OK);
    ASSERT_EQ(BdmGetCapacity(bdmId, &totalCapacity, &usedCapacity), BDM_CODE_OK);
    EXPECT_EQ(usedCapacity, 0);
    ASSERT_EQ(BdmAlloc(bdmId, 3, 4, NO_4194304, &chunkId), BDM_CODE_OK);
    ASSERT_EQ(BdmDestroy(bdmId), BDM_CODE_OK);

    ASSERT_EQ(BdmCreate(&para, &createdBdmId), BDM_CODE_OK);
    ASSERT_EQ(BdmGetCapacity(bdmId, &totalCapacity, &usedCapacity), BDM_CODE_OK);
    EXPECT_EQ(usedCapacity, NO_4194304);
    EXPECT_EQ(BdmDestroy(bdmId), BDM_CODE_OK);
}

TEST_F(TestDisk, test_bdm_calculate_virtual_region)
{
    uint64_t offset = 0;
    uint64_t length = 0;
    constexpr uint64_t capacity = TEST_BDM_META_DISK_LEN + BDM_ALIGN_SIZE / 2;

    EXPECT_EQ(BdmCalculateVirtualRegion(capacity, NO_4194304, 3, 8, &offset, &length), BDM_CODE_OK);
    EXPECT_EQ(length, TEST_BDM_META_DISK_LEN / 8);
    EXPECT_EQ(offset, length * 3);
    EXPECT_EQ(BdmCalculateVirtualRegion(capacity, NO_4194304, 8, 8, &offset, &length),
        BDM_CODE_INVALID_PARAM);
    EXPECT_EQ(BdmCalculateVirtualRegion(capacity, NO_4194304, 0,
        BDM_VIRTUAL_LAYOUT_SLOT_NUM + 1, &offset, &length), BDM_CODE_INVALID_PARAM);
    EXPECT_EQ(BdmCalculateVirtualRegion(capacity, NO_4194304, 0, 3, &offset, &length), BDM_CODE_OK);
    EXPECT_EQ(length, TEST_BDM_META_DISK_LEN / BDM_VIRTUAL_LAYOUT_SLOT_NUM *
        (BDM_VIRTUAL_LAYOUT_SLOT_NUM / 3));
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(BdmCalculateVirtualRegion(BDM_RESTORE_META_SIZE + NO_4194304, NO_4194304, 0, 2, &offset, &length),
        BDM_CODE_INVALID_PARAM);

    constexpr uint64_t unalignedCapacity = TEST_BDM_META_DISK_LEN + BDM_ALIGN_SIZE * 7;
    uint64_t smallestRegionOffset = 0;
    uint64_t smallestRegionLength = 0;
    ASSERT_EQ(BdmCalculateVirtualRegion(unalignedCapacity, NO_4194304, 0, BDM_VIRTUAL_LAYOUT_SLOT_NUM,
        &smallestRegionOffset, &smallestRegionLength), BDM_CODE_OK);
    EXPECT_EQ(smallestRegionOffset, 0);
    for (uint32_t deviceCount = 1; deviceCount <= BDM_VIRTUAL_LAYOUT_SLOT_NUM; deviceCount++) {
        for (uint32_t deviceId = 0; deviceId < deviceCount; deviceId++) {
            ASSERT_EQ(BdmCalculateVirtualRegion(unalignedCapacity, NO_4194304, deviceId, deviceCount, &offset,
                &length), BDM_CODE_OK);
            EXPECT_EQ(length, smallestRegionLength * (BDM_VIRTUAL_LAYOUT_SLOT_NUM / deviceCount));
            EXPECT_EQ(offset, length * deviceId);
            EXPECT_EQ(offset % smallestRegionLength, 0);
            EXPECT_LE(offset + length, smallestRegionLength * BDM_VIRTUAL_LAYOUT_SLOT_NUM);
        }
    }
}

TEST_F(TestDisk, test_bdm_virtual_regions_restore_independently)
{
    constexpr uint32_t bdmId = 703;
    constexpr uint32_t deviceCount = 8;
    TempBdmFile diskFile(bdmId);
    uint64_t region0Offset = 0;
    uint64_t region0Length = 0;
    uint64_t region1Offset = 0;
    uint64_t region1Length = 0;
    ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, 0, deviceCount, &region0Offset,
        &region0Length), BDM_CODE_OK);
    ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, 1, deviceCount, &region1Offset,
        &region1Length), BDM_CODE_OK);

    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(0, deviceCount), region0Offset, region0Length),
        BDM_CODE_OK);
    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(1, deviceCount), region1Offset, region1Length),
        BDM_CODE_OK);
    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(0, deviceCount), region0Offset, region0Length),
        BDM_CODE_OK);
    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(1, deviceCount), region1Offset, region1Length),
        BDM_CODE_OK);
}

TEST_F(TestDisk, test_bdm_virtual_pad_mismatch_creates_fresh_allocator)
{
    constexpr uint32_t bdmId = 704;
    TempBdmFile diskFile(bdmId);
    constexpr uint64_t regionLength = TEST_BDM_META_DISK_LEN / 8;

    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(2, 8), 2 * regionLength, regionLength),
        BDM_CODE_OK);
    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(2, 16), 2 * regionLength, regionLength),
        BDM_CODE_OK);
}

TEST_F(TestDisk, test_bdm_non_divisor_device_count_creates_fresh_allocator)
{
    constexpr uint32_t bdmId = 707;
    TempBdmFile diskFile(bdmId);
    uint64_t regionOffset = 0;
    uint64_t regionLength = 0;

    ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, 0, 4, &regionOffset, &regionLength),
        BDM_CODE_OK);
    ASSERT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(0, 4), regionOffset, regionLength),
        BDM_CODE_OK);

    ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, 0, 3, &regionOffset, &regionLength),
        BDM_CODE_OK);
    EXPECT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(0, 3), regionOffset, regionLength),
        BDM_CODE_OK);
}

TEST_F(TestDisk, test_bdm_device_count_change_clears_old_headers_in_current_region)
{
    constexpr uint32_t bdmId = 705;
    TempBdmFile diskFile(bdmId);
    uint64_t oldOffsets[3] = {0};
    uint64_t oldRegionLength = 0;
    for (uint32_t deviceId = 0; deviceId < 3; deviceId++) {
        ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, deviceId, 8,
            &oldOffsets[deviceId], &oldRegionLength), BDM_CODE_OK);
        ASSERT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(deviceId, 8), oldOffsets[deviceId],
            oldRegionLength), BDM_CODE_OK);
    }
    ASSERT_FALSE(IsFileRangeZero(diskFile.path, oldOffsets[1], BDM_RESTORE_META_SIZE));
    ASSERT_FALSE(IsFileRangeZero(diskFile.path, oldOffsets[2], BDM_RESTORE_META_SIZE));

    uint64_t smallestRegionOffset = 0;
    uint64_t smallestRegionLength = 0;
    ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, 0,
        BDM_VIRTUAL_LAYOUT_SLOT_NUM, &smallestRegionOffset, &smallestRegionLength), BDM_CODE_OK);
    EXPECT_EQ(smallestRegionOffset, 0);
    ASSERT_TRUE(WriteFileMarker(diskFile.path, smallestRegionLength));
    ASSERT_TRUE(WriteFileMarker(diskFile.path, smallestRegionLength * 3));

    uint64_t currentRegionOffset = 0;
    uint64_t currentRegionLength = 0;
    ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, 0, 4, &currentRegionOffset,
        &currentRegionLength), BDM_CODE_OK);
    ASSERT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(0, 4), currentRegionOffset,
        currentRegionLength), BDM_CODE_OK);

    EXPECT_TRUE(IsFileRangeZero(diskFile.path, smallestRegionLength, BDM_RESTORE_META_SIZE));
    EXPECT_TRUE(IsFileRangeZero(diskFile.path, oldOffsets[1], BDM_RESTORE_META_SIZE));
    EXPECT_TRUE(IsFileRangeZero(diskFile.path, smallestRegionLength * 3, BDM_RESTORE_META_SIZE));
    EXPECT_FALSE(IsFileRangeZero(diskFile.path, oldOffsets[2], BDM_RESTORE_META_SIZE));
}

TEST_F(TestDisk, test_bdm_invalid_virtual_anchor_clears_all_header_slots_in_current_region)
{
    constexpr uint32_t bdmId = 706;
    TempBdmFile diskFile(bdmId);
    uint64_t oldOffsets[3] = {0};
    uint64_t oldRegionLength = 0;
    for (uint32_t deviceId = 0; deviceId < 3; deviceId++) {
        ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, deviceId, 8,
            &oldOffsets[deviceId], &oldRegionLength), BDM_CODE_OK);
        ASSERT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(deviceId, 8), oldOffsets[deviceId],
            oldRegionLength), BDM_CODE_OK);
    }

    uint64_t smallestRegionOffset = 0;
    uint64_t smallestRegionLength = 0;
    ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, 0,
        BDM_VIRTUAL_LAYOUT_SLOT_NUM, &smallestRegionOffset, &smallestRegionLength), BDM_CODE_OK);
    EXPECT_EQ(smallestRegionOffset, 0);
    ASSERT_TRUE(WriteFileMarker(diskFile.path, smallestRegionLength));
    ASSERT_TRUE(WriteFileMarker(diskFile.path, smallestRegionLength * 3));
    ASSERT_TRUE(ClearFileMagic(diskFile.path, oldOffsets[0]));

    uint64_t currentRegionOffset = 0;
    uint64_t currentRegionLength = 0;
    ASSERT_EQ(BdmCalculateVirtualRegion(TEST_BDM_META_DISK_LEN, NO_4194304, 0, 4, &currentRegionOffset,
        &currentRegionLength), BDM_CODE_OK);
    ASSERT_EQ(CreateBdmAndDestroy(diskFile.path, bdmId, BuildVirtualPad(0, 4), currentRegionOffset,
        currentRegionLength), BDM_CODE_OK);

    EXPECT_TRUE(IsFileRangeZero(diskFile.path, smallestRegionLength, BDM_RESTORE_META_SIZE));
    EXPECT_TRUE(IsFileRangeZero(diskFile.path, oldOffsets[1], BDM_RESTORE_META_SIZE));
    EXPECT_TRUE(IsFileRangeZero(diskFile.path, smallestRegionLength * 3, BDM_RESTORE_META_SIZE));
    EXPECT_FALSE(IsFileRangeZero(diskFile.path, oldOffsets[2], BDM_RESTORE_META_SIZE));
}

TEST_F(TestDisk, test_disk_get_bdm_status_case_return_fail)
{
    LOG_INFO("test_disk_get_bdm_status_case_return_fail");
    auto ret = BdmGetDiskStatus(BDM_MAX_NUM);
    EXPECT_EQ(ret, BDM_DISK_STATE_FAULT);

    ret = BdmGetDiskStatus(1);
    EXPECT_EQ(ret, BDM_DISK_STATE_NORMAL);
}

TEST_F(TestDisk, test_disk_get_capacity)
{
    LOG_INFO("test_disk_get_capacity");
    uint32_t bdmId = NO_1024;
    uint64_t totalCapacity = NO_4194304;
    uint64_t usedCapacity = 0;
    auto ret = BdmGetCapacity(bdmId, nullptr, &usedCapacity);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    ret = BdmGetCapacity(bdmId, &totalCapacity, &usedCapacity);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
}

TEST_F(TestDisk, test_disk_thread_destroy)
{
    LOG_INFO("test_disk_thread_destroy");
    BDM_BIND_CPU_S cpus;
    cpus.cpunum = 1;
    cpus.cpus[0] = 1;
    BdmThreadBindCPUs("", 16);
    BDM_THREAD_POOL_S *pool;
    pool = BdmThreadPoolCreate(16, 0, &cpus, "pool", nullptr);
    EXPECT_TRUE(pool == nullptr);
    pool = BdmThreadPoolCreate(1, NO_1024, &cpus, "pool", nullptr);
    EXPECT_TRUE(pool == nullptr);

    auto ret = BdmThreadPoolDestroy(pool, 1);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    int someData = 1;
    void* ctx = &someData;
    BDM_BATCH_CTX_S batchCtx;
    batchCtx.batchHandle = nullptr;
    batchCtx.batchCtx = ctx;
    pool = BdmThreadPoolCreate(1, NO_1024, &cpus, "pool", &batchCtx);
    EXPECT_FALSE(pool == nullptr);

    ret = BdmThreadPoolDestroy(pool, 1);
    EXPECT_EQ(ret, BDM_CODE_OK);

    pool = BdmThreadPoolCreate(1, NO_1024, &cpus, nullptr, &batchCtx);
    EXPECT_FALSE(pool != nullptr);

    pool = BdmThreadPoolCreate(1, NO_1024, &cpus, "pool", &batchCtx);
    ret = BdmThreadPoolDestroy(pool, 0);
    EXPECT_EQ(ret, BDM_CODE_OK);
}

TEST_F(TestDisk, test_disk_allocator_get_split_size_fail)
{
    LOG_INFO("test_disk_allocator_get_split_size_fail");
    uint64_t metaSize;
    uint64_t dataSize;
    auto ret = BdmAllocatorGetSplitSize(1001, 100, 1000, &metaSize, &dataSize);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    ret = BdmAllocatorGetSplitSize(50, 100, 200, &metaSize, &dataSize);
    EXPECT_EQ(ret, BDM_CODE_ERR);

    ret = BdmAllocatorGetSplitSize(0, 100, 1000, &metaSize, &dataSize);
    EXPECT_EQ(ret, BDM_CODE_OK);
}
