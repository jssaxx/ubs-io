/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <algorithm>

#include "local_system_test_fixture.h"

using namespace ock::bio;
using namespace local_system_test;

namespace {

ssize_t FailedWrite(int, const void *, size_t, off_t)
{
    errno = ENOSPC;
    return -1;
}

TEST_F(TestSharedSystem, factory_dispatches_shared_operations_through_base_interfaces)
{
    MockFileSystemCapacity(1);
    UnderFsConfig::Initialize(config);
    auto local = FileSystemFactory::CreateFileSystem(BIO_UNDERFS_LOCAL);
    EXPECT_NE(std::dynamic_pointer_cast<LocalSystem>(local), nullptr);
    EXPECT_EQ(std::dynamic_pointer_cast<SharedSystem>(local), nullptr);
    auto fileSystem = FileSystemFactory::CreateFileSystem(BIO_UNDERFS_SHARED);
    ASSERT_NE(std::dynamic_pointer_cast<SharedSystem>(fileSystem), nullptr);
    ASSERT_EQ(fileSystem->Init(), BIO_OK);
    ASSERT_EQ(fileSystem->Init(), BIO_OK);
    ASSERT_EQ(fileSystem->Put("scalar", "value", 5), BIO_OK);
    auto posix = std::dynamic_pointer_cast<LocalSystem>(fileSystem);
    ASSERT_NE(posix, nullptr);
    ASSERT_EQ(fileSystem->Put("shared", "shared", 6), BIO_OK);
    FileSystem::ObjStat status{};
    ASSERT_EQ(fileSystem->Stat("shared", status), BIO_OK);
    EXPECT_EQ(status.size, 6U);
    Start();
    ASSERT_EQ(system.Put("shared", "updated", 7), BIO_OK);
    char value[7]{};
    ASSERT_EQ(fileSystem->Get("shared", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "updated");
    ASSERT_EQ(fileSystem->Stat("shared", status), BIO_OK);
    EXPECT_EQ(status.size, 7U);
    ASSERT_EQ(fileSystem->Delete("scalar"), BIO_OK);
    EXPECT_EQ(fileSystem->Exist("scalar"), BIO_NOT_EXISTS);
    fileSystem->Stop();
    ASSERT_EQ(fileSystem->Init(), BIO_OK);
    size_t realLen = 0;
    ASSERT_EQ(fileSystem->GetWithRealLen("shared", value, sizeof(value), 2, realLen), BIO_OK);
    EXPECT_EQ(std::string(value, realLen), "dated");

    EXPECT_EQ(posix->mEvictionPolicy, nullptr);
    EXPECT_EQ(posix->mLeaseFd, -1);
    EXPECT_FALSE(posix->mEvictionThread.joinable());
    fileSystem->Stop();
}

TEST_F(TestSharedSystem, shared_preserves_directory_and_ignores_capacity)
{
    MockFileSystemCapacity(1);
    config.localConfig.slotIndex = 1;
    config.localConfig.deviceCount = 2;
    Start();
    EXPECT_EQ(system.mRootPath, root + "/v1");
    EXPECT_NE(access((root + "/slot_1").c_str(), F_OK), 0);
    ASSERT_FALSE(system.mEvictionThread.joinable());
    EXPECT_EQ(system.mRootFd, -1);
    EXPECT_EQ(system.mLeaseFd, -1);
    EXPECT_TRUE(Usage().files == 0);
    EXPECT_EQ(system.mEvictionPolicy, nullptr);
    EXPECT_EQ(system.mCapacityBytes, 0U);
    Write("shared-key", 'a', 1);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("shared-key", path), BIO_OK);
    struct stat fileStat{};
    ASSERT_EQ(stat(path.c_str(), &fileStat), 0);
    EXPECT_EQ(fileStat.st_size, 4097);
    std::ifstream file(path, std::ios::binary);
    std::vector<char> stored(4097);
    ASSERT_TRUE(file.read(stored.data(), stored.size()));
    EXPECT_EQ(stored, std::vector<char>(4097, 'a'));
    SharedSystem peer;
    ASSERT_EQ(peer.Init(), BIO_OK);
    std::vector<char> data(4097);
    ASSERT_EQ(peer.Get("shared-key", data.data(), data.size(), 0), BIO_OK);
    EXPECT_EQ(data, std::vector<char>(4097, 'a'));
    EXPECT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.Exist("shared-key"), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_TRUE(events.empty());
    peer.Stop();
}

TEST_F(TestSharedSystem, shared_reads_existing_raw_values_without_key_metadata)
{
    Start();
    std::string hash;
    ASSERT_EQ(system.BuildHash("existing-key", hash), BIO_OK);
    std::string leaf;
    bool outOfSpace = false;
    ASSERT_EQ(system.EnsureLeafDirectory(hash, leaf, outOfSpace), BIO_OK);
    std::string path;
    path = system.BuildFilePath(leaf, hash);
    // A raw value may start with a local format magic; shared never interprets it.
    const std::string value = "UBSIOKV1/raw-value";
    {
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.write(value.data(), value.size()));
    }
    char partial[32]{};
    size_t realLen = 0;
    ASSERT_EQ(system.GetWithRealLen("existing-key", partial, sizeof(partial), 5, realLen), BIO_OK);
    EXPECT_EQ(realLen, value.size() - 5);
    EXPECT_EQ(std::string(partial, realLen), value.substr(5));
    FileSystem::ObjStat status{};
    ASSERT_EQ(system.Stat("existing-key", status), BIO_OK);
    EXPECT_EQ(status.size, value.size());
    system.Stop();
    Start();
    std::vector<char> data(value.size());
    ASSERT_EQ(system.Get("existing-key", data.data(), data.size(), 0), BIO_OK);
    EXPECT_EQ(std::string(data.begin(), data.end()), value);
    EXPECT_EQ(system.Exist("existing-key"), BIO_OK);
    EXPECT_EQ(system.Delete("existing-key"), BIO_OK);
    EXPECT_EQ(system.Exist("existing-key"), BIO_NOT_EXISTS);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestSharedSystem, shared_initialization_uses_only_the_root_path)
{
    auto options = MakeCConfig();
    options.localConfig.slotIndex = DEVICE_SIZE;
    options.localConfig.deviceCount = 0;
    options.localConfig.evictionPolicy = nullptr;
    options.localConfig.checkIntervalSeconds = 0;
    UfsInitUnderFsConfig(options);
    EXPECT_EQ(UfsSetLocalMetaEventCallback(nullptr, nullptr), BIO_INVALID_PARAM);
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_EQ(system.mRootPath, root + "/v1");
    EXPECT_EQ(system.mEvictionPolicy, nullptr);
    EXPECT_FALSE(system.mEvictionThread.joinable());
}

