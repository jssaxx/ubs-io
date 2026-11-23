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

namespace local_system_test {
LocalSystem *spaceOwner = nullptr;
uint32_t writeAttempts = 0;
uint32_t unlinkAttempts = 0;
uint64_t mockFileSystemTotalBytes = 0;
uint64_t mockFileSystemAvailableBytes = 0;
uint32_t fileSystemSpaceQueries = 0;
std::thread::id writeThread;

ssize_t ObserveWrite(int fd, const struct iovec *iov, int iovcnt, off_t offset) noexcept
{
    writeThread = std::this_thread::get_id();
    EXPECT_EQ(fcntl(fd, F_GETFL) & O_DIRECT, 0);
    size_t written = 0;
    for (int i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_len == 0) {
            continue;
        }
        ssize_t result = syscall(SYS_pwrite64, fd, iov[i].iov_base, iov[i].iov_len, offset + written);
        if (result < 0) {
            return written == 0 ? result : static_cast<ssize_t>(written);
        }
        written += result;
        if (static_cast<size_t>(result) != iov[i].iov_len) {
            break;
        }
    }
    return static_cast<ssize_t>(written);
}

ReadGate *readGate = nullptr;
ReadGate *spaceGate = nullptr;

ssize_t BlockingRead(int fd, void *value, size_t len, off_t offset)
{
    {
        std::unique_lock<std::mutex> lock(readGate->mutex);
        readGate->entered = true;
        readGate->wakeup.notify_all();
        readGate->wakeup.wait(lock, []() { return readGate->released; });
    }
    return syscall(SYS_pread64, fd, value, len, offset);
}

ssize_t OutOfSpaceVectorWrite(int, const struct iovec *, int, off_t) noexcept
{
    ++writeAttempts;
    errno = ENOSPC;
    return -1;
}

int ProbeFileSystemCapacity(int, struct statvfs *space) noexcept
{
    ++fileSystemSpaceQueries;
    *space = {};
    space->f_frsize = 1;
    space->f_blocks = mockFileSystemTotalBytes;
    space->f_bavail = mockFileSystemAvailableBytes;
    return 0;
}

int FailEvictionUnlink(int, const char *, int)
{
    errno = EACCES;
    return -1;
}

int ObserveTemporaryCleanup(const char *path) noexcept
{
    ++unlinkAttempts;
    return static_cast<int>(syscall(SYS_unlinkat, AT_FDCWD, path, 0));
}
}

using namespace ock::bio;
using namespace local_system_test;

namespace {

std::thread::id readThread;
bool directRead = false;
size_t lastReadLength = 0;
off_t lastReadOffset = 0;

ssize_t ObserveRead(int fd, void *value, size_t len, off_t offset)
{
    readThread = std::this_thread::get_id();
    directRead = (fcntl(fd, F_GETFL) & O_DIRECT) != 0;
    lastReadLength = len;
    lastReadOffset = offset;
    return syscall(SYS_pread64, fd, value, len, offset);
}

ssize_t InterruptedShortVectorWrite(int fd, const struct iovec *iov, int iovcnt, off_t offset) noexcept
{
    if (++writeAttempts == 1) {
        errno = EINTR;
        return -1;
    }
    for (int i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_len != 0) {
            return syscall(SYS_pwrite64, fd, iov[i].iov_base, std::min<size_t>(iov[i].iov_len, 4), offset);
        }
    }
    return 0;
}

ssize_t FailedVectorWrite(int, const struct iovec *, int, off_t) noexcept
{
    errno = EIO;
    return -1;
}

ssize_t ShortThenOutOfSpaceVectorWrite(int fd, const struct iovec *iov, int iovcnt, off_t offset) noexcept
{
    ++writeAttempts;
    if (writeAttempts == 1) {
        return syscall(SYS_pwrite64, fd, iov[0].iov_base, 2, offset);
    }
    if (writeAttempts == 2) {
        errno = ENOSPC;
        return -1;
    }
    EXPECT_EQ(offset, 0);
    return ObserveWrite(fd, iov, iovcnt, offset);
}

ssize_t ZeroVectorWriteWithStaleErrno(int, const struct iovec *, int, off_t) noexcept
{
    errno = ENOSPC;
    return 0;
}

int CloseAfterNoSpace(int fd) noexcept
{
    int result = static_cast<int>(syscall(SYS_close, fd));
    if (++writeAttempts == 1) {
        errno = ENOSPC;
        return -1;
    }
    return result;
}

int PublishAfterNoSpace(const char *source, const char *target) noexcept
{
    if (++writeAttempts == 1) {
        errno = ENOSPC;
        return -1;
    }
    return static_cast<int>(syscall(SYS_renameat, AT_FDCWD, source, AT_FDCWD, target));
}

ssize_t FailedFooterWrite(int fd, const struct iovec *iov, int iovcnt, off_t offset) noexcept
{
    if (offset == 0 && iovcnt > 1) {
        return syscall(SYS_pwrite64, fd, iov[0].iov_base, iov[0].iov_len, offset);
    }
    return FailedVectorWrite(fd, iov, iovcnt, offset);
}

