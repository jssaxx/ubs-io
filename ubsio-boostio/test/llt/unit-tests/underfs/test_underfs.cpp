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
#include "bio_err.h"
#include "bio_file_util.h"
#include "bio_log.h"
#include "bio_types.h"
#include "ceph_system.h"
#include "gtest/gtest.h"
#include "local_system.h"
#define private public
#include "hdfs_system.h"
#undef private
#include "file_system_factory.h"
#include "rados/librados.h"
#include "tracepoint.h"

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

std::shared_ptr<FileSystem> g_hdfsInstancePtr = std::make_shared<HdfsSystem>();
std::shared_ptr<FileSystem> g_cephInstancePtr = std::make_shared<CephSystem>();

static int g_ptrStub;
static int g_hdfsFsStub;
static int g_hdfsBuilderStub;
static int g_hdfsStreamBuilderStub;
static int g_hdfsFileStub;
static int g_hdfsCreateDirRet = BIO_OK;
static int g_hdfsDeleteRet = BIO_OK;
static int g_hdfsExistsRet = BIO_OK;
static int g_hdfsWriteRet = BIO_OK;
static int g_hdfsPreadRet = BIO_OK;
static int g_hdfsFlushRet = BIO_OK;
static bool g_hdfsBuildFile = true;
static bool g_hdfsReturnFileInfo = true;
static bool g_hdfsReturnList = true;
static tObjectKind g_hdfsPathKind = kObjectKindFile;
static tOffset g_hdfsInfoSize = 8;
static hdfsFileInfo g_hdfsInfo[2];
static char g_hdfsFileName[] = "/work/file1";
static char g_hdfsDirName[] = "/work/dir1";

namespace ock {
namespace bio {
BResult ParsePath(const char *path, char *parentDir, size_t parentDirSize);
std::pair<std::string, std::string> ParseIpPort(const std::string &host);
bool IsValidPort(std::string &port);
bool IsValidIP(const std::string &ip);
bool IsValidWorkingPath(std::string &path);
bool IsValidHdfsConfig(std::pair<std::string, std::string> &ipPort, std::string &workingPath);
std::string GetParentDirectory(const std::string path);
std::string GetFileNameFromHdfsPath(const std::string path);
} // namespace bio
} // namespace ock

static int RadosCreateStub(rados_t *pcluster, const char *const clusterName, const char *const name, uint64_t flags)
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

static int HdfsCreateDirStub(hdfsFS fs, const char *path)
{
    return g_hdfsCreateDirRet;
}

static int HdfsSetWorkingDirStub(hdfsFS fs, const char *path)
{
    return BIO_OK;
}

static int HdfsDisconnectStub(hdfsFS fs)
{
    return BIO_OK;
}

static hdfsStreamBuilder *HdfsStreamBuilderAllocStub(hdfsFS fs, const char *path, int flags)
{
    return reinterpret_cast<hdfsStreamBuilder *>(&g_hdfsStreamBuilderStub);
}

static hdfsFile HdfsStreamBuilderBuildStub(hdfsStreamBuilder *builder)
{
    return g_hdfsBuildFile ? reinterpret_cast<hdfsFile>(&g_hdfsFileStub) : nullptr;
}

static int32_t HdfsWriteStub(hdfsFS fs, hdfsFile file, const void *buffer, int32_t length)
{
    return g_hdfsWriteRet == BIO_OK ? length : -1;
}

static int HdfsFlushStub(hdfsFS fs, hdfsFile file)
{
    return g_hdfsFlushRet;
}

static int HdfsCloseFileStub(hdfsFS fs, hdfsFile file)
{
    return BIO_OK;
}

static int32_t HdfsPreadStub(hdfsFS fs, hdfsFile file, int64_t position, void *buffer, int32_t length)
{
    if (g_hdfsPreadRet != BIO_OK) {
        return -1;
    }
    std::memset(buffer, 'a', length);
    return length;
}

static int HdfsDeleteStub(hdfsFS fs, const char *path, int recursive)
{
    return g_hdfsDeleteRet;
}

static int HdfsExistsStub(hdfsFS fs, const char *path)
{
    return g_hdfsExistsRet;
}

