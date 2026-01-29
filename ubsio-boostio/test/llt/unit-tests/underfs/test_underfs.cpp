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

#include "test_underfs.h"
#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"
#include "rados/librados.h"
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

std::shared_ptr<FileSystem> g_hdfsInstancePtr = FileSystemFactory::CreateFileSystem(HDFS_SYSTEM);
std::shared_ptr<FileSystem> g_cephInstancePtr = FileSystemFactory::CreateFileSystem(CEPH_SYSTEM);

static int g_ptrStub;
static int RadosCreateStub(rados_t *pcluster, const char * const clusterName, const char * const name, uint64_t flags)
{
    *pcluster = static_cast<rados_t>(&g_ptrStub);
    return BIO_OK;
}

static void IoctxCdestroyStub(rados_ioctx_t io)
{
    return;
}

static void RadosShutdownStub(rados_t cluster)
{
    return;
}

static void RadosListCloseStub(rados_list_ctx_t ctx)
{
    return;
}

static int IoctxCreateStub(rados_t cluster, const char *poolName, rados_ioctx_t *ioctx)
{
    *ioctx = static_cast<rados_ioctx_t>(&g_ptrStub);
    return BIO_OK;
}

static int RadosStatStub(rados_ioctx_t io, const char *o, uint64_t *psize, time_t *pmtime)
{
    return BIO_OK;
}

void TestUnderFs::Stub()
{
    MOCKER(rados_create2).stubs().will(invoke(RadosCreateStub));
    MOCKER(rados_conf_read_file).stubs().will(returnValue(0));
    MOCKER(rados_connect).stubs().will(returnValue(0));
    MOCKER(rados_shutdown).stubs().will(invoke(RadosShutdownStub));
    MOCKER(rados_pool_lookup).stubs().will(returnValue(0));
    MOCKER(rados_ioctx_create).stubs().will(invoke(IoctxCreateStub));
    MOCKER(rados_ioctx_destroy).stubs().will(invoke(IoctxCdestroyStub));
    MOCKER(rados_read).stubs().will(returnValue(0));
    MOCKER(rados_write).stubs().will(returnValue(0));
    MOCKER(rados_stat).stubs().will(invoke(RadosStatStub));
    MOCKER(rados_remove).stubs().will(returnValue(0));
    MOCKER(rados_nobjects_list_next).stubs().will(returnValue(-ENOENT));
    MOCKER(rados_nobjects_list_open).stubs().will(returnValue(0));
    MOCKER(rados_nobjects_list_close).stubs().will(invoke(RadosListCloseStub));
}

TEST_F(TestUnderFs, test_underfs_ceph_init_creat_fail)
{
    LOG_INFO("test_underfs_ceph_init_creat_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_CEPH_CREAT_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_CEPH_CREAT_FAIL");
}

TEST_F(TestUnderFs, test_underfs_ceph_init_read_fail)
{
    LOG_INFO("test_underfs_ceph_init_read_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_CEPH_READ_FILE_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_CEPH_READ_FILE_FAIL");
}

TEST_F(TestUnderFs, test_underfs_ceph_init_connect_fail)
{
    LOG_INFO("test_underfs_ceph_init_connect_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_CEPH_CONNECT_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_CEPH_CONNECT_FAIL");
}

TEST_F(TestUnderFs, test_underfs_ceph_init_ioctx_creat_fail)
{
    LOG_INFO("test_underfs_ceph_init_ioctx_creat_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_CEPH_IOCTX_CREAT_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_CEPH_IOCTX_CREAT_FAIL");
}

TEST_F(TestUnderFs, test_underfs_ceph_init_return_ok)
{
    LOG_INFO("test_underfs_ceph_init_return_ok");
    auto ret = g_cephInstancePtr->Init();
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestUnderFs, test_underfs_ceph_put_return_fail)
{
    LOG_INFO("test_underfs_ceph_put_return_fail");
    const char *value = "value1";
    const size_t len = strlen(value);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_PUT", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Put(G_KEY, value, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_PUT");
}

