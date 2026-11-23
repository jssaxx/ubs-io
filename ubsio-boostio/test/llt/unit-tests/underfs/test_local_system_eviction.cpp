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

ssize_t OutOfSpaceOnceVectorWrite(int fd, const struct iovec *iov, int iovcnt, off_t offset) noexcept
{
    if (++writeAttempts == 1) {
        errno = ENOSPC;
        return -1;
    }
    return ObserveWrite(fd, iov, iovcnt, offset);
}

int CloseAndClobberErrno(int fd) noexcept
{
    int result = static_cast<int>(syscall(SYS_close, fd));
    errno = EIO;
    return result;
}

int CreateTemporaryAfterNoSpace(char *pattern, int flags) noexcept
{
    if (++writeAttempts == 1) {
        errno = ENOSPC;
        return -1;
    }
    std::memcpy(pattern + std::strlen(pattern) - 6, "A1b2C3", 6);
    return open(pattern, O_RDWR | O_CREAT | O_EXCL | flags, 0600);
}

int MakeDirectoryAfterNoSpace(const char *path, mode_t mode) noexcept
{
    if (++writeAttempts == 1) {
        errno = ENOSPC;
        return -1;
    }
    return static_cast<int>(syscall(SYS_mkdirat, AT_FDCWD, path, mode));
}

int FailTemporaryCleanup(const char *) noexcept
{
    errno = EACCES;
    return -1;
}

int BlockingFileSystemSpace(int fd, struct statvfs *space) noexcept
{
    const int ret = ProbeFileSystemCapacity(fd, space);
    std::unique_lock<std::mutex> lock(spaceGate->mutex);
    spaceGate->entered = true;
    spaceGate->wakeup.notify_all();
    if (!spaceGate->wakeup.wait_for(lock, std::chrono::seconds(5), []() { return spaceGate->released; })) {
        errno = EIO;
        return -1;
    }
    return ret;
}

int SimulatedFileSystemSpace(int, struct statvfs *space) noexcept
{
    ++fileSystemSpaceQueries;
    *space = {};
    space->f_frsize = 1;
    space->f_blocks = 100000;
    space->f_bavail = 100000 - 88000 - spaceOwner->mUsedBytes;
    return 0;
}

int FailSecondEvictionUnlink(int directory, const char *name, int flags)
{
    if (++unlinkAttempts == 2) {
        return FailEvictionUnlink(directory, name, flags);
    }
    return static_cast<int>(syscall(SYS_unlinkat, directory, name, flags));
}

int ObserveUnpublishedBeforeEvictionUnlink(int directory, const char *name, int flags) noexcept
{
    ++unlinkAttempts;
    struct stat status{};
    EXPECT_EQ(fstatat(directory, name, &status, AT_SYMLINK_NOFOLLOW), 0);
    for (const char *key : {"first", "second"}) {
        char value = 0;
        FileSystem::ObjStat objStat{};
        EXPECT_EQ(spaceOwner->Get(key, &value, 1, 0), BIO_NOT_EXISTS);
        EXPECT_EQ(spaceOwner->Exist(key), BIO_NOT_EXISTS);
        EXPECT_EQ(spaceOwner->Stat(key, objStat), BIO_NOT_EXISTS);
    }
    EXPECT_EQ(spaceOwner->mEvictionPolicy->Size(), 1U);
    EXPECT_EQ(spaceOwner->mEvictionPolicy->ForEachFile(
        [](const std::string &, const FileRecord &) noexcept -> BResult {
            ADD_FAILURE() << "Unpublished eviction candidates must not be scanned.";
            return BIO_OK;
        }), BIO_OK);
    return static_cast<int>(syscall(SYS_unlinkat, directory, name, flags));
}

TEST_F(TestLocalEviction, storage_metrics_require_initialization_and_preserve_output_on_failure)
{
    UfsStorageMetrics metrics{7, 9};
    EXPECT_EQ(system.GetStorageMetrics(metrics), BIO_NOT_READY);
    EXPECT_EQ(metrics.totalCapacity, 7U);
    EXPECT_EQ(metrics.usedBytes, 9U);
    EXPECT_EQ(UfsGetStorageMetrics(nullptr), BIO_INVALID_PARAM);
    Start();
    ASSERT_EQ(system.GetStorageMetrics(metrics), BIO_OK);
    EXPECT_GT(metrics.totalCapacity, 0U);
    EXPECT_EQ(metrics.usedBytes, 0U);
    const auto capacity = metrics.totalCapacity;
    system.Stop();
    EXPECT_EQ(system.GetStorageMetrics(metrics), BIO_NOT_READY);
    EXPECT_EQ(metrics.totalCapacity, capacity);
    EXPECT_EQ(metrics.usedBytes, 0U);
}