static hdfsFileInfo *HdfsGetPathInfoStub(hdfsFS fs, const char *path)
{
    if (!g_hdfsReturnFileInfo) {
        return nullptr;
    }
    g_hdfsInfo[0].mKind = g_hdfsPathKind;
    g_hdfsInfo[0].mName = g_hdfsFileName;
    g_hdfsInfo[0].mSize = g_hdfsInfoSize;
    g_hdfsInfo[0].mLastMod = 9;
    return &g_hdfsInfo[0];
}

static void HdfsFreeFileInfoStub(hdfsFileInfo *info, int numEntries) {}

static hdfsFileInfo *HdfsListDirectoryStub(hdfsFS fs, const char *path, int *numEntries)
{
    if (!g_hdfsReturnList) {
        *numEntries = 0;
        errno = EIO;
        return nullptr;
    }
    *numEntries = 2;
    g_hdfsInfo[0].mKind = kObjectKindFile;
    g_hdfsInfo[0].mName = g_hdfsFileName;
    g_hdfsInfo[0].mSize = 10;
    g_hdfsInfo[0].mLastMod = 11;
    g_hdfsInfo[1].mKind = kObjectKindDirectory;
    g_hdfsInfo[1].mName = g_hdfsDirName;
    g_hdfsInfo[1].mSize = 0;
    g_hdfsInfo[1].mLastMod = 12;
    return g_hdfsInfo;
}

static hdfsBuilder *HdfsNewBuilderStub()
{
    return reinterpret_cast<hdfsBuilder *>(&g_hdfsBuilderStub);
}

static void HdfsBuilderSetNameNodeStub(hdfsBuilder *builder, const char *nameNode) {}

static void HdfsBuilderSetNameNodePortStub(hdfsBuilder *builder, uint16_t port) {}

static hdfsFS HdfsBuilderConnectStub(hdfsBuilder *builder)
{
    return reinterpret_cast<hdfsFS>(&g_hdfsFsStub);
}

static std::shared_ptr<HdfsSystem> Hdfs()
{
    return std::static_pointer_cast<HdfsSystem>(g_hdfsInstancePtr);
}

static void ResetHdfsStubs()
{
    auto hdfs = Hdfs();
    hdfs->mHdfsFs = reinterpret_cast<hdfsFS>(&g_hdfsFsStub);
    hdfs->mHdfsWorkingPath = "/work";
    hdfs->mNameNodeIp = "127.0.0.1";
    hdfs->mNameNodePort = 9000;
    hdfs->createDirOp = HdfsCreateDirStub;
    hdfs->setWorkingDirOp = HdfsSetWorkingDirStub;
    hdfs->disconnectOp = HdfsDisconnectStub;
    hdfs->streamBuilderAllocOp = HdfsStreamBuilderAllocStub;
    hdfs->streamBuilderBuildOp = HdfsStreamBuilderBuildStub;
    hdfs->writeOp = HdfsWriteStub;
    hdfs->flushOp = HdfsFlushStub;
    hdfs->closeFileOp = HdfsCloseFileStub;
    hdfs->preadOp = HdfsPreadStub;
    hdfs->deleteOp = HdfsDeleteStub;
    hdfs->existsOp = HdfsExistsStub;
    hdfs->getPathInfoOp = HdfsGetPathInfoStub;
    hdfs->freeFileInfoOp = HdfsFreeFileInfoStub;
    hdfs->listDirectoryOp = HdfsListDirectoryStub;
    hdfs->newBuilderOp = HdfsNewBuilderStub;
    hdfs->builderSetNameNodeOp = HdfsBuilderSetNameNodeStub;
    hdfs->builderSetNameNodePortOp = HdfsBuilderSetNameNodePortStub;
    hdfs->builderConnectOp = HdfsBuilderConnectStub;
    g_hdfsCreateDirRet = BIO_OK;
    g_hdfsDeleteRet = BIO_OK;
    g_hdfsExistsRet = BIO_OK;
    g_hdfsWriteRet = BIO_OK;
    g_hdfsPreadRet = BIO_OK;
    g_hdfsFlushRet = BIO_OK;
    g_hdfsBuildFile = true;
    g_hdfsReturnFileInfo = true;
    g_hdfsReturnList = true;
    g_hdfsPathKind = kObjectKindFile;
    g_hdfsInfoSize = 8;
    errno = BIO_OK;
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_CEPH_CREAT_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "UNDERFS_CEPH_CREAT_FAIL");
}

