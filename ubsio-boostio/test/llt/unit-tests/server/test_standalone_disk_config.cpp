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

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unistd.h>
#include "gtest/gtest.h"

#define private public
#include "bio_config_instance.h"
#undef private

using namespace ock::bio;

namespace {
class TestStandaloneDiskConfig : public testing::Test {
protected:
    void SetUp() override
    {
        char path[] = "/tmp/ubsio-disk-config-XXXXXX";
        char *created = mkdtemp(path);
        ASSERT_NE(created, nullptr);
        directory = created;
        oldDisk = directory + "/old";
        newDisk = directory + "/new";
        alias = directory + "/alias";
        std::ofstream(oldDisk).close();
        std::ofstream(newDisk).close();
        ASSERT_EQ(symlink(oldDisk.c_str(), alias.c_str()), 0);
        config.mConfigPath = directory + "/ubsio.conf";
        config.mConfigBakPath = config.mConfigPath + ".bak";
        config.mConfigBakInitPath = config.mConfigPath + ".bak.init";
        config.mConfigLockPath = config.mConfigPath + ".lock";
        config.mDaemonConfig.diskList = {oldDisk};
    }

    void TearDown() override
    {
        config.UnlockDiskConfig();
        for (const auto &path : {oldDisk, newDisk, alias, config.mConfigPath, config.mConfigBakPath,
            config.mConfigBakInitPath, config.mConfigLockPath}) {
            if (!path.empty()) {
                unlink(path.c_str());
            }
        }
        if (!directory.empty()) {
            rmdir(directory.c_str());
        }
    }

    void WriteConfig(const std::string &paths)
    {
        std::ofstream output(config.mConfigPath);
        output << "ubsio.disk.path=" << paths << '\n';
        ASSERT_TRUE(output.good());
    }

    BioConfig config;
    std::string directory;
    std::string oldDisk;
    std::string newDisk;
    std::string alias;
};

TEST_F(TestStandaloneDiskConfig, memory_only_does_not_require_device_info)
{
    config.mDaemonConfig.hasDiskCache = false;
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_OK);
}

TEST_F(TestStandaloneDiskConfig, virtual_device_validation_preserves_all_disks)
{
    auto &daemon = config.mDaemonConfig;
    daemon.hasDiskCache = true;
    daemon.standaloneDeviceCount = 2;
    daemon.diskList = {oldDisk, newDisk};
    daemon.diskCaps = {1024, 2048};
    daemon.diskPhysicalCaps = daemon.diskCaps;
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
    config.SetStandaloneDeviceInfo(1);
    EXPECT_EQ(config.GetStandaloneDeviceId(), 1U);
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_OK);
    EXPECT_EQ(daemon.diskList, (std::vector<std::string>{oldDisk, newDisk}));
    EXPECT_EQ(daemon.diskCaps, (std::vector<int64_t>{1024, 2048}));
    EXPECT_EQ(daemon.diskPhysicalCaps, daemon.diskCaps);
}

TEST_F(TestStandaloneDiskConfig, virtual_device_validation_rejects_invalid_count_and_index)
{
    auto &daemon = config.mDaemonConfig;
    daemon.hasDiskCache = true;
    daemon.diskCaps = {1024};
    daemon.diskPhysicalCaps = daemon.diskCaps;
    daemon.standaloneDeviceCount = 1;
    config.SetStandaloneDeviceInfo(1);
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
    config.SetStandaloneDeviceInfo(0);
    daemon.standaloneDeviceCount = 0;
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
    daemon.standaloneDeviceCount = UINT32_MAX;
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
}

TEST_F(TestStandaloneDiskConfig, virtual_device_validation_rejects_invalid_disk_config)
{
    auto &daemon = config.mDaemonConfig;
    daemon.hasDiskCache = true;
    daemon.standaloneDeviceCount = 1;
    config.SetStandaloneDeviceInfo(0);
    daemon.diskCaps.clear();
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_ERR);
    daemon.diskCaps = {1024};
    daemon.diskPhysicalCaps.clear();
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_ERR);
    daemon.diskPhysicalCaps = {1024};
    daemon.diskCaps = {0};
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
    daemon.diskCaps = {-1};
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
    daemon.diskList.clear();
    EXPECT_EQ(config.SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
}

