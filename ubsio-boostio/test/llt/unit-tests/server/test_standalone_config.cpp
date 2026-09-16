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

#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "gtest/gtest.h"

#define private public
#include "bio_config_instance.h"
#undef private

using namespace ock::bio;

namespace {
constexpr int64_t TEST_DISK_CAP = 1024 * 1024 * 1024;

BioConfigPtr MakeStandaloneConfig(const std::vector<std::string> &diskList, const std::vector<int64_t> &diskCaps,
    uint32_t deviceCount)
{
    auto config = MakeRef<BioConfig>();
    config->mDaemonConfig.diskList = diskList;
    config->mDaemonConfig.diskCaps = diskCaps;
    // SelectStandaloneDiskByDeviceInfo requires the startup physical capacity
    // snapshot to stay in sync with diskList; the fixture bypasses
    // AutoConfigDaemonDisk, so fill it with the same physical values.
    config->mDaemonConfig.diskPhysicalCaps = diskCaps;
    config->mDaemonConfig.standaloneDeviceCount = deviceCount;
    return config;
}

std::vector<int64_t> MakeDiskCaps(size_t diskNum)
{
    return std::vector<int64_t>(diskNum, TEST_DISK_CAP);
}

class TempConfigBackup {
public:
    TempConfigBackup() : root("./bio_ut_config_backup_" + std::to_string(getpid())),
        current(root + "/ubsio.conf"), prepared(root + "/ubsio.conf.bak.init"),
        committed(root + "/ubsio.conf.bak"), lock(root + "/disk.lock")
    {
        created = mkdir(root.c_str(), 0700) == 0;
    }

    ~TempConfigBackup()
    {
        static_cast<void>(unlink(current.c_str()));
        static_cast<void>(unlink(prepared.c_str()));
        static_cast<void>(unlink(committed.c_str()));
        static_cast<void>(unlink(lock.c_str()));
        if (created) {
            static_cast<void>(rmdir(root.c_str()));
        }
    }

    std::string root;
    std::string current;
    std::string prepared;
    std::string committed;
    std::string lock;
    bool created;
};

}

TEST(TestStandaloneConfig, legacy_device_count_zero_selects_disk_by_device_id)
{
    std::vector<std::string> disks = { "disk0", "disk1", "disk2" };
    auto config = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 0);

    config->SetStandaloneDeviceInfo(1);
    EXPECT_EQ(config->SelectStandaloneDiskByDeviceInfo(), BIO_OK);

    ASSERT_EQ(config->GetDaemonConfig().diskList.size(), 1);
    EXPECT_EQ(config->GetDaemonConfig().diskList[0], "disk1");
    EXPECT_EQ(config->GetDaemonConfig().diskCaps[0], TEST_DISK_CAP);
    ASSERT_EQ(config->GetDaemonConfig().diskPhysicalCaps.size(), 1);
    EXPECT_EQ(config->GetDaemonConfig().diskPhysicalCaps[0], TEST_DISK_CAP);
    EXPECT_EQ(config->mStandaloneDiskIndex, 1);
}

TEST(TestStandaloneConfig, no_disk_cache_skips_standalone_disk_selection)
{
    auto config = MakeStandaloneConfig({}, {}, 0);
    config->mDaemonConfig.hasDiskCache = false;

    EXPECT_EQ(config->SelectStandaloneDiskByDeviceInfo(), BIO_OK);
    EXPECT_TRUE(config->GetDaemonConfig().diskList.empty());
    EXPECT_TRUE(config->GetDaemonConfig().diskCaps.empty());
}

TEST(TestStandaloneConfig, legacy_device_count_zero_rejects_out_of_range_device_id)
{
    std::vector<std::string> disks = { "disk0", "disk1" };
    auto config = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 0);

    config->SetStandaloneDeviceInfo(2);
    EXPECT_EQ(config->SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
    EXPECT_EQ(config->GetDaemonConfig().diskList, disks);
}

TEST(TestStandaloneConfig, select_disk_requires_device_info_and_consistent_caps)
{
    std::vector<std::string> disks = { "disk0", "disk1" };
    auto config = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 2);

    EXPECT_EQ(config->SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);

    auto badCapsConfig = MakeStandaloneConfig(disks, { TEST_DISK_CAP }, 2);
    badCapsConfig->SetStandaloneDeviceInfo(0);
    EXPECT_EQ(badCapsConfig->SelectStandaloneDiskByDeviceInfo(), BIO_ERR);
}