TEST_F(TestUnderFs, test_underfs_ceph_init_read_fail)
{
    LOG_INFO("test_underfs_ceph_init_read_fail");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_CEPH_READ_FILE_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "UNDERFS_CEPH_READ_FILE_FAIL");
}

TEST_F(TestUnderFs, test_underfs_ceph_init_connect_fail)
{
    LOG_INFO("test_underfs_ceph_init_connect_fail");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_CEPH_CONNECT_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "UNDERFS_CEPH_CONNECT_FAIL");
}

TEST_F(TestUnderFs, test_underfs_ceph_init_ioctx_creat_fail)
{
    LOG_INFO("test_underfs_ceph_init_ioctx_creat_fail");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_CEPH_IOCTX_CREAT_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "UNDERFS_CEPH_IOCTX_CREAT_FAIL");
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

    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "SERVER_UNDERFS_PUT", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Put(G_KEY, value, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "SERVER_UNDERFS_PUT");
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

    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_CEPH_GET_FAIL", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Get(G_KEY, value, len, off);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    BioHvsDeactiveTracePoint(0, "UNDERFS_CEPH_GET_FAIL");

    BioHvsActiveTracePoint(0, "SERVER_UNDERFS_GET", 0, 1, userParam);
    ret = g_cephInstancePtr->Get(G_KEY, value, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "SERVER_UNDERFS_GET");
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_CEPH_DELETE_NOT_EXIST", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Delete(G_KEY);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    BioHvsDeactiveTracePoint(0, "UNDERFS_CEPH_DELETE_NOT_EXIST");

    BioHvsActiveTracePoint(0, "SERVER_UNDERFS_DELETE", 0, 1, userParam);
    ret = g_cephInstancePtr->Delete(G_KEY);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "SERVER_UNDERFS_DELETE");
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_CEPH_STAT_NOT_EXIST", 0, 1, userParam);
    auto ret = g_cephInstancePtr->Stat(G_KEY, stat);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    BioHvsDeactiveTracePoint(0, "UNDERFS_CEPH_STAT_NOT_EXIST");

    BioHvsActiveTracePoint(0, "SERVER_UNDERFS_STAT_SIZE", 0, 1, userParam);
    ret = g_cephInstancePtr->Stat(G_KEY, stat);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
    BioHvsDeactiveTracePoint(0, "SERVER_UNDERFS_STAT_SIZE");

    BioHvsActiveTracePoint(0, "SERVER_UNDERFS_STAT", 0, 1, userParam);
    ret = g_cephInstancePtr->Stat(G_KEY, stat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "SERVER_UNDERFS_STAT");
}

TEST_F(TestUnderFs, test_underfs_ceph_stat_return_ok)
{
    LOG_INFO("test_underfs_ceph_stat_return_ok");
    FileSystem::ObjStat stat;
    stat.size = 1;
    auto ret = g_cephInstancePtr->Stat(G_KEY, stat);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestUnderFs, test_underfs_ceph_list_return_fail)
{
    LOG_INFO("test_underfs_ceph_list_return_fail");
    const char *prefix = "key";
    std::unordered_map<std::string, CephSystem::ObjStat> objStat;
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "SERVER_UNDERFS_LIST", 0, 1, userParam);
    auto ret = g_cephInstancePtr->List(prefix, objStat);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "SERVER_UNDERFS_LIST");
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_HDFS_CONNECT_FAIL", 0, 1, userParam);
    auto ret = g_hdfsInstancePtr->Init();
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "UNDERFS_HDFS_CONNECT_FAIL");
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_SET_BUILDER_NULL", 0, 1, userParam);
    ret = g_hdfsInstancePtr->Put(key, value2, len);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "UNDERFS_SET_BUILDER_NULL");
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
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "UNDERFS_SET_BUILDER_NULL", 0, 1, userParam);
    ret = g_hdfsInstancePtr->Get(G_KEY, value2, len, off);
    EXPECT_EQ(ret, BIO_UFS_IOERR);
    BioHvsDeactiveTracePoint(0, "UNDERFS_SET_BUILDER_NULL");
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