TEST_F(TestLocalEviction, storage_metrics_use_cached_capacity_and_committed_instance_bytes)
{
    config.localConfig.deviceCount = 4;
    MockFileSystemCapacity(1024 * 1024 + 3);
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    ASSERT_EQ(Reserve("pending", 4096), BIO_OK);
    MockFileSystemCapacity(2 * 1024 * 1024 + 3);
    const auto queries = fileSystemSpaceQueries;
    UfsStorageMetrics metrics{};
    ASSERT_EQ(system.GetStorageMetrics(metrics), BIO_OK);
    EXPECT_EQ(metrics.totalCapacity, (uint64_t{1024} * 1024 + 3) * 9 / 10 / 4);
    EXPECT_EQ(metrics.usedBytes, FileBytes("key", 5));
    EXPECT_EQ(fileSystemSpaceQueries, queries);
    system.ReleaseWriteSpace("pending", 4096);

    system.Stop();
    Start();
    ASSERT_EQ(system.GetStorageMetrics(metrics), BIO_OK);
    EXPECT_EQ(metrics.totalCapacity, (uint64_t{2} * 1024 * 1024 + 3) * 9 / 10 / 4);
    EXPECT_EQ(metrics.usedBytes, FileBytes("key", 5));
    ASSERT_EQ(system.Delete("key"), BIO_OK);
    ASSERT_EQ(system.GetStorageMetrics(metrics), BIO_OK);
    EXPECT_EQ(metrics.usedBytes, 0U);
}

TEST(LocalStorageMetrics, helper_validates_local_mode_and_passes_through_metrics_and_errors)
{
    UfsHelper helper;
    UfsStorageMetrics metrics{7, 9};
    helper.mConfig.underFsType = BIO_UNDERFS_SHARED;
    EXPECT_EQ(helper.GetStorageMetrics(metrics), BIO_INVALID_PARAM);
    helper.mConfig.underFsType = BIO_UNDERFS_LOCAL;
    EXPECT_EQ(helper.GetStorageMetrics(metrics), BIO_NOT_READY);
    EXPECT_EQ(metrics.totalCapacity, 7U);
    EXPECT_EQ(metrics.usedBytes, 9U);
    helper.getStorageMetricsOp = [](UfsStorageMetrics *snapshot) noexcept -> int32_t {
        snapshot->totalCapacity = uint64_t{1} << 40;
        snapshot->usedBytes = uint64_t{1} << 33;
        return BIO_OK;
    };
    ASSERT_EQ(helper.GetStorageMetrics(metrics), BIO_OK);
    EXPECT_EQ(metrics.totalCapacity, uint64_t{1} << 40);
    EXPECT_EQ(metrics.usedBytes, uint64_t{1} << 33);
    helper.getStorageMetricsOp = [](UfsStorageMetrics *) noexcept -> int32_t { return BIO_UFS_IOERR; };
    EXPECT_EQ(helper.GetStorageMetrics(metrics), BIO_UFS_IOERR);
    EXPECT_EQ(metrics.totalCapacity, uint64_t{1} << 40);
    EXPECT_EQ(metrics.usedBytes, uint64_t{1} << 33);
}

TEST_F(TestLocalEviction, local_automatically_enables_eviction_and_uses_ninety_percent_capacity)
{
    Start();
    EXPECT_NE(system.mEvictionPolicy, nullptr);
    EXPECT_TRUE(system.mEvictionThread.joinable());
    EXPECT_GE(system.mLeaseFd, 0);
    struct statvfs space{};
    ASSERT_EQ(statvfs(root.c_str(), &space), 0);
    uint64_t blockSize = space.f_frsize == 0 ? space.f_bsize : space.f_frsize;
    const uint64_t total = static_cast<uint64_t>(space.f_blocks) * blockSize;
    EXPECT_EQ(system.mCapacityBytes, total / 10 * 9 + total % 10 * 9 / 10);
    system.Stop();
    EXPECT_FALSE(system.mEvictionThread.joinable());
    EXPECT_EQ(system.mLeaseFd, -1);
}

TEST_F(TestLocalEviction, exclusive_eviction_owner_and_invalid_configuration)
{
    Start();
    LocalSystem peer;
    EXPECT_EQ(peer.Init(), BIO_UFS_IOERR);
    system.Stop();
    config.localConfig.checkIntervalSeconds = 0;
    UnderFsConfig::Initialize(config);
    EXPECT_EQ(peer.Init(), BIO_INVALID_PARAM);
}

TEST_F(TestLocalEviction, capacity_uses_configured_device_count_on_each_refresh)
{
    config.localConfig.deviceCount = 3;
    Start();
    struct statvfs space{};
    ASSERT_EQ(statvfs(root.c_str(), &space), 0);
    uint64_t blockSize = space.f_frsize == 0 ? space.f_bsize : space.f_frsize;
    const uint64_t total = static_cast<uint64_t>(space.f_blocks) * blockSize;
    EXPECT_EQ(system.mCapacityBytes, (total / 10 * 9 + total % 10 * 9 / 10) / 3);
    spaceOwner = &system;
    MOCKER(fstatvfs).stubs().will(invoke(SimulatedFileSystemSpace));
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.mCapacityBytes, 90000U / 3);
    ASSERT_EQ(Reserve("pending", 1), BIO_OK);
    ASSERT_EQ(system.CheckAndEvict(1), BIO_OK);
    EXPECT_EQ(system.mCapacityBytes, 90000U / 3);
    system.ReleaseWriteSpace("pending", 1);
}

