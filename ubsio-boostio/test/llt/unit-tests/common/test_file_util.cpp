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
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "bio_file_util.h"
#include "gtest/gtest.h"

using namespace ock::bio;

namespace {
class TempDiskState {
public:
    explicit TempDiskState(const std::string &name)
        : root("./bio_ut_disk_state_" + std::to_string(getpid()) + "_" + name),
          child(root + "/disk1"),
          partition(root + "/partition"),
          childPartition(child + "/partition"),
          mountInfo(root + "/mountinfo")
    {
        Cleanup();
        static_cast<void>(mkdir(root.c_str(), 0700));
    }

    ~TempDiskState()
    {
        Cleanup();
    }

    bool CreateFile(const std::string &path, const std::string &content = "") const
    {
        int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (fd < 0) {
            return false;
        }
        ssize_t written = write(fd, content.data(), content.size());
        close(fd);
        return written == static_cast<ssize_t>(content.size());
    }

    bool CreateChild() const
    {
        return mkdir(child.c_str(), 0700) == 0;
    }

    void Cleanup() const
    {
        static_cast<void>(unlink(childPartition.c_str()));
        static_cast<void>(unlink(partition.c_str()));
        static_cast<void>(unlink(mountInfo.c_str()));
        static_cast<void>(rmdir(child.c_str()));
        static_cast<void>(rmdir(root.c_str()));
    }

    std::string root;
    std::string child;
    std::string partition;
    std::string childPartition;
    std::string mountInfo;
};
}

TEST(TestFileUtil, disk_without_partitions_passes_partition_check)
{
    TempDiskState state("empty");
    std::string reason;

    EXPECT_TRUE(FileUtil::CheckNoPartitions(state.root, reason));
    EXPECT_TRUE(reason.empty());
}

TEST(TestFileUtil, configured_partition_without_child_partition_passes)
{
    TempDiskState state("partition");
    ASSERT_TRUE(state.CreateFile(state.partition));
    std::string reason;

    EXPECT_TRUE(FileUtil::CheckNoPartitions(state.root, reason));
    EXPECT_TRUE(reason.empty());
}

TEST(TestFileUtil, disk_containing_partition_is_rejected)
{
    TempDiskState state("child_partition");
    ASSERT_TRUE(state.CreateChild());
    ASSERT_TRUE(state.CreateFile(state.childPartition));
    std::string reason;

    EXPECT_FALSE(FileUtil::CheckNoPartitions(state.root, reason));
    EXPECT_NE(reason.find("contains partition"), std::string::npos);
}

TEST(TestFileUtil, mounted_device_is_rejected)
{
    TempDiskState state("mounted");
    ASSERT_TRUE(state.CreateFile(state.mountInfo,
        "29 23 8:0 / /cache rw,relatime - ext4 /dev/sda rw\n"
        "30 23 8:16 / /data rw,relatime - ext4 /dev/sdb rw\n"));
    std::string reason;

    EXPECT_FALSE(FileUtil::CheckNotMounted(state.mountInfo, "8:0", reason));
    EXPECT_EQ(reason, "the configured device is mounted");
}

TEST(TestFileUtil, unmounted_device_passes_mount_check)
{
    TempDiskState state("unmounted");
    ASSERT_TRUE(state.CreateFile(state.mountInfo, "29 23 8:0 / /cache rw,relatime - ext4 /dev/sda rw\n"));
    std::string reason;

    EXPECT_TRUE(FileUtil::CheckNotMounted(state.mountInfo, "8:16", reason));
    EXPECT_TRUE(reason.empty());
}

TEST(TestFileUtil, unreadable_mount_state_is_rejected)
{
    TempDiskState state("invalid_mountinfo");
    ASSERT_TRUE(state.CreateFile(state.mountInfo, "invalid\n"));
    std::string reason;

    EXPECT_FALSE(FileUtil::CheckNotMounted(state.mountInfo, "8:0", reason));
    EXPECT_EQ(reason, "failed to parse mounted devices");
}

TEST(TestFileUtil, config_line_lookup_matches_startup_parser_semantics)
{
    std::vector<std::string> lines = {
        "# ubsio.disk.path = /dev/commented",
        "ubsio.disk.path.backup = /dev/similar",
        " ubsio.disk.path = /dev/first ",
        "ubsio.disk.path = /dev/effective",
    };

    EXPECT_EQ(FileUtil::FindTargetLine(lines, "ubsio.disk.path"), 3);
}

TEST(TestFileUtil, append_config_updates_last_valid_duplicate)
{
    std::vector<std::string> lines = {
        "ubsio.disk.path = /dev/first",
        "# ubsio.disk.path = /dev/commented",
        "ubsio.disk.path.backup = /dev/similar",
        "  ubsio.disk.path  =   /dev/effective  \t",
    };

    ASSERT_TRUE(FileUtil::AppendConfigToLine(lines, "ubsio.disk.path", ":/dev/new"));
    EXPECT_EQ(lines[0], "ubsio.disk.path = /dev/first");
    EXPECT_EQ(lines[1], "# ubsio.disk.path = /dev/commented");
    EXPECT_EQ(lines[2], "ubsio.disk.path.backup = /dev/similar");
    EXPECT_EQ(lines[3], "  ubsio.disk.path  =   /dev/effective:/dev/new");
}