TEST_F(TestUnderFs, test_hdfs_helper_paths_and_config)
{
    char parent[NO_256] = {};
    EXPECT_EQ(ParsePath("dir/file", parent, sizeof(parent)), BIO_OK);
    EXPECT_STREQ(parent, "dir");

    EXPECT_EQ(ParsePath("file", parent, sizeof(parent)), BIO_OK);
    EXPECT_STREQ(parent, "");

    auto ipPort = ParseIpPort("127.0.0.1:9000");
    EXPECT_EQ(ipPort.first, "127.0.0.1");
    EXPECT_EQ(ipPort.second, "9000");
    EXPECT_TRUE(ParseIpPort("127.0.0.1").first.empty());

    std::string port = "65535";
    EXPECT_TRUE(IsValidPort(port));
    port = "65536";
    EXPECT_FALSE(IsValidPort(port));

    EXPECT_TRUE(IsValidIP("127.0.0.1"));
    EXPECT_TRUE(IsValidIP("default"));
    EXPECT_FALSE(IsValidIP("999.0.0.1"));

    std::string workingPath = "/hdfs/path";
    EXPECT_TRUE(IsValidWorkingPath(workingPath));
    std::string badPath = "relative/path";
    EXPECT_FALSE(IsValidWorkingPath(badPath));
    EXPECT_TRUE(IsValidHdfsConfig(ipPort, workingPath));

    ipPort.first = "999.0.0.1";
    EXPECT_FALSE(IsValidHdfsConfig(ipPort, workingPath));
    EXPECT_EQ(GetParentDirectory("dir/sub/file"), "dir/sub");
    EXPECT_EQ(GetParentDirectory("file"), "");
    EXPECT_EQ(GetFileNameFromHdfsPath("/work/file"), "file");
    EXPECT_EQ(GetFileNameFromHdfsPath("file"), "file");
}

TEST_F(TestUnderFs, test_hdfs_fake_ops_return_ok)
{
    ResetHdfsStubs();
    g_hdfsExistsRet = BIO_ERR;
    EXPECT_EQ(Hdfs()->CreateDirectory("dir/sub"), BIO_OK);
    g_hdfsExistsRet = BIO_OK;

    const char *value = "value";
    EXPECT_EQ(g_hdfsInstancePtr->Put("dir/file", value, strlen(value)), BIO_OK);

    char buf[NO_32] = {};
    EXPECT_EQ(g_hdfsInstancePtr->Get("dir/file", buf, sizeof(buf), 0), BIO_OK);
    EXPECT_EQ(buf[0], 'a');

    FileSystem::ObjStat stat;
    EXPECT_EQ(g_hdfsInstancePtr->Stat("dir/file", stat), BIO_OK);
    EXPECT_EQ(stat.size, 8UL);

    std::unordered_map<std::string, FileSystem::ObjStat> objStat;
    EXPECT_EQ(g_hdfsInstancePtr->List("file", objStat), BIO_OK);
    EXPECT_EQ(objStat.size(), 1UL);

    EXPECT_EQ(g_hdfsInstancePtr->Delete("dir/file"), BIO_OK);
    g_hdfsInstancePtr->Stop();
}