TEST_F(TestLocalEviction, overlapping_checks_skip_before_locking_or_querying_space)
{
    Start();
    ASSERT_EQ(Reserve("pending", 1), BIO_OK);
    mockFileSystemTotalBytes = 100000;
    mockFileSystemAvailableBytes = 100000;
    const auto queries = fileSystemSpaceQueries;
    ReadGate gate;
    spaceGate = &gate;
    MOCKER(fstatvfs).stubs().will(invoke(BlockingFileSystemSpace));
    BResult result = BIO_INNER_ERR;
    std::thread owner([&]() { result = system.CheckAndEvict(); });
    bool entered = false;
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        entered = gate.wakeup.wait_for(lock, std::chrono::seconds(5), [&]() { return gate.entered; });
    }
    if (entered) {
        EXPECT_TRUE(system.mEvictionInProgress.load());
        EXPECT_EQ(system.CheckAndEvict(), BIO_INNER_RETRY);
        EXPECT_EQ(system.CheckAndEvict(1), BIO_INNER_RETRY);
        EXPECT_TRUE(system.mEvictionInProgress.load());
        EXPECT_EQ(fileSystemSpaceQueries, queries + 1);
    }
    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.released = true;
    }
    gate.wakeup.notify_all();
    owner.join();
    EXPECT_TRUE(entered);
    EXPECT_EQ(result, BIO_OK);
    EXPECT_FALSE(system.mEvictionInProgress.load());
    EXPECT_EQ(Usage().reservedBytes, 1U);
    system.ReleaseWriteSpace("pending", 1);
    spaceGate = nullptr;
}

TEST_F(TestLocalEviction, failed_checks_release_ownership_and_restore_the_write_reservation)
{
    Start();
    EXPECT_EQ(system.CheckAndEvict(1), BIO_INVALID_PARAM);
    EXPECT_FALSE(system.mEvictionInProgress.load());
    ASSERT_EQ(Reserve("pending", 128), BIO_OK);
    MOCKER(fstatvfs).stubs().will(returnValue(-1));
    EXPECT_EQ(system.CheckAndEvict(128), BIO_UFS_IOERR);
    EXPECT_FALSE(system.mEvictionInProgress.load());
    EXPECT_EQ(Usage().reservedBytes, 128U);
    system.ReleaseWriteSpace("pending", 128);
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, busy_eviction_rolls_back_only_the_incoming_write)
{
    MockManagedCapacity(1000);
    Start();
    const std::string value(900, 'x');
    ASSERT_EQ(system.Put("old", value.data(), value.size()), BIO_OK);
    const auto queries = fileSystemSpaceQueries;
    system.mEvictionInProgress.store(true);
    EXPECT_EQ(system.Put("new", "x", 1), BIO_INNER_RETRY);
    EXPECT_TRUE(system.mEvictionInProgress.load());
    system.mEvictionInProgress.store(false);
    EXPECT_EQ(fileSystemSpaceQueries, queries);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().usedBytes, FileBytes("old", value.size()));
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_EQ(system.Exist("old"), BIO_OK);
    EXPECT_EQ(system.Exist("new"), BIO_NOT_EXISTS);
    EXPECT_TRUE(events.empty());
    ASSERT_EQ(system.Put("new", "x", 1), BIO_OK);
    EXPECT_EQ(system.Exist("old"), BIO_NOT_EXISTS);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_FALSE(system.mEvictionInProgress.load());
}

TEST_F(TestLocalEviction, lru_recovers_existing_layout_and_repeats_rounds_until_below_high_watermark)
{
    Start();
    Write("old", 'a', 1);
    Write("middle", 'b', 2);
    Write("new", 'c', 3);
    system.Stop();
    Start();
    std::vector<std::string> files;
    uint64_t used = 0;
    ASSERT_EQ(ScanKeys(files, used), BIO_OK);
    ASSERT_EQ(files.size(), 3U);
    ASSERT_GT(used, 0U);
    const auto order = files;
    EXPECT_EQ(CandidateKeys(), order);
    SetEvictionTarget(used * 2 / 3);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.Exist(order[0].c_str()), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist(order[1].c_str()), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist(order[2].c_str()), BIO_OK);
    ASSERT_EQ(ScanKeys(files, used), BIO_OK);
    EXPECT_EQ(files.size(), 1U);
}

