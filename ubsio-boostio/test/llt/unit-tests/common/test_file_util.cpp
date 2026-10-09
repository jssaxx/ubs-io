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

TEST(TestFileUtil, path_helpers_validate_and_join_paths)
{
    TempDiskState state("path_helpers");
    ASSERT_TRUE(state.CreateFile(state.partition));

    std::string absoluteFilePath = state.partition;
    ASSERT_TRUE(FileUtil::CanonicalPath(absoluteFilePath));
    EXPECT_TRUE(FileUtil::IsAbsoluteRegularFile(absoluteFilePath));
    EXPECT_FALSE(FileUtil::IsAbsoluteRegularFile(state.partition));

    std::string absoluteDirectoryPath = state.root;
    ASSERT_TRUE(FileUtil::CanonicalPath(absoluteDirectoryPath));
    EXPECT_FALSE(FileUtil::IsAbsoluteRegularFile(absoluteDirectoryPath));

    EXPECT_EQ(FileUtil::JoinPath("/var/log/ubsio", "trace"), "/var/log/ubsio/trace");
    EXPECT_EQ(FileUtil::JoinPath("/var/log/ubsio/", "trace"), "/var/log/ubsio/trace");
}

TEST(TestFileUtil, prepare_directory_creates_and_canonicalizes_path)
{
    const std::string root = "./bio_ut_prepare_directory_" + std::to_string(getpid());
    if (FileUtil::Exist(root)) {
        ASSERT_TRUE(FileUtil::RemoveDirRecursive(root));
    }

    std::string directory = root + "/parent/child";
    auto prepared = FileUtil::PrepareDirectory(directory, 0700);
    EXPECT_TRUE(prepared);
    if (prepared) {
        EXPECT_EQ(directory.front(), '/');
        struct stat pathStat {};
        EXPECT_EQ(stat(directory.c_str(), &pathStat), 0);
        EXPECT_TRUE(S_ISDIR(pathStat.st_mode));
    }

    if (FileUtil::Exist(root)) {
        EXPECT_TRUE(FileUtil::RemoveDirRecursive(root));
    }
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

TEST(TestFileUtil, disk_capacity_and_raw_path_diagnostics)
{
    TempDiskState state("capacity");
    ASSERT_TRUE(state.CreateFile(state.partition));
    int fd = open(state.partition.c_str(), O_RDWR);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(ftruncate(fd, 4096), 0);
    ASSERT_EQ(close(fd), 0);

    std::string path = state.partition;
    std::string operation;
    int32_t errorCode = 0;
    EXPECT_EQ(FileUtil::GetDiskCapacityWithDiagnostics(path, operation, errorCode), 4096);
    EXPECT_TRUE(operation.empty());
    EXPECT_EQ(errorCode, 0);

    std::string absent = state.root + "/absent";
    EXPECT_EQ(FileUtil::GetDiskCapacityWithDiagnostics(absent, operation, errorCode), 0);
    EXPECT_EQ(operation, "realpath");
    EXPECT_NE(errorCode, 0);

    std::string directory = state.root;
    EXPECT_EQ(FileUtil::GetDiskCapacityWithDiagnostics(directory, operation, errorCode), 0);
    EXPECT_EQ(operation, "open(O_RDWR|O_SYNC)");
    EXPECT_NE(errorCode, 0);

    std::string sysPath;
    ASSERT_TRUE(FileUtil::GetBlockDeviceSysPath(path, sysPath));
    EXPECT_EQ(sysPath.front(), '/');
    std::string diskKey;
    ASSERT_TRUE(FileUtil::GetPhysicalDiskKey(path, diskKey));
    EXPECT_EQ(diskKey, sysPath);
    EXPECT_FALSE(FileUtil::GetBlockDeviceSysPath(absent, sysPath));
    EXPECT_FALSE(FileUtil::GetPhysicalDiskKey(absent, diskKey));

    std::string reason;
    EXPECT_FALSE(FileUtil::CheckNoPartitions(absent, reason));
    EXPECT_EQ(reason, "failed to inspect device partitions");
    EXPECT_FALSE(FileUtil::CheckNotMounted(absent, "8:0", reason));
    EXPECT_EQ(reason, "failed to inspect mounted devices");
}

TEST(TestFileUtil, file_mutations_sync_and_report_failures)
{
    TempDiskState state("mutations");
    ASSERT_TRUE(state.CreateFile(state.partition, "original"));
    ASSERT_TRUE(state.CreateChild());
    const std::string absent = state.root + "/absent";

    EXPECT_FALSE(FileUtil::BackUpFile(absent, state.mountInfo));
    EXPECT_FALSE(FileUtil::BackUpFile(state.partition, state.root + "/missing/copy"));
    ASSERT_TRUE(FileUtil::BackUpFile(state.partition, state.mountInfo));
    EXPECT_TRUE(FileUtil::SyncFile(state.mountInfo));
    EXPECT_FALSE(FileUtil::SyncFile(absent));
    EXPECT_TRUE(FileUtil::SyncDir(state.root));
    EXPECT_FALSE(FileUtil::SyncDir(state.partition));

    EXPECT_TRUE(FileUtil::RenameFile(state.mountInfo, state.mountInfo));
    EXPECT_FALSE(FileUtil::RenameFile(absent, state.childPartition));
    ASSERT_TRUE(FileUtil::RenameFile(state.mountInfo, state.childPartition));
    EXPECT_TRUE(FileUtil::RemoveFile(state.childPartition));
    EXPECT_FALSE(FileUtil::RemoveFile(state.childPartition));

    EXPECT_FALSE(FileUtil::WriteFile(state.root, { "invalid" }));
    ASSERT_TRUE(FileUtil::WriteFile(state.mountInfo, { "first", "second" }));
    std::vector<std::string> lines;
    ASSERT_TRUE(FileUtil::ReadFile(state.mountInfo, lines));
    EXPECT_EQ(lines, (std::vector<std::string>{ "first", "second" }));
    EXPECT_FALSE(FileUtil::ReadFile(absent, lines));
}