TEST(TestStandaloneConfig, virtual_regions_validate_inputs)
{
    std::vector<std::string> disks = { "disk0", "disk1" };

    auto nonDivisorCountConfig = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 3);
    nonDivisorCountConfig->SetStandaloneDeviceInfo(0);
    EXPECT_EQ(nonDivisorCountConfig->SelectStandaloneDiskByDeviceInfo(), BIO_OK);

    auto outOfRangeConfig = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 2);
    outOfRangeConfig->SetStandaloneDeviceInfo(2);
    EXPECT_EQ(outOfRangeConfig->SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);

    auto invalidCapConfig = MakeStandaloneConfig(disks, { TEST_DISK_CAP, 0 }, 2);
    invalidCapConfig->SetStandaloneDeviceInfo(1);
    EXPECT_EQ(invalidCapConfig->SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);

    std::vector<std::string> tooManyDisks(DEVICE_SIZE + 1, "disk");
    auto tooManyDisksConfig = MakeStandaloneConfig(tooManyDisks, MakeDiskCaps(tooManyDisks.size()), 2);
    tooManyDisksConfig->SetStandaloneDeviceInfo(0);
    EXPECT_EQ(tooManyDisksConfig->SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
}

TEST(TestStandaloneConfig, virtual_regions_keep_all_whole_disks_for_every_device)
{
    std::vector<std::string> disks = { "disk0", "disk1" };
    auto diskCaps = MakeDiskCaps(disks.size());

    for (uint32_t deviceId = 0; deviceId < 8; deviceId++) {
        auto config = MakeStandaloneConfig(disks, diskCaps, 8);
        config->SetStandaloneDeviceInfo(deviceId);
        ASSERT_EQ(config->SelectStandaloneDiskByDeviceInfo(), BIO_OK);
        EXPECT_EQ(config->GetDaemonConfig().diskList, disks);
        EXPECT_EQ(config->GetDaemonConfig().diskCaps, diskCaps);
    }
}

TEST(TestStandaloneConfig, virtual_regions_allow_one_whole_disk_for_multiple_devices)
{
    std::vector<std::string> disks = { "disk0" };
    auto config = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 16);
    config->SetStandaloneDeviceInfo(15);

    EXPECT_EQ(config->SelectStandaloneDiskByDeviceInfo(), BIO_OK);
    EXPECT_EQ(config->GetDaemonConfig().diskList, disks);
}

TEST(TestStandaloneConfig, disk_config_backup_commit_and_lock_lifecycle)
{
    TempConfigBackup files;
    ASSERT_TRUE(files.created);
    auto config = MakeRef<BioConfig>();
    ASSERT_NE(config, nullptr);
    config->mConfigPath = files.current;
    config->mConfigBakInitPath = files.prepared;
    config->mConfigBakPath = files.committed;

    EXPECT_EQ(config->LockDiskConfig(), BIO_INNER_ERR);
    config->UnlockDiskConfig();
    config->mConfigLockPath = files.root + "/missing/lock";
    EXPECT_EQ(config->LockDiskConfig(), BIO_INNER_ERR);
    config->mConfigLockPath = files.lock;
    ASSERT_EQ(config->LockDiskConfig(), BIO_OK);
    EXPECT_EQ(config->LockDiskConfig(), BIO_OK);
    config->UnlockDiskConfig();
    config->UnlockDiskConfig();

    EXPECT_EQ(config->CommitDiskConfBak(), BIO_INNER_ERR);
    EXPECT_EQ(config->CreateDiskConfBak("/dev/disk2"), BIO_INNER_ERR);
    ASSERT_TRUE(FileUtil::WriteFile(files.current, { "ubsio.disk.path = /dev/disk1" }));
    ASSERT_EQ(config->CreateDiskConfBak("/dev/disk2"), BIO_OK);
    std::vector<std::string> lines;
    ASSERT_TRUE(FileUtil::ReadFile(files.prepared, lines));
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.front(), "ubsio.disk.path = /dev/disk1:/dev/disk2");
    ASSERT_EQ(config->CommitDiskConfBak(), BIO_OK);
    lines.clear();
    ASSERT_TRUE(FileUtil::ReadFile(files.current, lines));
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.front(), "ubsio.disk.path = /dev/disk1:/dev/disk2");
    EXPECT_EQ(config->CreateDiskConfBak("/dev/disk2"), BIO_EXISTS);
    EXPECT_EQ(config->ReplaceFile(files.current, files.prepared), BIO_INNER_ERR);
    config->DiscardDiskConfBak();

    config->mDaemonConfig.diskList.clear();
    config->mDaemonConfig.diskCaps.clear();
    config->mDaemonConfig.diskPhysicalCaps.clear();
    std::string diskPath = files.current;
    config->ResizeDaemonConfigDisks(diskPath);
    ASSERT_EQ(config->GetDaemonConfig().diskList.size(), 1);
    EXPECT_EQ(config->GetDaemonConfig().diskCaps[0], TEST_DISK_CAP);
    EXPECT_EQ(config->UpdateStandaloneDiskCapacity(1, 10), BIO_INVALID_PARAM);
    EXPECT_EQ(config->UpdateStandaloneDiskCapacity(0, 0), BIO_INVALID_PARAM);
    EXPECT_EQ(config->UpdateStandaloneDiskCapacity(0, 20), BIO_OK);
    EXPECT_EQ(config->GetDaemonConfig().diskCaps[0], 20);
}