TEST_F(TestUnderFs, test_hdfs_fake_ops_error_paths)
{
    ResetHdfsStubs();
    g_hdfsCreateDirRet = BIO_ERR;
    g_hdfsExistsRet = BIO_ERR;
    EXPECT_EQ(Hdfs()->CreateDirectory("dir/sub"), BIO_UFS_IOERR);

    ResetHdfsStubs();
    g_hdfsBuildFile = false;
    const char *value = "value";
    EXPECT_EQ(g_hdfsInstancePtr->Put("dir/file", value, strlen(value)), BIO_UFS_IOERR);

    ResetHdfsStubs();
    g_hdfsWriteRet = BIO_ERR;
    EXPECT_EQ(g_hdfsInstancePtr->Put("dir/file", value, strlen(value)), BIO_UFS_IOERR);

    ResetHdfsStubs();
    g_hdfsBuildFile = false;
    char buf[NO_32] = {};
    EXPECT_EQ(g_hdfsInstancePtr->Get("dir/file", buf, sizeof(buf), 0), BIO_NOT_EXISTS);

    ResetHdfsStubs();
    g_hdfsPreadRet = BIO_ERR;
    EXPECT_EQ(g_hdfsInstancePtr->Get("dir/file", buf, sizeof(buf), 0), BIO_UFS_IOERR);

    ResetHdfsStubs();
    g_hdfsReturnFileInfo = false;
    FileSystem::ObjStat missingStat;
    EXPECT_EQ(g_hdfsInstancePtr->Stat("dir/file", missingStat), BIO_NOT_EXISTS);

    ResetHdfsStubs();
    g_hdfsInfoSize = IO_MAX_LEN + 1;
    FileSystem::ObjStat stat;
    EXPECT_EQ(g_hdfsInstancePtr->Stat("dir/file", stat), BIO_NOT_EXISTS);

    ResetHdfsStubs();
    g_hdfsDeleteRet = BIO_ERR;
    g_hdfsExistsRet = BIO_ERR;
    EXPECT_EQ(g_hdfsInstancePtr->Delete("dir/file"), BIO_NOT_EXISTS);

    ResetHdfsStubs();
    g_hdfsDeleteRet = BIO_ERR;
    g_hdfsExistsRet = BIO_OK;
    EXPECT_EQ(g_hdfsInstancePtr->Delete("dir/file"), BIO_UFS_IOERR);

    ResetHdfsStubs();
    g_hdfsReturnList = false;
    g_hdfsPathKind = kObjectKindDirectory;
    std::unordered_map<std::string, FileSystem::ObjStat> objStat;
    EXPECT_EQ(g_hdfsInstancePtr->List("dir", objStat), BIO_UFS_IOERR);
}

TEST_F(TestUnderFs, test_local_system_and_factory_paths)
{
    FileUtil::RemoveDirRecursive("./ceph");
    auto local = FileSystemFactory::CreateFileSystem(LOCAL_SYSTEM);
    auto ceph = FileSystemFactory::CreateFileSystem(CEPH_SYSTEM);
    auto hdfs = FileSystemFactory::CreateFileSystem(HDFS_SYSTEM);
    auto fallback = FileSystemFactory::CreateFileSystem("unknown");

    EXPECT_NE(std::dynamic_pointer_cast<LocalSystem>(local), nullptr);
    EXPECT_NE(std::dynamic_pointer_cast<CephSystem>(ceph), nullptr);
    EXPECT_NE(std::dynamic_pointer_cast<HdfsSystem>(hdfs), nullptr);
    EXPECT_NE(std::dynamic_pointer_cast<LocalSystem>(fallback), nullptr);

    EXPECT_EQ(local->Init(), BIO_OK);
    EXPECT_EQ(local->Init(), BIO_OK);

    const char *value = "local-data";
    EXPECT_EQ(local->Put("local_dir/file", value, strlen(value)), BIO_OK);

    char buf[NO_32] = {};
    EXPECT_EQ(local->Get("local_dir/file", buf, strlen(value), 0), BIO_OK);
    EXPECT_EQ(std::string(buf, strlen(value)), value);

    FileSystem::ObjStat stat;
    EXPECT_EQ(local->Stat("local_dir/file", stat), BIO_OK);
    EXPECT_EQ(stat.size, strlen(value));

    std::unordered_map<std::string, FileSystem::ObjStat> objStat;
    EXPECT_EQ(local->List("local", objStat), BIO_OK);
    EXPECT_FALSE(objStat.empty());

    EXPECT_EQ(local->Delete("local_dir/file"), BIO_OK);
    EXPECT_EQ(local->Delete("local_dir/file"), BIO_NOT_EXISTS);
    EXPECT_EQ(local->Get("local_dir/file", buf, sizeof(buf), 0), BIO_NOT_EXISTS);
    EXPECT_EQ(local->Stat("local_dir/file", stat), BIO_NOT_EXISTS);
    local->Stop();
    FileUtil::RemoveDirRecursive("./ceph");
}