TEST_F(TestLocalEviction, reads_and_writes_execute_on_the_calling_thread)
{
    Start();
    MOCKER(pwritev).stubs().will(invoke(ObserveWrite));
    MOCKER(pread).stubs().will(invoke(ObserveRead));
    const auto caller = std::this_thread::get_id();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    EXPECT_EQ(writeThread, caller);
    char value[5]{};
    ASSERT_EQ(system.Get("key", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(readThread, caller);
    EXPECT_EQ(std::string(value, sizeof(value)), "value");
}

TEST_F(TestLocalEviction, synchronous_calls_require_initialization_and_can_restart_after_stop)
{
    char output[5]{};
    size_t realLen = 42;
    EXPECT_EQ(system.Put("key", "value", 5), BIO_NOT_READY);
    EXPECT_EQ(system.GetWithRealLen("key", output, sizeof(output), 0, realLen), BIO_NOT_READY);
    EXPECT_EQ(realLen, 0U);
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    system.Stop();
    EXPECT_EQ(system.Get("key", output, sizeof(output), 0), BIO_NOT_READY);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_READY);
    EXPECT_EQ(system.Delete("key"), BIO_NOT_READY);
    Start();
    ASSERT_EQ(system.Get("key", output, sizeof(output), 0), BIO_OK);
    EXPECT_EQ(std::string(output, sizeof(output)), "value");
}

TEST_F(TestLocalEviction, metadata_operations_return_results_directly)
{
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    FileSystem::ObjStat stat{};
    EXPECT_EQ(system.Exist("key"), BIO_OK);
    EXPECT_EQ(system.Stat("key", stat), BIO_OK);
    EXPECT_EQ(stat.size, 5U);
    EXPECT_EQ(system.Delete("key"), BIO_OK);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
    system.Stop();
    EXPECT_EQ(system.Stat("key", stat), BIO_NOT_READY);
}

TEST_F(TestLocalEviction, stop_waits_for_synchronous_file_access_before_releasing_resources)
{
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    ReadGate gate;
    readGate = &gate;
    MOCKER(pread).stubs().will(invoke(BlockingRead));
    char value[5]{};
    BResult result = BIO_ERR;
    std::thread caller([&]() {
        result = system.Get("key", value, sizeof(value), 0);
    });
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        EXPECT_TRUE(gate.wakeup.wait_for(lock, std::chrono::seconds(5), [&]() { return gate.entered; }));
    }
    std::atomic<bool> stopping{false};
    std::atomic<bool> stopped{false};
    std::thread stopper([&]() {
        stopping.store(true);
        system.Stop();
        stopped.store(true);
    });
    while (!stopping.load()) {
        std::this_thread::yield();
    }
    EXPECT_FALSE(stopped.load());
    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.released = true;
    }
    gate.wakeup.notify_all();
    caller.join();
    stopper.join();
    EXPECT_EQ(result, BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "value");
    EXPECT_TRUE(stopped.load());
    EXPECT_EQ(system.Get("key", value, sizeof(value), 0), BIO_NOT_READY);
}

TEST_F(TestLocalEviction, slots_isolate_files_indexes_capacity_and_deletion)
{
    config.localConfig.deviceCount = 2;
    MockFileSystemCapacity(1024 * 1024 + 1);
    Start();
    EXPECT_EQ(system.mRootPath, root + "/slot_0");
    EXPECT_EQ(system.mCapacityBytes, (uint64_t{1024} * 1024 + 1) * 9 / 10 / 2);
    ASSERT_EQ(system.Put("same-key", "first", 5), BIO_OK);
    ASSERT_EQ(system.Put("slot-zero", "zero", 4), BIO_OK);

    config.localConfig.slotIndex = 1;
    UnderFsConfig::Initialize(config);
    LocalSystem peer;
    ASSERT_EQ(peer.Init(), BIO_OK);
    EXPECT_EQ(peer.mRootPath, root + "/slot_1");
    EXPECT_EQ(peer.mCapacityBytes, system.mCapacityBytes);
    EXPECT_EQ(peer.Exist("slot-zero"), BIO_NOT_EXISTS);
    ASSERT_EQ(peer.Put("same-key", "second", 6), BIO_OK);
    ASSERT_EQ(peer.Put("slot-one", "one", 3), BIO_OK);
    EXPECT_EQ(system.Exist("slot-one"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.mUsedBytes, FileBytes("same-key", 5) + FileBytes("slot-zero", 4));
    EXPECT_EQ(peer.mUsedBytes, FileBytes("same-key", 6) + FileBytes("slot-one", 3));
    UfsStorageMetrics metrics{}, peerMetrics{};
    ASSERT_EQ(system.GetStorageMetrics(metrics), BIO_OK);
    ASSERT_EQ(peer.GetStorageMetrics(peerMetrics), BIO_OK);
    EXPECT_EQ(metrics.totalCapacity, (uint64_t{1024} * 1024 + 1) * 9 / 10 / 2);
    EXPECT_EQ(peerMetrics.totalCapacity, metrics.totalCapacity);
    EXPECT_EQ(metrics.usedBytes, FileBytes("same-key", 5) + FileBytes("slot-zero", 4));
    EXPECT_EQ(peerMetrics.usedBytes, FileBytes("same-key", 6) + FileBytes("slot-one", 3));

    char value[6]{};
    ASSERT_EQ(system.Get("same-key", value, 5, 0), BIO_OK);
    EXPECT_EQ(std::string(value, 5), "first");
    ASSERT_EQ(peer.Get("same-key", value, 6, 0), BIO_OK);
    EXPECT_EQ(std::string(value, 6), "second");
    std::unordered_map<std::string, uint64_t> keys;
    ASSERT_EQ(SnapshotKeys(keys), BIO_OK);
    EXPECT_EQ(keys, (std::unordered_map<std::string, uint64_t>{{"same-key", 5}, {"slot-zero", 4}}));

    SetEvictionTarget(1);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.mEvictionPolicy->Size(), 0U);
    EXPECT_EQ(events.size(), 2U);
    EXPECT_EQ(peer.Exist("same-key"), BIO_OK);
    EXPECT_EQ(peer.Exist("slot-one"), BIO_OK);
    peer.Stop();
}