TEST_F(TestStandaloneDiskConfig, missing_old_node_allows_config_append)
{
    WriteConfig(oldDisk);
    ASSERT_EQ(unlink(oldDisk.c_str()), 0);
    ASSERT_EQ(config.LockDiskConfig(), BIO_OK);
    ASSERT_EQ(config.CreateDiskConfBak(newDisk), BIO_OK);
    ASSERT_EQ(config.CommitDiskConfBak(), BIO_OK);
    EXPECT_TRUE(config.FindDiskInConfig(config.mConfigPath, oldDisk));
    EXPECT_TRUE(config.FindDiskInConfig(config.mConfigPath, newDisk));
    EXPECT_EQ(config.CreateDiskConfBak(newDisk), BIO_EXISTS);
}

TEST_F(TestStandaloneDiskConfig, missing_alias_target_allows_config_append)
{
    WriteConfig(alias);
    ASSERT_EQ(unlink(oldDisk.c_str()), 0);
    ASSERT_EQ(config.LockDiskConfig(), BIO_OK);
    ASSERT_EQ(config.CreateDiskConfBak(newDisk), BIO_OK);
    ASSERT_EQ(config.CommitDiskConfBak(), BIO_OK);
    EXPECT_TRUE(config.FindDiskInConfig(config.mConfigPath, alias));
    EXPECT_TRUE(config.FindDiskInConfig(config.mConfigPath, newDisk));
}

TEST_F(TestStandaloneDiskConfig, disk_config_backup_commit_and_lock_lifecycle)
{
    config.mConfigLockPath.clear();
    EXPECT_EQ(config.LockDiskConfig(), BIO_INNER_ERR);
    config.UnlockDiskConfig();
    config.mConfigLockPath = directory + "/missing/lock";
    EXPECT_EQ(config.LockDiskConfig(), BIO_INNER_ERR);
    config.mConfigLockPath = directory + "/ubsio.conf.lock";
    ASSERT_EQ(config.LockDiskConfig(), BIO_OK);
    EXPECT_EQ(config.LockDiskConfig(), BIO_OK);
    config.UnlockDiskConfig();
    config.UnlockDiskConfig();

    EXPECT_EQ(config.CommitDiskConfBak(), BIO_INNER_ERR);
    EXPECT_EQ(config.CreateDiskConfBak(newDisk), BIO_INNER_ERR);
    WriteConfig(oldDisk);
    ASSERT_EQ(config.CreateDiskConfBak(newDisk), BIO_OK);
    std::vector<std::string> lines;
    ASSERT_TRUE(FileUtil::ReadFile(config.mConfigBakInitPath, lines));
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.front(), "ubsio.disk.path=" + oldDisk + ":" + newDisk);
    ASSERT_EQ(config.CommitDiskConfBak(), BIO_OK);
    lines.clear();
    ASSERT_TRUE(FileUtil::ReadFile(config.mConfigPath, lines));
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.front(), "ubsio.disk.path=" + oldDisk + ":" + newDisk);
    EXPECT_EQ(config.CreateDiskConfBak(newDisk), BIO_EXISTS);
    EXPECT_EQ(config.ReplaceFile(config.mConfigPath, config.mConfigBakInitPath), BIO_INNER_ERR);
    config.DiscardDiskConfBak();

    config.mDaemonConfig.diskList.clear();
    config.mDaemonConfig.diskCaps.clear();
    config.mDaemonConfig.diskPhysicalCaps.clear();
    std::string diskPath = config.mConfigPath;
    config.ResizeDaemonConfigDisks(diskPath);
    ASSERT_EQ(config.mDaemonConfig.diskList.size(), 1);
    ASSERT_EQ(config.mDaemonConfig.diskCaps.size(), 1);
    ASSERT_EQ(config.mDaemonConfig.diskPhysicalCaps.size(), 1);
    EXPECT_EQ(config.mDaemonConfig.diskCaps[0], config.mDaemonConfig.diskPhysicalCaps[0]);
    EXPECT_EQ(config.UpdateStandaloneDiskCapacity(1, 10), BIO_INVALID_PARAM);
    EXPECT_EQ(config.UpdateStandaloneDiskCapacity(0, 0), BIO_INVALID_PARAM);
    EXPECT_EQ(config.UpdateStandaloneDiskCapacity(0, 20), BIO_OK);
    EXPECT_EQ(config.mDaemonConfig.diskCaps[0], 20);
}
}