TEST_F(TestLocalEviction, background_check_runs_without_new_writes)
{
    config.localConfig.checkIntervalSeconds = 1;
    Start();
    Write("key", 'a', 1);
    SetEvictionTarget(1);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (system.Exist("key") == BIO_OK && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
}

TEST_F(TestLocalEviction, repeated_rounds_stop_when_all_files_have_been_evicted)
{
    Start();
    for (uint32_t index = 0; index < 129; ++index) {
        std::string key = "batch-" + std::to_string(index);
        Write(key.c_str(), 'a', index + 1);
    }
    std::vector<std::string> files;
    uint64_t used = 0;
    ASSERT_EQ(ScanKeys(files, used), BIO_OK);
    ASSERT_GT(used, 1U);
    SetEvictionTarget(1);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    ASSERT_EQ(ScanKeys(files, used), BIO_OK);
    EXPECT_TRUE(files.empty());
    EXPECT_EQ(events.size(), 129U);
    EXPECT_EQ(system.CheckAndEvict(), BIO_OK);
}

TEST_F(TestLocalEviction, managed_capacity_and_eviction_watermark_apply_separate_percentages)
{
    MockFileSystemCapacity(10000);
    Start();
    uint64_t total = 0, available = 0;
    ASSERT_EQ(system.GetFileSystemSpace(total, available), BIO_OK);
    EXPECT_EQ(total, 10000U);
    EXPECT_EQ(available, 10000U);
    UfsStorageMetrics metrics{};
    ASSERT_EQ(system.GetStorageMetrics(metrics), BIO_OK);
    EXPECT_EQ(metrics.totalCapacity, 9000U);

    const std::string value(900 - FileBytes("key-0", 0), 'x');
    for (uint32_t index = 0; index < 9; ++index) {
        const auto key = "key-" + std::to_string(index);
        ASSERT_EQ(system.Put(key.c_str(), value.data(), value.size()), BIO_OK);
    }
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, 8100U);
    EXPECT_EQ(Usage().files, 9U);
    EXPECT_TRUE(events.empty());

    ASSERT_EQ(system.Put("key-9", value.data(), value.size()), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, 9000U);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    ASSERT_EQ(system.GetStorageMetrics(metrics), BIO_OK);
    EXPECT_EQ(metrics.totalCapacity, 9000U);
    EXPECT_EQ(metrics.usedBytes, 8100U);
    EXPECT_EQ(Usage().files, 9U);
    EXPECT_EQ(events.size(), 1U);
    EXPECT_EQ(system.Exist("key-0"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("key-9"), BIO_OK);
}

TEST_F(TestLocalEviction, high_watermark_evicts_ten_percent_of_keys_rounded_up)
{
    MockManagedCapacity(20 * FileBytes("boundary-100", 100));
    Start();
    const std::string value(100, 'x');
    for (uint32_t i = 0; i < 18; ++i) {
        ASSERT_EQ(system.Put(("boundary-" + std::to_string(i + 100)).c_str(), value.data(), value.size()), BIO_OK);
    }
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, 18 * FileBytes("boundary-100", 100));
    ASSERT_EQ(system.Put("boundary-118", value.data(), value.size()), BIO_OK);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, 17 * FileBytes("boundary-100", 100));
    EXPECT_EQ(system.mEvictionPolicy->Size(), 17U);
    EXPECT_EQ(system.Exist("boundary-100"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("boundary-101"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("boundary-102"), BIO_OK);
    EXPECT_EQ(system.Exist("boundary-118"), BIO_OK);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, 17 * FileBytes("boundary-100", 100));
    EXPECT_EQ(events.size(), 2U);
}

TEST_F(TestLocalEviction, one_round_finishes_its_key_target_across_multiple_batches)
{
    Start();
    for (uint32_t i = 0; i < 1301; ++i) {
        const auto key = "round-" + std::to_string(10000 + i);
        ASSERT_EQ(system.Put(key.c_str(), "x", 1), BIO_OK);
    }
    const auto used = Usage().usedBytes;
    SetEvictionTarget(used * 100 / 95);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.mEvictionPolicy->Size(), 1170U);
    EXPECT_EQ(events.size(), 131U);
    EXPECT_EQ(Usage().usedBytes, 1170 * FileBytes("round-10000", 1));
}