TEST_F(TestLocalEviction, rejects_invalid_storage_slots_and_symlinked_slot_directories)
{
    config.localConfig.deviceCount = 0;
    UnderFsConfig::Initialize(config);
    EXPECT_EQ(system.Init(), BIO_INVALID_PARAM);
    config.localConfig.deviceCount = DEVICE_SIZE + 1;
    UnderFsConfig::Initialize(config);
    EXPECT_EQ(system.Init(), BIO_INVALID_PARAM);
    config.localConfig.deviceCount = 2;
    config.localConfig.slotIndex = 2;
    UnderFsConfig::Initialize(config);
    EXPECT_EQ(system.Init(), BIO_INVALID_PARAM);
    config.localConfig.slotIndex = 1;
    ASSERT_EQ(symlink(root.c_str(), (root + "/slot_1").c_str()), 0);
    UnderFsConfig::Initialize(config);
    EXPECT_EQ(system.Init(), BIO_UFS_IOERR);
}

TEST_F(TestLocalEviction, partial_enospc_write_restarts_from_the_original_vectors)
{
    MockFileSystemCapacity(100000);
    Start();
    writeAttempts = 0;
    fileSystemSpaceQueries = 0;
    MOCKER(pwritev).stubs().will(invoke(ShortThenOutOfSpaceVectorWrite));
    char first[] = "first";
    char second[] = "second";
    const struct iovec vectors[]{{first, 5}, {second, 6}};
    ASSERT_EQ(system.PutV("key", vectors, 2), BIO_OK);
    EXPECT_EQ(writeAttempts, 3U);
    EXPECT_EQ(fileSystemSpaceQueries, 1U);
    EXPECT_EQ(vectors[0].iov_base, first);
    EXPECT_EQ(vectors[0].iov_len, 5U);
    EXPECT_EQ(vectors[1].iov_base, second);
    EXPECT_EQ(vectors[1].iov_len, 6U);
    char value[11]{};
    ASSERT_EQ(system.Get("key", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "firstsecond");
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 11));
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, non_space_write_errors_do_not_query_capacity_or_retry)
{
    Start();
    MOCKER(fstatvfs).expects(never());
    MOCKER(pwritev).expects(once()).will(invoke(FailedVectorWrite));
    EXPECT_EQ(system.Put("key", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 0U);
}

TEST_F(TestLocalEviction, zero_byte_write_does_not_treat_stale_errno_as_enospc)
{
    Start();
    MOCKER(fstatvfs).expects(never());
    MOCKER(pwritev).expects(once()).will(invoke(ZeroVectorWriteWithStaleErrno));
    EXPECT_EQ(system.Put("key", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 0U);
}

TEST_F(TestLocalEviction, enospc_on_close_retries_without_publishing_the_failed_file)
{
    MockFileSystemCapacity(100000);
    Start();
    writeAttempts = 0;
    fileSystemSpaceQueries = 0;
    MOCKER(close).stubs().will(invoke(CloseAfterNoSpace));
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    EXPECT_EQ(writeAttempts, 2U);
    EXPECT_EQ(fileSystemSpaceQueries, 1U);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, enospc_on_rename_keeps_the_write_reference_until_the_retry_commits)
{
    MockFileSystemCapacity(100000);
    Start();
    writeAttempts = 0;
    fileSystemSpaceQueries = 0;
    MOCKER(rename).stubs().will(invoke(PublishAfterNoSpace));
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    EXPECT_EQ(writeAttempts, 2U);
    EXPECT_EQ(fileSystemSpaceQueries, 1U);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 1U);
}

TEST_F(TestLocalEviction, duplicate_put_skips_space_checks_hashing_and_file_writes)
{
    TraceEnableGuard traceGuard;
    MockFileSystemCapacity(100000);
    Start();
    ASSERT_EQ(system.Put("key", "original", 8), BIO_OK);
    FileRecord record;
    FileReference reader;
    ASSERT_EQ(system.mEvictionPolicy->Lookup("key", record, reader), BIO_OK);
    auto *hash = ock::htracer::HtracerManager::GetPointPtr(UFS_TRACE_HASH_KEY);
    auto *write = ock::htracer::HtracerManager::GetPointPtr(UFS_TRACE_PUT_WRITEV);
    const auto hashBefore = hash->GetBegin();
    const auto writeBefore = write->GetBegin();
    const auto originalHash = system.mSha256;
    system.mSha256 = nullptr;
    mockFileSystemAvailableBytes = 0;
    fileSystemSpaceQueries = 0;
    MOCKER(pwritev).stubs().will(invoke(FailedVectorWrite));
    EXPECT_EQ(system.Put("key", "different", 9), BIO_OK);
    EXPECT_EQ(fileSystemSpaceQueries, 0U);
    EXPECT_EQ(hash->GetBegin(), hashBefore);
    EXPECT_EQ(write->GetBegin(), writeBefore);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 8));
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_TRUE(events.empty());
    system.mSha256 = originalHash;
    reader.Reset();
    char value[8]{};
    ASSERT_EQ(system.Get("key", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "original");
    EXPECT_EQ(system.Delete("key"), BIO_OK);
}

