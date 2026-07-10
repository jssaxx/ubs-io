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

#include <algorithm>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unistd.h>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

#define private public
#include "bio_config_instance.h"
#undef private

using namespace ock::bio;

namespace {
constexpr int64_t TEST_DISK_CAP = 1024 * 1024 * 1024;

class TempPath {
public:
    explicit TempPath(std::string pathParam) : path(std::move(pathParam))
    {
        static_cast<void>(unlink(path.c_str()));
    }

    ~TempPath()
    {
        static_cast<void>(unlink(path.c_str()));
    }

    bool CreateFile() const
    {
        int fd = open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
        if (fd < 0) {
            return false;
        }
        close(fd);
        return true;
    }

    bool CreateSymlinkTo(const std::string &target) const
    {
        static_cast<void>(unlink(path.c_str()));
        return symlink(target.c_str(), path.c_str()) == 0;
    }

    std::string path;
};

std::string MakeTempPath(const std::string &name)
{
    return "./bio_ut_standalone_" + std::to_string(getpid()) + "_" + name;
}

BioConfigPtr MakeStandaloneConfig(const std::vector<std::string> &diskList, const std::vector<int64_t> &diskCaps,
    uint32_t deviceCount)
{
    auto config = MakeRef<BioConfig>();
    config->mDaemonConfig.diskList = diskList;
    config->mDaemonConfig.diskCaps = diskCaps;
    config->mDaemonConfig.standaloneDeviceCount = deviceCount;
    return config;
}

std::vector<int64_t> MakeDiskCaps(size_t diskNum)
{
    return std::vector<int64_t>(diskNum, TEST_DISK_CAP);
}

bool ContainsExactlyOne(const std::vector<std::string> &selected, const std::string &first, const std::string &second)
{
    uint32_t count = 0;
    count += std::find(selected.begin(), selected.end(), first) != selected.end() ? 1 : 0;
    count += std::find(selected.begin(), selected.end(), second) != selected.end() ? 1 : 0;
    return count == 1;
}
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

TEST(TestStandaloneConfig, device_count_rejects_invalid_distribution_inputs)
{
    std::vector<std::string> disks = { "disk0", "disk1" };

    auto outOfRangeConfig = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 2);
    outOfRangeConfig->SetStandaloneDeviceInfo(2);
    EXPECT_EQ(outOfRangeConfig->SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);

    auto tooFewDisksConfig = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 3);
    tooFewDisksConfig->SetStandaloneDeviceInfo(0);
    EXPECT_EQ(tooFewDisksConfig->SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);

    auto invalidCapConfig = MakeStandaloneConfig(disks, { TEST_DISK_CAP, 0 }, 2);
    invalidCapConfig->SetStandaloneDeviceInfo(1);
    EXPECT_EQ(invalidCapConfig->SelectStandaloneDiskByDeviceInfo(), BIO_INVALID_PARAM);
}

TEST(TestStandaloneConfig, device_count_distributes_non_integer_disk_count)
{
    std::vector<std::string> disks = { "disk0", "disk1", "disk2", "disk3", "disk4" };
    std::set<std::string> selectedDisks;
    std::vector<size_t> selectedSizes;

    for (uint32_t deviceId = 0; deviceId < 3; deviceId++) {
        auto config = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 3);
        config->SetStandaloneDeviceInfo(deviceId);
        ASSERT_EQ(config->SelectStandaloneDiskByDeviceInfo(), BIO_OK);
        ASSERT_FALSE(config->GetDaemonConfig().diskList.empty());

        selectedSizes.emplace_back(config->GetDaemonConfig().diskList.size());
        for (const auto &disk : config->GetDaemonConfig().diskList) {
            EXPECT_TRUE(selectedDisks.insert(disk).second);
        }
    }

    EXPECT_EQ(selectedDisks.size(), disks.size());
    auto minmax = std::minmax_element(selectedSizes.begin(), selectedSizes.end());
    ASSERT_NE(minmax.first, selectedSizes.end());
    EXPECT_LE(*minmax.second - *minmax.first, 1U);
}

TEST(TestStandaloneConfig, device_count_spreads_partitions_from_same_physical_disk)
{
    TempPath targetA(MakeTempPath("target_a"));
    TempPath targetB(MakeTempPath("target_b"));
    TempPath diskA0(MakeTempPath("disk_a0"));
    TempPath diskA1(MakeTempPath("disk_a1"));
    TempPath diskB0(MakeTempPath("disk_b0"));
    TempPath diskB1(MakeTempPath("disk_b1"));

    ASSERT_TRUE(targetA.CreateFile());
    ASSERT_TRUE(targetB.CreateFile());
    ASSERT_TRUE(diskA0.CreateSymlinkTo(targetA.path));
    ASSERT_TRUE(diskA1.CreateSymlinkTo(targetA.path));
    ASSERT_TRUE(diskB0.CreateSymlinkTo(targetB.path));
    ASSERT_TRUE(diskB1.CreateSymlinkTo(targetB.path));

    std::vector<std::string> disks = { diskA0.path, diskA1.path, diskB0.path, diskB1.path };
    for (uint32_t deviceId = 0; deviceId < 2; deviceId++) {
        auto config = MakeStandaloneConfig(disks, MakeDiskCaps(disks.size()), 2);
        config->SetStandaloneDeviceInfo(deviceId);
        ASSERT_EQ(config->SelectStandaloneDiskByDeviceInfo(), BIO_OK);

        const auto &selected = config->GetDaemonConfig().diskList;
        ASSERT_EQ(selected.size(), 2);
        EXPECT_TRUE(ContainsExactlyOne(selected, diskA0.path, diskA1.path));
        EXPECT_TRUE(ContainsExactlyOne(selected, diskB0.path, diskB1.path));
    }
}