TEST_F(TestLocalEviction, variable_sized_keys_trigger_additional_rounds_until_below_high_watermark)
{
    Start();
    for (uint32_t i = 0; i < 10; ++i) {
        const auto key = "small-0" + std::to_string(i);
        ASSERT_EQ(system.Put(key.c_str(), "x", 1), BIO_OK);
    }
    const std::string large(4096, 'x');
    ASSERT_EQ(system.Put("large-00", large.data(), large.size()), BIO_OK);
    const uint64_t targetBytes = FileBytes("small-09", 1) + FileBytes("large-00", large.size());
    // Reaching the 90% watermark requires nine cold small files while retaining the hot large file.
    SetEvictionTarget((targetBytes * 100 + 89) / 90);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(events.size(), 9U);
    EXPECT_EQ(system.mEvictionPolicy->Size(), 2U);
    EXPECT_EQ(Usage().usedBytes, targetBytes);
    EXPECT_LE(Usage().usedBytes, system.mCapacityBytes * 90 / 100);
    EXPECT_EQ(system.Exist("small-08"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("small-09"), BIO_OK);
    EXPECT_EQ(system.Exist("large-00"), BIO_OK);
}

TEST_F(TestLocalEviction, round_key_target_uses_index_size_and_skips_pending_placeholders)
{
    Start();
    for (uint32_t i = 0; i < 10; ++i) {
        const auto key = "key-0" + std::to_string(i);
        ASSERT_EQ(system.Put(key.c_str(), "x", 1), BIO_OK);
    }
    ASSERT_EQ(Reserve("pending", 1), BIO_OK);
    EXPECT_EQ(system.mEvictionPolicy->Size(), 11U);
    SetEvictionTarget((Usage().usedBytes + 1) * 100 / 95);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(events.size(), 2U);
    EXPECT_EQ(system.mEvictionPolicy->Size(), 9U);
    EXPECT_EQ(Usage().reservedBytes, 1U);
    EXPECT_EQ(system.Exist("pending"), BIO_NOT_EXISTS);
    system.ReleaseWriteSpace("pending", 1);
    EXPECT_EQ(system.mEvictionPolicy->Size(), 8U);
}

TEST_F(TestLocalEviction, a_round_evicts_available_candidates_and_stops_when_remaining_files_are_referenced)
{
    Start();
    for (uint32_t i = 0; i < 11; ++i) {
        const auto key = "key-" + std::to_string(100 + i);
        ASSERT_EQ(system.Put(key.c_str(), "x", 1), BIO_OK);
    }
    FileReference readers[10];
    FileRecord file;
    for (uint32_t i = 0; i < 10; ++i) {
        const auto key = "key-" + std::to_string(100 + i);
        ASSERT_EQ(system.mEvictionPolicy->Lookup(key, file, readers[i]), BIO_OK);
    }
    SetEvictionTarget(Usage().usedBytes * 100 / 95);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(events.size(), 1U);
    EXPECT_EQ(system.mEvictionPolicy->Size(), 10U);
    EXPECT_EQ(system.Exist("key-110"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("key-100"), BIO_OK);
}

TEST_F(TestLocalEviction, writes_reclaim_space_and_update_accounting)
{
    MockManagedCapacity(FileBytes("second") + FileBytes("third"));
    Start();
    Write("first", 'a', 1);
    Write("second", 'b', 2);
    Write("third", 'c', 3);
    EXPECT_EQ(system.Exist("first"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("second"), BIO_OK);
    EXPECT_EQ(system.Exist("third"), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, FileBytes("second") + FileBytes("third"));
    EXPECT_EQ(Usage().reservedBytes, 0U);
    ASSERT_EQ(system.Delete("second"), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, FileBytes("third"));
    ASSERT_EQ(system.Put("third", "new", 3), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, FileBytes("third"));
    EXPECT_EQ(Usage().files, 1U);
}

TEST_F(TestLocalEviction, eviction_unpublishes_the_complete_batch_before_unlinking_any_file)
{
    Start();
    Write("first", 'a', 1);
    Write("second", 'b', 2);
    const uint64_t bytes = Usage().usedBytes;
    SetEvictionTarget(bytes);
    spaceOwner = &system;
    unlinkAttempts = 0;
    MOCKER(unlinkat).stubs().will(invoke(ObserveUnpublishedBeforeEvictionUnlink));
    ASSERT_EQ(Reserve("pending", bytes), BIO_OK);
    EXPECT_EQ(unlinkAttempts, 2U);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, bytes);
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_EQ(events.size(), 2U);
    system.ReleaseWriteSpace("pending", bytes);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, oversized_new_keys_fail_and_duplicate_keys_succeed_at_full_capacity)
{
    MockManagedCapacity(FileBytes("key"));
    Start();
    Write("key", 'a', 1);
    const std::string large(4098, 'b');
    EXPECT_EQ(system.Put("oversized", large.data(), large.size()), BIO_UFS_IOERR);
    EXPECT_EQ(system.Put("key", "b", 1), BIO_OK);
    std::vector<char> data(4097);
    EXPECT_EQ(system.Get("key", data.data(), data.size(), 0), BIO_OK);
    EXPECT_EQ(data, std::vector<char>(4097, 'a'));
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key"));
}

TEST_F(TestLocalEviction, pending_writes_reserve_capacity_before_io)
{
    MockFileSystemCapacity(8194);
    Start();
    ASSERT_EQ(Reserve("pending-a", 6000), BIO_OK);
    EXPECT_EQ(Reserve("pending-b", 3000), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().reservedBytes, 6000U);
    system.ReleaseWriteSpace("pending-a", 6000);
    ASSERT_EQ(Reserve("pending-b", 3000), BIO_OK);
    system.ReleaseWriteSpace("pending-b", 3000);
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, filesystem_usage_does_not_evict_below_instance_watermark)
{
    config.localConfig.deviceCount = 2;
    Start();
    const std::string value(4096, 'x');
    ASSERT_EQ(system.Put("old", value.data(), value.size()), BIO_OK);
    ASSERT_EQ(system.Put("new", value.data(), value.size()), BIO_OK);
    spaceOwner = &system;
    MOCKER(fstatvfs).stubs().will(invoke(SimulatedFileSystemSpace));
    const auto used = Usage().usedBytes;
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.mCapacityBytes, 45000U);
    EXPECT_EQ(Usage().usedBytes, used);
    EXPECT_EQ(system.Exist("old"), BIO_OK);
    EXPECT_EQ(system.Exist("new"), BIO_OK);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, physical_space_shortage_reclaims_only_the_current_write_deficit)
{
    Start();
    const std::string value(4096, 'x');
    ASSERT_EQ(system.Put("old", value.data(), value.size()), BIO_OK);
    ASSERT_EQ(system.Put("new", value.data(), value.size()), BIO_OK);
    spaceOwner = &system;
    MOCKER(fstatvfs).stubs().will(invoke(SimulatedFileSystemSpace));
    writeAttempts = 0;
    MOCKER(pwritev).stubs().will(invoke(OutOfSpaceOnceVectorWrite));
    fileSystemSpaceQueries = 0;
    ASSERT_EQ(system.Put("incoming", value.data(), value.size()), BIO_OK);
    EXPECT_EQ(fileSystemSpaceQueries, 2U);
    EXPECT_EQ(writeAttempts, 2U);
    EXPECT_EQ(system.Exist("old"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("new"), BIO_OK);
    EXPECT_EQ(system.Exist("incoming"), BIO_OK);
    const auto used = FileBytes("new", value.size()) + FileBytes("incoming", value.size());
    EXPECT_EQ(Usage().usedBytes, used);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(events.size(), 1U);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, used);
    EXPECT_EQ(events.size(), 1U);
}

TEST_F(TestLocalEviction, instances_evict_against_their_own_watermarks_on_a_full_filesystem)
{
    config.localConfig.deviceCount = 2;
    MockManagedCapacity(50000);
    Start();
    const std::string value(4096, 'x');
    for (uint32_t i = 0; i < 11; ++i) {
        const auto key = "busy-" + std::to_string(i);
        ASSERT_EQ(system.Put(key.c_str(), value.data(), value.size()), BIO_OK);
    }
    ASSERT_GT(Usage().usedBytes, 45000U);
    config.localConfig.slotIndex = 1;
    UnderFsConfig::Initialize(config);
    LocalSystem peer;
    ASSERT_EQ(peer.Init(), BIO_OK);
    ASSERT_EQ(peer.Put("peer-a", value.data(), value.size()), BIO_OK);
    ASSERT_EQ(peer.Put("peer-b", value.data(), value.size()), BIO_OK);
    const auto peerUsed = peer.mUsedBytes;

    mockFileSystemAvailableBytes = 1000;
    ASSERT_EQ(peer.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(peer.mUsedBytes, peerUsed);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_LE(Usage().usedBytes, 45000U);
    EXPECT_EQ(Usage().files, 9U);
    EXPECT_EQ(events.size(), 2U);
    ASSERT_EQ(peer.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(peer.mUsedBytes, peerUsed);
    EXPECT_EQ(peer.Exist("peer-a"), BIO_OK);
    EXPECT_EQ(peer.Exist("peer-b"), BIO_OK);
    peer.Stop();
}

TEST_F(TestLocalEviction, writes_without_eviction_use_cached_capacity_without_space_queries)
{
    MockFileSystemCapacity(100000);
    Start();
    fileSystemSpaceQueries = 0;
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    EXPECT_EQ(fileSystemSpaceQueries, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, normal_writes_do_not_depend_on_a_live_filesystem_space_query)
{
    Start();
    MOCKER(fstatvfs).expects(never());
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, quota_pressure_queries_space_before_admitting_a_new_write)
{
    MockManagedCapacity(1000);
    Start();
    const std::string value(900, 'x');
    ASSERT_EQ(system.Put("old", value.data(), value.size()), BIO_OK);
    fileSystemSpaceQueries = 0;
    ASSERT_EQ(system.Put("new", "x", 1), BIO_OK);
    EXPECT_EQ(fileSystemSpaceQueries, 2U);
    EXPECT_EQ(system.Exist("old"), BIO_NOT_EXISTS);
    EXPECT_EQ(Usage().usedBytes, FileBytes("new", 1));
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, enospc_retries_once_without_double_reserving_or_using_clobbered_errno)
{
    MockManagedCapacity(FileBytes("key", 5) + 1);
    Start();
    writeAttempts = 0;
    fileSystemSpaceQueries = 0;
    MOCKER(pwritev).stubs().will(invoke(OutOfSpaceOnceVectorWrite));
    MOCKER(close).stubs().will(invoke(CloseAndClobberErrno));
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    EXPECT_EQ(writeAttempts, 2U);
    EXPECT_EQ(fileSystemSpaceQueries, 1U);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 1U);
    char value[5]{};
    ASSERT_EQ(system.Get("key", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "value");
}

TEST_F(TestLocalEviction, repeated_enospc_is_bounded_and_removes_both_temporary_files)
{
    MockFileSystemCapacity(100000);
    Start();
    writeAttempts = 0;
    unlinkAttempts = 0;
    fileSystemSpaceQueries = 0;
    MOCKER(pwritev).stubs().will(invoke(OutOfSpaceVectorWrite));
    MOCKER(unlink).stubs().will(invoke(ObserveTemporaryCleanup));
    EXPECT_EQ(system.Put("key", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(writeAttempts, 2U);
    EXPECT_EQ(unlinkAttempts, 2U);
    EXPECT_EQ(fileSystemSpaceQueries, 1U);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
}

TEST_F(TestLocalEviction, failed_enospc_cleanup_does_not_start_space_reclamation_or_retry)
{
    Start();
    writeAttempts = 0;
    MOCKER(pwritev).stubs().will(invoke(OutOfSpaceVectorWrite));
    MOCKER(unlink).stubs().will(invoke(FailTemporaryCleanup));
    MOCKER(fstatvfs).expects(never());
    EXPECT_EQ(system.Put("key", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(writeAttempts, 1U);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 0U);
}

TEST_F(TestLocalEviction, enospc_during_temporary_creation_reclaims_before_retrying)
{
    MockFileSystemCapacity(100000);
    Start();
    std::string hash, leaf;
    ASSERT_EQ(system.BuildHash("key", hash), BIO_OK);
    bool outOfSpace = false;
    ASSERT_EQ(system.EnsureLeafDirectory(hash, leaf, outOfSpace), BIO_OK);
    writeAttempts = 0;
    fileSystemSpaceQueries = 0;
    MOCKER(mkostemp).stubs().will(invoke(CreateTemporaryAfterNoSpace));
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    EXPECT_EQ(writeAttempts, 2U);
    EXPECT_EQ(fileSystemSpaceQueries, 1U);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, enospc_during_directory_creation_reclaims_before_retrying)
{
    MockFileSystemCapacity(100000);
    Start();
    writeAttempts = 0;
    fileSystemSpaceQueries = 0;
    MOCKER(mkdir).stubs().will(invoke(MakeDirectoryAfterNoSpace));
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    EXPECT_EQ(writeAttempts, 4U);
    EXPECT_EQ(fileSystemSpaceQueries, 1U);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, failed_space_query_rolls_back_the_inserted_key)
{
    Start();
    MOCKER(pwritev).stubs().will(invoke(OutOfSpaceVectorWrite));
    MOCKER(fstatvfs).stubs().will(returnValue(-1));
    EXPECT_EQ(system.Put("key", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_TRUE(CandidateKeys().empty());
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, insufficient_physical_space_preserves_referenced_files_and_rejects_write)
{
    MockFileSystemCapacity(100000);
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    FileRecord record;
    FileReference reader;
    ASSERT_EQ(system.mEvictionPolicy->Lookup("key", record, reader), BIO_OK);
    mockFileSystemAvailableBytes = 0;
    MOCKER(pwritev).stubs().will(invoke(OutOfSpaceVectorWrite));
    EXPECT_EQ(system.Put("incoming", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_EQ(system.Exist("key"), BIO_OK);
    EXPECT_EQ(system.Exist("incoming"), BIO_NOT_EXISTS);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, write_rechecks_physical_space_after_unlink_before_retrying)
{
    MockFileSystemCapacity(100000);
    Start();
    ASSERT_EQ(system.Put("old", "value", 5), BIO_OK);
    mockFileSystemAvailableBytes = 0;
    MOCKER(pwritev).stubs().will(invoke(OutOfSpaceVectorWrite));
    fileSystemSpaceQueries = 0;
    EXPECT_EQ(system.Put("incoming", "x", 1), BIO_UFS_IOERR);
    EXPECT_EQ(fileSystemSpaceQueries, 2U);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_EQ(system.Exist("incoming"), BIO_NOT_EXISTS);
    EXPECT_EQ(events.size(), 1U);
}

TEST_F(TestLocalEviction, unlink_failure_preserves_file_and_usage)
{
    Start();
    Write("key", 'a', 1);
    SetEvictionTarget(1);
    MOCKER(unlinkat).stubs().will(invoke(FailEvictionUnlink));
    EXPECT_EQ(system.CheckAndEvict(), BIO_UFS_IOERR);
    EXPECT_FALSE(system.mEvictionInProgress.load());
    EXPECT_EQ(system.Exist("key"), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key"));
    EXPECT_EQ(Usage().files, 1U);
}

TEST_F(TestLocalEviction, failed_batch_restores_all_unprocessed_candidates)
{
    Start();
    Write("first", 'a', 1);
    Write("second", 'b', 2);
    Write("third", 'c', 3);
    const auto order = CandidateKeys();
    SetEvictionTarget(FileBytes("first"));
    MOCKER(unlinkat).stubs().will(invoke(FailEvictionUnlink));
    EXPECT_EQ(system.CheckAndEvict(), BIO_UFS_IOERR);
    EXPECT_EQ(CandidateKeys(), order);
    EXPECT_EQ(Usage().usedBytes, FileBytes("first") + FileBytes("second") + FileBytes("third"));
    EXPECT_EQ(system.Exist("first"), BIO_OK);
    EXPECT_EQ(system.Exist("second"), BIO_OK);
    EXPECT_EQ(system.Exist("third"), BIO_OK);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, in_flight_read_reference_skips_eviction_until_io_completes)
{
    Start();
    Write("busy", 'a', 1);
    Write("idle", 'b', 2);
    SetEvictionTarget(FileBytes("busy"));
    ReadGate gate;
    readGate = &gate;
    MOCKER(pread).stubs().will(invoke(BlockingRead));
    std::vector<char> value(4097);
    auto reading = std::async(std::launch::async, [&]() {
        return system.Get("busy", value.data(), value.size(), 0);
    });
    bool entered = false;
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        entered = gate.wakeup.wait_for(lock, std::chrono::seconds(5), [&]() { return gate.entered; });
    }
    EXPECT_TRUE(entered);
    if (entered) {
        EXPECT_EQ(system.CheckAndEvict(), BIO_OK);
        EXPECT_EQ(system.Exist("idle"), BIO_NOT_EXISTS);
        EXPECT_EQ(system.Exist("busy"), BIO_OK);
        EXPECT_EQ(system.Delete("busy"), BIO_INNER_RETRY);
        EXPECT_EQ(Usage().usedBytes, FileBytes("busy"));
    }
    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.released = true;
    }
    gate.wakeup.notify_all();
    EXPECT_EQ(reading.get(), BIO_OK);
    EXPECT_EQ(value, std::vector<char>(4097, 'a'));
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.Exist("busy"), BIO_NOT_EXISTS);
    EXPECT_EQ(Usage().usedBytes, 0U);
}

TEST_F(TestLocalEviction, partially_deleted_batch_keeps_success_and_restores_remaining_files)
{
    Start();
    Write("first", 'a', 1);
    Write("second", 'b', 2);
    Write("third", 'c', 3);
    SetEvictionTarget(FileBytes("first"));
    unlinkAttempts = 0;
    MOCKER(unlinkat).stubs().will(invoke(FailSecondEvictionUnlink));
    EXPECT_EQ(system.CheckAndEvict(), BIO_UFS_IOERR);
    EXPECT_EQ(system.Exist("first"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("second"), BIO_OK);
    EXPECT_EQ(system.Exist("third"), BIO_OK);
    EXPECT_EQ(CandidateKeys(), (std::vector<std::string>{"second", "third"}));
    EXPECT_EQ(Usage().usedBytes, FileBytes("second") + FileBytes("third"));
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.front(), std::make_pair(UFS_LOCAL_META_DELETE, std::string("first")));
}

TEST_F(TestLocalEviction, duplicate_put_promotes_the_key_and_preserves_its_value)
{
    Start();
    Write("first", 'a', 1);
    Write("second", 'b', 2);
    Write("first", 'c', 3);
    EXPECT_EQ(Usage().usedBytes, FileBytes("first") + FileBytes("second"));
    EXPECT_EQ(Usage().files, 2U);
    EXPECT_EQ(CandidateKeys(), (std::vector<std::string>{"second", "first"}));
    char value = 0;
    ASSERT_EQ(system.Get("first", &value, 1, 0), BIO_OK);
    EXPECT_EQ(value, 'a');
    SetEvictionTarget(FileBytes("first") + FileBytes("second") - 1);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.Exist("second"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("first"), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, FileBytes("first"));
}

TEST_F(TestLocalEviction, default_lru_protects_successfully_read_file)
{
    EXPECT_EQ(config.localConfig.evictionPolicy, FileEvictionPolicyType::LRU);
    Start();
    Write("hot", 'a', 1);
    Write("cold", 'b', 2);
    char byte = 0;
    ASSERT_EQ(system.Get("hot", &byte, 1, 0), BIO_OK);
    EXPECT_EQ(byte, 'a');
    SetEvictionTarget(Usage().usedBytes - 1);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.Exist("cold"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("hot"), BIO_OK);
}

TEST_F(TestLocalEviction, lookup_hits_promote_for_failed_empty_and_metadata_reads)
{
    Start();
    Write("old", 'a', 1);
    Write("new", 'b', 2);
    char byte = 0;
    EXPECT_EQ(system.Get("missing", &byte, 1, 0), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Get("old", &byte, 1, 4097), BIO_READ_EXCEED);
    EXPECT_EQ(system.Get("old", &byte, 0, 0), BIO_OK);
    std::vector<char> tooLong(4098);
    EXPECT_EQ(system.Get("old", tooLong.data(), tooLong.size(), 0), BIO_READ_EXCEED);
    size_t realLen = 1;
    EXPECT_EQ(system.GetWithRealLen("old", &byte, 1, 4097, realLen), BIO_READ_EXCEED);
    EXPECT_EQ(realLen, 0U);
    FileSystem::ObjStat status{};
    EXPECT_EQ(system.Stat("old", status), BIO_OK);
    EXPECT_EQ(system.Exist("old"), BIO_OK);
    EXPECT_EQ(CandidateKeys(), (std::vector<std::string>{"new", "old"}));
    SetEvictionTarget(Usage().usedBytes - 1);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.Exist("old"), BIO_OK);
    EXPECT_EQ(system.Exist("new"), BIO_NOT_EXISTS);
}

TEST_F(TestLocalEviction, successful_partial_read_promotes_file)
{
    Start();
    Write("hot", 'a', 1);
    Write("cold", 'b', 2);
    char buffer[2]{};
    size_t realLen = 0;
    ASSERT_EQ(system.GetWithRealLen("hot", buffer, sizeof(buffer), 4096, realLen), BIO_OK);
    EXPECT_EQ(realLen, 1U);
    SetEvictionTarget(Usage().usedBytes - 1);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.Exist("cold"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("hot"), BIO_OK);
}

TEST_F(TestLocalEviction, put_reserves_new_files_and_duplicate_put_uses_no_additional_capacity)
{
    MockManagedCapacity(FileBytes("key", 7));
    Start();
    EXPECT_EQ(system.Put("key", "abcdabcd", 8), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    ASSERT_EQ(system.Put("key", "abcabcd", 7), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 7));
    EXPECT_EQ(system.Put("key", "different", 9), BIO_OK);
    char value[7]{};
    ASSERT_EQ(system.Get("key", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "abcabcd");
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 7));
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, failed_unlink_does_not_report_metadata_deletion)
{
    Start();
    Write("key", 'a', 1);
    SetEvictionTarget(1);
    MOCKER(unlinkat).stubs().will(invoke(FailEvictionUnlink));
    EXPECT_EQ(system.CheckAndEvict(), BIO_UFS_IOERR);
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(Usage().usedBytes, FileBytes("key"));
}

}