TEST_F(TestLocalEviction, duplicate_pending_put_keeps_the_original_reservation_until_rollback)
{
    MockFileSystemCapacity(100000);
    Start();
    const uint64_t reserved = FileBytes("pending", 5);
    ASSERT_EQ(Reserve("pending", reserved), BIO_OK);
    fileSystemSpaceQueries = 0;
    EXPECT_EQ(system.Put("pending", "other", 5), BIO_OK);
    EXPECT_EQ(fileSystemSpaceQueries, 0U);
    EXPECT_EQ(Usage().reservedBytes, reserved);
    EXPECT_EQ(system.mEvictionPolicy->Size(), 1U);
    EXPECT_EQ(system.Exist("pending"), BIO_NOT_EXISTS);
    std::unordered_map<std::string, uint64_t> snapshot;
    ASSERT_EQ(SnapshotKeys(snapshot), BIO_OK);
    EXPECT_TRUE(snapshot.empty());
    ASSERT_EQ(system.Put("published", "value", 5), BIO_OK);
    system.ReleaseWriteSpace("pending", reserved);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_EQ(CandidateKeys(), (std::vector<std::string>{"published"}));
}

TEST_F(TestLocalEviction, failed_hash_rolls_back_the_inserted_key_and_write_reservation)
{
    Start();
    const auto originalHash = system.mSha256;
    system.mSha256 = nullptr;
    EXPECT_EQ(system.Put("key", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_TRUE(CandidateKeys().empty());
    system.mSha256 = originalHash;
    EXPECT_EQ(system.Put("key", "value", 5), BIO_OK);
}

TEST_F(TestLocalEviction, failed_publication_rolls_back_the_index_queue_and_write_reservation)
{
    Start();
    unlinkAttempts = 0;
    MOCKER(unlink).stubs().will(invoke(ObserveTemporaryCleanup));
    MOCKER(rename).stubs().will(returnValue(-1));
    EXPECT_EQ(system.Put("key", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(unlinkAttempts, 1U);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_TRUE(CandidateKeys().empty());
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, open_reader_keeps_the_selected_file_contents)
{
    Start();
    Write("key", 'a', 1);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("key", path), BIO_OK);
    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    struct CloseReader {
        int fd;
        ~CloseReader() { close(fd); }
    } reader{fd};
    SetEvictionTarget(1);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
    char value = 0;
    EXPECT_EQ(pread(fd, &value, 1, 0), 1);
    EXPECT_EQ(value, 'a');
}

TEST_F(TestLocalEviction, failed_file_creation_releases_write_reservation)
{
    Start();
    std::string hash;
    ASSERT_EQ(system.BuildHash("blocked", hash), BIO_OK);
    const std::string first = system.mRootPath + "/" + hash.substr(0, 2);
    ASSERT_EQ(mkdir(first.c_str(), 0700), 0);
    const std::string leaf = first + "/" + hash.substr(2, 3);
    std::ofstream(leaf) << "not a directory";
    EXPECT_EQ(system.Put("blocked", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_TRUE(Usage().files == 0);
}

TEST_F(TestLocalEviction, c_configuration_validates_the_interval_at_initialization)
{
    auto options = MakeCConfig();
    for (uint32_t interval : {0U, 3601U}) {
        options.localConfig.checkIntervalSeconds = interval;
        UfsInitUnderFsConfig(options);
        EXPECT_EQ(system.Init(), BIO_INVALID_PARAM);
    }
    options.localConfig.checkIntervalSeconds = 10;
    UfsInitUnderFsConfig(options);
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_NE(system.mEvictionPolicy, nullptr);
    EXPECT_TRUE(system.mEvictionThread.joinable());
    EXPECT_EQ(system.mCheckIntervalSeconds, 10U);
}

TEST_F(TestLocalEviction, c_configuration_validates_and_passes_storage_slots)
{
    MockFileSystemCapacity(1024 * 1024);
    auto options = MakeCConfig();
    options.localConfig.deviceCount = 0;
    UfsInitUnderFsConfig(options);
    EXPECT_EQ(system.Init(), BIO_INVALID_PARAM);
    options.localConfig.deviceCount = DEVICE_SIZE + 1;
    UfsInitUnderFsConfig(options);
    EXPECT_EQ(system.Init(), BIO_INVALID_PARAM);
    options.localConfig.deviceCount = 2;
    options.localConfig.slotIndex = 2;
    UfsInitUnderFsConfig(options);
    EXPECT_EQ(system.Init(), BIO_INVALID_PARAM);
    options.localConfig.slotIndex = 1;
    UfsInitUnderFsConfig(options);
    const auto &local = UnderFsConfig::Instance()->GetUnderFsConfig().localConfig;
    EXPECT_EQ(local.slotIndex, 1U);
    EXPECT_EQ(local.deviceCount, 2U);
    EXPECT_EQ(local.evictionPolicy, FileEvictionPolicyType::LRU);
    EXPECT_EQ(local.checkIntervalSeconds, options.localConfig.checkIntervalSeconds);
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_EQ(system.mRootPath, root + "/slot_1");
    EXPECT_EQ(system.mCapacityBytes, 512U * 1024 * 9 / 10);
}

TEST_F(TestLocalEviction, lookup_miss_does_not_recreate_deleted_file)
{
    Start();
    Write("key", 'a', 1);
    ASSERT_EQ(system.Delete("key"), BIO_OK);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
    EXPECT_TRUE(Usage().files == 0);
    EXPECT_TRUE(CandidateKeys().empty());
    EXPECT_EQ(Usage().usedBytes, 0U);
}

TEST_F(TestLocalEviction, c_configuration_validates_the_policy_at_initialization)
{
    auto options = MakeCConfig();
    options.localConfig.evictionPolicy = nullptr;
    UfsInitUnderFsConfig(options);
    EXPECT_EQ(system.Init(), BIO_INVALID_PARAM);
    for (const char *name : {"", "unknown", "fifo"}) {
        options.localConfig.evictionPolicy = name;
        UfsInitUnderFsConfig(options);
        EXPECT_EQ(system.Init(), BIO_INVALID_PARAM);
    }
    options.localConfig.evictionPolicy = "lru";
    UfsInitUnderFsConfig(options);
    EXPECT_EQ(UnderFsConfig::Instance()->GetUnderFsConfig().localConfig.evictionPolicy, FileEvictionPolicyType::LRU);
    ASSERT_EQ(system.Init(), BIO_OK);
}

TEST_F(TestLocalEviction, c_configuration_copies_local_values_before_returning)
{
    MockFileSystemCapacity(1024 * 1024);
    auto options = MakeCConfig();
    std::string configuredRoot = root;
    char policy[] = "lru";
    options.localConfig = {configuredRoot.c_str(), 1, 2, policy, 120};
    UfsInitUnderFsConfig(options);
    configuredRoot = "changed";
    policy[0] = 'x';
    options.localConfig.slotIndex = 0;
    options.localConfig.deviceCount = 1;
    options.localConfig.checkIntervalSeconds = 0;
    const auto &applied = UnderFsConfig::Instance()->GetUnderFsConfig().localConfig;
    EXPECT_EQ(applied.rootPath, root);
    EXPECT_EQ(applied.checkIntervalSeconds, 120U);
    EXPECT_EQ(applied.slotIndex, 1U);
    EXPECT_EQ(applied.deviceCount, 2U);
    EXPECT_EQ(applied.evictionPolicy, FileEvictionPolicyType::LRU);
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_EQ(system.mRootPath, root + "/slot_1");
    EXPECT_EQ(system.mCapacityBytes, 512U * 1024 * 9 / 10);
    EXPECT_EQ(system.mCheckIntervalSeconds, 120U);
}

TEST_F(TestLocalEviction, aligned_512_byte_io_preserves_value_offsets_and_recovery)
{
    Start();
    MOCKER(pwritev).stubs().will(invoke(ObserveWrite));
    MOCKER(pread).stubs().will(invoke(ObserveRead));
    void *memory = nullptr;
    ASSERT_EQ(posix_memalign(&memory, 4096, 4096), 0);
    std::unique_ptr<void, decltype(&free)> owner(memory, free);
    // The value buffer is aligned to 512 bytes but not to 4096 bytes.
    auto *data = static_cast<char *>(memory) + 512;
    memset(data, 'a', 512);
    memset(data + 512, 'c', 512);
    memset(data + 1024, 'b', 512);
    ASSERT_EQ(system.Put("aligned", data, 1536), BIO_OK);
    const std::string expected = std::string(512, 'a') + std::string(512, 'c') + std::string(512, 'b');
    ASSERT_EQ(system.Get("aligned", data, expected.size(), 0), BIO_OK);
    EXPECT_TRUE(directRead);
    EXPECT_EQ(lastReadOffset, 0);
    EXPECT_EQ(lastReadLength, expected.size());
    EXPECT_EQ(std::string(data, expected.size()), expected);
    EXPECT_EQ(Usage().usedBytes, FileBytes("aligned", expected.size()));
    EXPECT_EQ(Usage().reservedBytes, 0U);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("aligned", path), BIO_OK);
    struct stat status{};
    ASSERT_EQ(stat(path.c_str(), &status), 0);
    EXPECT_EQ(status.st_size, FileBytes("aligned", expected.size()));
    ASSERT_EQ(system.Get("aligned", data, 512, 512), BIO_OK);
    EXPECT_EQ(std::string(data, 512), std::string(512, 'c'));
    system.Stop();
    Start();
    EXPECT_FALSE(directRead);
    EXPECT_EQ(lastReadLength, LOCAL_FILE_MAX_METADATA_SIZE);
    EXPECT_EQ(lastReadOffset, FileBytes("aligned", expected.size()) - LOCAL_FILE_MAX_METADATA_SIZE);
    ASSERT_EQ(system.Get("aligned", data, expected.size(), 0), BIO_OK);
    EXPECT_TRUE(directRead);
    EXPECT_EQ(std::string(data, expected.size()), expected);
    EXPECT_EQ(Usage().usedBytes, FileBytes("aligned", expected.size()));
}

TEST_F(TestLocalEviction, reads_never_expose_key_or_footer_bytes)
{
    Start();
    MOCKER(pread).stubs().will(invoke(ObserveRead));
    const std::string value(513, 'v');
    ASSERT_EQ(system.Put("tail-key", value.data(), value.size()), BIO_OK);
    alignas(512) char output[1024];
    memset(output, '?', sizeof(output));
    size_t realLen = 0;
    ASSERT_EQ(system.GetWithRealLen("tail-key", output, sizeof(output), 0, realLen), BIO_OK);
    EXPECT_EQ(realLen, value.size());
    EXPECT_EQ(std::string(output, realLen), value);
    EXPECT_EQ(std::string(output + realLen, sizeof(output) - realLen), std::string(511, '?'));
    EXPECT_FALSE(directRead);
    EXPECT_EQ(lastReadLength, value.size());
    memset(output, '?', sizeof(output));
    EXPECT_EQ(system.Get("tail-key", output, sizeof(output), 0), BIO_READ_EXCEED);
    EXPECT_EQ(output[513], '?');
    ASSERT_EQ(system.GetWithRealLen("tail-key", output, 512, 512, realLen), BIO_OK);
    EXPECT_EQ(realLen, 1U);
    EXPECT_EQ(output[0], 'v');
    EXPECT_FALSE(directRead);
    EXPECT_EQ(system.GetWithRealLen("tail-key", output, 1, 513, realLen), BIO_READ_EXCEED);
    EXPECT_EQ(realLen, 0U);
    system.Stop();
    Start();
    memset(output, '?', sizeof(output));
    ASSERT_EQ(system.GetWithRealLen("tail-key", output, sizeof(output), 0, realLen), BIO_OK);
    EXPECT_EQ(realLen, value.size());
    EXPECT_EQ(output[513], '?');
}

TEST_F(TestLocalEviction, put_retries_interruption_and_short_writes)
{
    Start();
    writeAttempts = 0;
    MOCKER(pwritev).stubs().will(invoke(InterruptedShortVectorWrite));
    ASSERT_EQ(system.Put("short", "abcdefghij", 10), BIO_OK);
    EXPECT_GT(writeAttempts, 2U);
    char value[10]{};
    ASSERT_EQ(system.Get("short", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "abcdefghij");
    EXPECT_EQ(Usage().usedBytes, FileBytes("short", sizeof(value)));
}

TEST_F(TestLocalEviction, putv_preserves_aligned_segments_and_recovers_value)
{
    Start();
    alignas(512) char first[512];
    alignas(512) char second[1024];
    alignas(512) char value[1536]{};
    memset(first, 'a', sizeof(first));
    memset(second, 'b', sizeof(second));
    const struct iovec buffers[]{{first, sizeof(first)}, {nullptr, 0}, {second, sizeof(second)}};
    ASSERT_EQ(system.PutV("vectors", buffers, 3), BIO_OK);
    ASSERT_EQ(system.Get("vectors", value, sizeof(value), 0), BIO_OK);
    const std::string expected = std::string(sizeof(first), 'a') + std::string(sizeof(second), 'b');
    EXPECT_EQ(std::string(value, sizeof(value)), expected);
    EXPECT_EQ(Usage().usedBytes, FileBytes("vectors", sizeof(value)));
    EXPECT_EQ(Usage().reservedBytes, 0U);
    system.Stop();
    Start();
    ASSERT_EQ(system.Get("vectors", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), expected);
}

TEST_F(TestLocalEviction, putv_retries_short_writes_without_modifying_caller_vectors)
{
    Start();
    char first[] = "abc";
    char second[] = "defghij";
    const struct iovec buffers[]{{first, 3}, {second, 7}};
    writeAttempts = 0;
    MOCKER(pwritev).stubs().will(invoke(InterruptedShortVectorWrite));
    ASSERT_EQ(system.PutV("short-vectors", buffers, 2), BIO_OK);
    EXPECT_GT(writeAttempts, 2U);
    EXPECT_EQ(buffers[0].iov_base, first);
    EXPECT_EQ(buffers[0].iov_len, 3U);
    EXPECT_EQ(buffers[1].iov_base, second);
    EXPECT_EQ(buffers[1].iov_len, 7U);
    char value[10]{};
    ASSERT_EQ(system.Get("short-vectors", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "abcdefghij");
}

TEST_F(TestLocalEviction, duplicate_putv_succeeds_without_writing_or_changing_the_value)
{
    Start();
    ASSERT_EQ(system.Put("key", "old", 3), BIO_OK);
    char first[] = "new";
    char second[] = "value";
    const struct iovec buffers[]{{first, 3}, {second, 5}};
    MOCKER(pwritev).stubs().will(invoke(FailedVectorWrite));
    EXPECT_EQ(system.PutV("key", buffers, 2), BIO_OK);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 3));
    char value[3]{};
    ASSERT_EQ(system.Get("key", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "old");
}

TEST_F(TestLocalEviction, putv_batches_vectors_above_the_syscall_limit)
{
    Start();
    char byte = 'v';
    const std::vector<struct iovec> buffers(IOV_MAX + 2, {&byte, 1});
    ASSERT_EQ(system.PutV("many-vectors", buffers.data(), buffers.size()), BIO_OK);
    std::string value(buffers.size(), '\0');
    ASSERT_EQ(system.Get("many-vectors", &value[0], value.size(), 0), BIO_OK);
    EXPECT_EQ(value, std::string(buffers.size(), byte));
}

TEST_F(TestLocalEviction, putv_validates_each_segment_and_total_length)
{
    Start();
    MOCKER(pwritev).expects(never());
    char byte = 'x';
    const struct iovec invalid[]{{&byte, 1}, {nullptr, 1}};
    const struct iovec overflow[]{{&byte, IO_MAX_LEN}, {&byte, 1}};
    const struct iovec unrepresentable[]{{&byte, LOCAL_FILE_MAX_VALUE_SIZE}, {&byte, 1}};
    EXPECT_EQ(system.PutV("invalid", nullptr, 1), BIO_INVALID_PARAM);
    EXPECT_EQ(system.PutV("invalid", invalid, 0), BIO_INVALID_PARAM);
    EXPECT_EQ(system.PutV("invalid", invalid, 2), BIO_INVALID_PARAM);
    EXPECT_EQ(system.PutV("overflow", overflow, 2), BIO_INVALID_PARAM);
    EXPECT_EQ(system.PutV("unrepresentable", unrepresentable, 2), BIO_INVALID_PARAM);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, failed_new_value_write_rolls_back_the_index_queue_and_reservation)
{
    Start();
    ASSERT_EQ(system.Put("existing", "old", 3), BIO_OK);
    unlinkAttempts = 0;
    MOCKER(unlink).stubs().will(invoke(ObserveTemporaryCleanup));
    MOCKER(pwritev).stubs().will(invoke(FailedVectorWrite));
    EXPECT_EQ(system.Put("key", "newvalue", 8), BIO_UFS_IOERR);
    EXPECT_EQ(unlinkAttempts, 1U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().usedBytes, FileBytes("existing", 3));
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
    EXPECT_EQ(CandidateKeys(), (std::vector<std::string>{"existing"}));
    char value[3]{};
    ASSERT_EQ(system.Get("existing", value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "old");
    std::string path;
    ASSERT_EQ(system.BuildFilePath("key", path), BIO_OK);
    DIR *dir = opendir(path.substr(0, path.find_last_of('/')).c_str());
    ASSERT_NE(dir, nullptr);
    while (auto *entry = readdir(dir)) {
        EXPECT_NE(std::string(entry->d_name).find(".tmp."), 0U);
    }
    closedir(dir);
}

TEST_F(TestLocalEviction, failed_footer_write_releases_reservation_and_removes_placeholder)
{
    Start();
    MOCKER(pwritev).stubs().will(invoke(FailedFooterWrite));
    EXPECT_EQ(system.Put("key", "value", 5), BIO_UFS_IOERR);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, put_validates_buffer_and_value_length)
{
    Start();
    MOCKER(pwritev).expects(never());
    char byte = 'x';
    EXPECT_EQ(system.Put("invalid", nullptr, 1), BIO_INVALID_PARAM);
    EXPECT_EQ(system.Put("invalid", &byte, LOCAL_FILE_MAX_VALUE_SIZE + 1), BIO_INVALID_PARAM);
    EXPECT_EQ(system.Put("invalid", &byte, IO_MAX_LEN + 1), BIO_INVALID_PARAM);
    EXPECT_EQ(system.Exist("invalid"), BIO_NOT_EXISTS);
    EXPECT_EQ(Usage().usedBytes, 0U);
    EXPECT_EQ(Usage().reservedBytes, 0U);
}

TEST_F(TestLocalEviction, serialized_file_preserves_full_key_and_value_offset)
{
    Start();
    const std::string key = "tenant/a:key\\with?symbols";
    const std::string value = "payload";
    ASSERT_EQ(system.Put(key.c_str(), value.data(), value.size()), BIO_OK);
    std::string path;
    ASSERT_EQ(system.BuildFilePath(key.c_str(), path), BIO_OK);
    std::ifstream file(path, std::ios::binary);
    std::string storedValue(value.size(), '\0');
    ASSERT_TRUE(file.read(&storedValue[0], storedValue.size()));
    EXPECT_EQ(storedValue, value);
    char metadata[LOCAL_FILE_MAX_METADATA_SIZE]{};
    const size_t metadataSize = key.size() + LOCAL_FILE_FOOTER_SIZE;
    ASSERT_TRUE(file.read(metadata, metadataSize));
    EXPECT_EQ(file.peek(), std::char_traits<char>::eof());
    EXPECT_EQ(std::string(metadata, key.size()), key);
    EXPECT_EQ(std::string(metadata + key.size(), 5), "UBSIO");
    std::string storedKey;
    uint64_t storedSize = 0;
    ASSERT_EQ(LocalFilePack::Deserialize(metadata, metadataSize, storedKey, storedSize), BIO_OK);
    EXPECT_EQ(storedKey, key);
    EXPECT_EQ(storedSize, value.size());
    EXPECT_TRUE(Contains(key));
    FileSystem::ObjStat status{};
    ASSERT_EQ(system.Stat(key.c_str(), status), BIO_OK);
    EXPECT_EQ(status.size, value.size());
}

TEST_F(TestLocalEviction, hash_failure_preserves_files_and_capacity_accounting)
{
    Start();
    Write("key", 'a', 1);
    const auto used = Usage().usedBytes;
    const auto originalHash = system.mSha256;
    system.mSha256 = nullptr;
    char value = 0;
    EXPECT_EQ(system.Get("key", &value, 1, 0), BIO_UFS_IOERR);
    EXPECT_EQ(system.Delete("key"), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().usedBytes, used);
    EXPECT_EQ(Usage().files, 1U);
    SetEvictionTarget(FileBytes("key") - 1);
    EXPECT_EQ(system.CheckAndEvict(), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().usedBytes, used);
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_TRUE(events.empty());
    system.mSha256 = originalHash;
    EXPECT_EQ(system.Get("key", &value, 1, 0), BIO_OK);
    EXPECT_EQ(value, 'a');
}

TEST_F(TestLocalEviction, traces_vector_writes_reads_and_recovery)
{
    TraceEnableGuard traceGuard;
    Start();
    auto *vectors = ock::htracer::HtracerManager::GetPointPtr(UFS_TRACE_PUT_WRITEV);
    auto *recover = ock::htracer::HtracerManager::GetPointPtr(UFS_TRACE_RECOVER);
    auto *read = ock::htracer::HtracerManager::GetPointPtr(UFS_TRACE_GET_READ);
    const auto vectorsBefore = vectors->GetGoodEnd();
    const auto recoverBefore = recover->GetGoodEnd();
    const auto readBefore = read->GetGoodEnd();
    ASSERT_EQ(system.Put("scalar", "value", 5), BIO_OK);
    ASSERT_EQ(system.Put("second", "abcd", 4), BIO_OK);
    EXPECT_EQ(vectors->GetGoodEnd(), vectorsBefore + 2);
    char output[6]{};
    EXPECT_EQ(system.Get("scalar", output, sizeof(output), 0), BIO_READ_EXCEED);
    EXPECT_EQ(read->GetGoodEnd(), readBefore + 1);
    EXPECT_EQ(recover->GetGoodEnd(), recoverBefore);
    system.Stop();
    Start();
    EXPECT_EQ(recover->GetGoodEnd(), recoverBefore + 1);
    EXPECT_EQ(read->GetGoodEnd(), readBefore + 1);
}

}