TEST_F(TestSharedSystem, shared_put_uses_posix_data_without_eviction)
{
    MockFileSystemCapacity(1);
    Start();
    ASSERT_EQ(system.Put("key", "sharedvalue", 11), BIO_OK);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("key", path), BIO_OK);
    struct stat fileStat{};
    ASSERT_EQ(stat(path.c_str(), &fileStat), 0);
    EXPECT_EQ(fileStat.st_size, 11);
    std::ifstream file(path, std::ios::binary);
    char stored[11]{};
    ASSERT_TRUE(file.read(stored, sizeof(stored)));
    EXPECT_EQ(std::string(stored, sizeof(stored)), "sharedvalue");
    SharedSystem peer;
    ASSERT_EQ(peer.Init(), BIO_OK);
    char value[11]{};
    ASSERT_EQ(peer.Get("key", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "sharedvalue");
    EXPECT_TRUE(Usage().files == 0);
    EXPECT_FALSE(system.mEvictionThread.joinable());
    peer.Stop();
}

TEST_F(TestSharedSystem, failed_write_keeps_published_raw_value)
{
    Start();
    ASSERT_EQ(system.Put("key", "old", 3), BIO_OK);
    unlinkAttempts = 0;
    MOCKER(unlink).stubs().will(invoke(ObserveTemporaryCleanup));
    MOCKER(pwrite).expects(once()).will(invoke(FailedWrite));
    MOCKER(fstatvfs).expects(never());
    EXPECT_EQ(system.Put("key", "new-value", 9), BIO_UFS_IOERR);
    EXPECT_EQ(unlinkAttempts, 1U);
    char value[3]{};
    ASSERT_EQ(system.Get("key", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "old");
    FileSystem::ObjStat status{};
    ASSERT_EQ(system.Stat("key", status), BIO_OK);
    EXPECT_EQ(status.size, 3U);
    EXPECT_TRUE(Usage().files == 0);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestSharedSystem, enospc_write_returns_without_local_space_queries_or_retry)
{
    Start();
    writeAttempts = 0;
    MOCKER(fstatvfs).expects(never());
    MOCKER(pwrite).expects(once()).will(invoke(OutOfSpaceWrite));
    EXPECT_EQ(system.Put("key", "firstsecond", 11), BIO_UFS_IOERR);
    EXPECT_EQ(writeAttempts, 1U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 0U);
}

TEST_F(TestSharedSystem, failed_publication_keeps_published_raw_value_and_cleans_the_temporary_file)
{
    Start();
    ASSERT_EQ(system.Put("key", "old", 3), BIO_OK);
    unlinkAttempts = 0;
    MOCKER(unlink).stubs().will(invoke(ObserveTemporaryCleanup));
    MOCKER(rename).stubs().will(returnValue(-1));
    EXPECT_EQ(system.Put("key", "new-value", 9), BIO_UFS_IOERR);
    EXPECT_EQ(unlinkAttempts, 1U);
    char value[3]{};
    ASSERT_EQ(system.Get("key", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "old");
    FileSystem::ObjStat status{};
    ASSERT_EQ(system.Stat("key", status), BIO_OK);
    EXPECT_EQ(status.size, 3U);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_TRUE(events.empty());
}

}
