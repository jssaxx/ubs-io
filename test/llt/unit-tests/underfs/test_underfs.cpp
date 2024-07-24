/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "test_underfs.h"
#include "gtest/gtest.h"
#include "bio_err.h"
#include "bio_types.h"
#include "tracepoint.h"
#include "hdfs_system.h"
#include "ceph_system.h"
#include "bio_log.h"
#include "file_system_factory.h"

using namespace ock::bio;

bool TestUnderFs::gSetup = false;

const char *G_KEY = "key111";
const char *G_INVALID_KEY = nullptr;
void TestUnderFs::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestUnderFs::TearDown()
{
    return;
}

std::shared_ptr<FileSystem> HDFS_INSTANCE_PTR = FileSystemFactory::CreateFileSystem(HDFS_SYSTEM);
std::shared_ptr<FileSystem> CEPH_INSTANCE_PTR = FileSystemFactory::CreateFileSystem(CEPH_SYSTEM);

TEST_F(TestUnderFs, test_underfs_ceph_init_return_fail)
{
    LOG_INFO("test_underfs_ceph_init_return_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_CEPH_CREAT_FAIL", 0, 1, userParam);
    auto ret = CEPH_INSTANCE_PTR->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_CEPH_CREAT_FAIL");
}

TEST_F(TestUnderFs, test_underfs_ceph_put_return_fail)
{
    LOG_INFO("test_underfs_ceph_put_return_fail");
    const char *value = "value1";
    const size_t len = strlen(value);
    auto ret = CEPH_INSTANCE_PTR->Put(G_KEY, value, len);
    EXPECT_EQ(ret, BIO_NOT_READY);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_SET_IOCTX_TRUE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_PUT", 0, 1, userParam);
    ret = CEPH_INSTANCE_PTR->Put(G_KEY, value, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_PUT");
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_SET_IOCTX_TRUE");
}

TEST_F(TestUnderFs, test_underfs_ceph_get_return_fail)
{
    LOG_INFO("test_underfs_ceph_get_return_fail");
    char value[NO_256];
    const size_t len = NO_256;
    const uint64_t off = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_SET_IOCTX_TRUE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_GET", 0, 1, userParam);
    auto ret = CEPH_INSTANCE_PTR->Get(G_KEY, value, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_GET");
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_SET_IOCTX_TRUE");
}

TEST_F(TestUnderFs, test_underfs_ceph_delete_return_fail)
{
    LOG_INFO("test_underfs_ceph_delete_return_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_SET_IOCTX_TRUE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_DELETE", 0, 1, userParam);
    auto ret = CEPH_INSTANCE_PTR->Delete(G_KEY);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_DELETE");
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_SET_IOCTX_TRUE");
}

TEST_F(TestUnderFs, test_underfs_ceph_stat_return_fail)
{
    LOG_INFO("test_underfs_ceph_stat_return_fail");
    FileSystem::ObjStat stat;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_SET_IOCTX_TRUE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_STAT", 0, 1, userParam);
    auto ret = CEPH_INSTANCE_PTR->Stat(G_KEY, stat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_STAT");
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_SET_IOCTX_TRUE");
}

TEST_F(TestUnderFs, test_underfs_ceph_list_return_fail)
{
    LOG_INFO("test_underfs_ceph_list_return_fail");
    const char *prefix = "key";
    std::unordered_map<std::string, CephSystem::ObjStat> objStat;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_SET_IOCTX_TRUE", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_LIST", 0, 1, userParam);
    auto ret = CEPH_INSTANCE_PTR->List(prefix, objStat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_LIST");
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_SET_IOCTX_TRUE");
}

TEST_F(TestUnderFs, test_underfs_hdfs_put_return_fail)
{
    LOG_INFO("test_underfs_hdfs_put_return_fail");
    const char *value = nullptr;
    size_t len = 0;
    auto ret = HDFS_INSTANCE_PTR->Put(G_KEY, value, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);

    const char *value2 = "value2";
    ret = HDFS_INSTANCE_PTR->Put(G_KEY, value2, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);

    len = strlen(value2);
    const char *key = "/key111";
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_SET_BUILDER_NULL", 0, 1, userParam);
    ret = HDFS_INSTANCE_PTR->Put(key, value2, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_SET_BUILDER_NULL");
}

TEST_F(TestUnderFs, test_underfs_hdfs_get_return_fail)
{
    LOG_INFO("test_underfs_hdfs_get_return_fail");
    char *value = nullptr;
    size_t len = 0;
    const uint64_t off = 0;
    auto ret = HDFS_INSTANCE_PTR->Get(G_KEY, value, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);

    char value2[NO_256];
    ret = HDFS_INSTANCE_PTR->Get(G_KEY, value2, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);

    len = NO_256;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_SET_BUILDER_NULL", 0, 1, userParam);
    ret = HDFS_INSTANCE_PTR->Get(G_KEY, value2, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_SET_BUILDER_NULL");
}

TEST_F(TestUnderFs, test_underfs_hdfs_delete_return_fail)
{
    LOG_INFO("test_underfs_hdfs_delete_return_fail");
    auto ret = HDFS_INSTANCE_PTR->Delete(G_INVALID_KEY);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
}

TEST_F(TestUnderFs, test_underfs_hdfs_stat_return_fail)
{
    LOG_INFO("test_underfs_hdfs_stat_return_fail");
    FileSystem::ObjStat objStat;
    auto ret = HDFS_INSTANCE_PTR->Stat(G_INVALID_KEY, objStat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
}

TEST_F(TestUnderFs, test_underfs_hdfs_list_return_fail)
{
    LOG_INFO("test_underfs_hdfs_list_return_fail");
    const char *prefix = nullptr;
    std::unordered_map<std::string, FileSystem::ObjStat> objStat;
    auto ret = HDFS_INSTANCE_PTR->List(prefix, objStat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
}