TEST_F(TestUnderFs, test_underfs_ceph_put_return_ok)
{
    LOG_INFO("test_underfs_ceph_put_return_ok");
    const char *value = "value1";
    const size_t len = strlen(value);

    auto ret = g_cephInstancePtr->Put(G_KEY, value, len);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestUnderFs, test_underfs_ceph_get_return_fail)
{
    LOG_INFO("test_underfs_ceph_get_return_fail");
    char value[NO_256];
    const size_t len = NO_256;
    const uint64_t off = 0;

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_CEPH_GET_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Get(G_KEY, value, len, off);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_CEPH_GET_FAIL");

    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_GET", 0, 1, userParam);
    ret = g_cephInstancePtr->Get(G_KEY, value, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_GET");
}

TEST_F(TestUnderFs, test_underfs_ceph_get_return_ok)
{
    LOG_INFO("test_underfs_ceph_get_return_ok");
    char value[NO_256];
    const size_t len = NO_256;
    const uint64_t off = 0;

    auto ret = g_cephInstancePtr->Get(G_KEY, value, len, off);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestUnderFs, test_underfs_ceph_delete_return_fail)
{
    LOG_INFO("test_underfs_ceph_delete_return_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_CEPH_DELETE_NOT_EXIST", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Delete(G_KEY);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_CEPH_DELETE_NOT_EXIST");

    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_DELETE", 0, 1, userParam);
    ret = g_cephInstancePtr->Delete(G_KEY);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_DELETE");
}

TEST_F(TestUnderFs, test_underfs_ceph_delete_return_ok)
{
    LOG_INFO("test_underfs_ceph_delete_return_ok");
    auto ret = g_cephInstancePtr->Delete(G_KEY);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestUnderFs, test_underfs_ceph_stat_return_fail)
{
    LOG_INFO("test_underfs_ceph_stat_return_fail");
    FileSystem::ObjStat stat;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_CEPH_STAT_NOT_EXIST", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Stat(G_KEY, stat);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_CEPH_STAT_NOT_EXIST");

    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_STAT_SIZE", 0, 1, userParam);
    ret = g_cephInstancePtr->Stat(G_KEY, stat);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_STAT_SIZE");

    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_STAT", 0, 1, userParam);
    ret = g_cephInstancePtr->Stat(G_KEY, stat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_STAT");
}

TEST_F(TestUnderFs, test_underfs_ceph_stat_return_ok)
{
    LOG_INFO("test_underfs_ceph_stat_return_fail");
    FileSystem::ObjStat stat;
    auto ret = g_cephInstancePtr->Stat(G_KEY, stat);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestUnderFs, test_underfs_ceph_list_return_fail)
{
    LOG_INFO("test_underfs_ceph_list_return_fail");
    const char *prefix = "key";
    std::unordered_map<std::string, CephSystem::ObjStat> objStat;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SERVER_UNDERFS_LIST", 0, 1, userParam);
    auto ret = g_cephInstancePtr->List(prefix, objStat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_UNDERFS_LIST");
}

TEST_F(TestUnderFs, test_underfs_ceph_list_return_OK)
{
    LOG_INFO("test_underfs_ceph_list_return_OK");
    const char *prefix = "key";
    std::unordered_map<std::string, CephSystem::ObjStat> objStat;
    auto ret = g_cephInstancePtr->List(prefix, objStat);
    EXPECT_EQ(ret, BIO_OK);
    g_cephInstancePtr->Stop();
}

TEST_F(TestUnderFs, test_underfs_hdfs_init_return_fail)
{
    LOG_INFO("test_underfs_hdfs_init_return_fail");
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_HDFS_CONNECT_FAIL", 0, 1, userParam);
    auto ret = g_hdfsInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_HDFS_CONNECT_FAIL");
}

TEST_F(TestUnderFs, test_underfs_hdfs_put_return_fail)
{
    LOG_INFO("test_underfs_hdfs_put_return_fail");
    const char *value = nullptr;
    size_t len = 0;
    auto ret = g_hdfsInstancePtr->Put(G_KEY, value, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);

    const char *value2 = "value2";
    ret = g_hdfsInstancePtr->Put(G_KEY, value2, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);

    len = strlen(value2);
    const char *key = "/key111";
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_SET_BUILDER_NULL", 0, 1, userParam);
    ret = g_hdfsInstancePtr->Put(key, value2, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_SET_BUILDER_NULL");
}

TEST_F(TestUnderFs, test_underfs_hdfs_get_return_fail)
{
    LOG_INFO("test_underfs_hdfs_get_return_fail");
    char *value = nullptr;
    size_t len = 0;
    const uint64_t off = 0;
    auto ret = g_hdfsInstancePtr->Get(G_KEY, value, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);

    char value2[NO_256];
    ret = g_hdfsInstancePtr->Get(G_KEY, value2, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);

    len = NO_256;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "UNDERFS_SET_BUILDER_NULL", 0, 1, userParam);
    ret = g_hdfsInstancePtr->Get(G_KEY, value2, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_SET_BUILDER_NULL");
}

TEST_F(TestUnderFs, test_underfs_hdfs_delete_return_fail)
{
    LOG_INFO("test_underfs_hdfs_delete_return_fail");
    auto ret = g_hdfsInstancePtr->Delete(G_INVALID_KEY);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
}

TEST_F(TestUnderFs, test_underfs_hdfs_stat_return_fail)
{
    LOG_INFO("test_underfs_hdfs_stat_return_fail");
    FileSystem::ObjStat objStat;
    auto ret = g_hdfsInstancePtr->Stat(G_INVALID_KEY, objStat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
}

TEST_F(TestUnderFs, test_underfs_hdfs_list_return_fail)
{
    LOG_INFO("test_underfs_hdfs_list_return_fail");
    const char *prefix = nullptr;
    std::unordered_map<std::string, FileSystem::ObjStat> objStat;
    auto ret = g_hdfsInstancePtr->List(prefix, objStat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
}