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

ssize_t FailedRecoveryRead(int, void *, size_t, off_t) noexcept
{
    errno = EIO;
    return -1;
}

std::string QuarantinedPath(const std::string &path)
{
    const size_t split = path.rfind('/') + 1;
    return path.substr(0, split) + ".corrupt." + path.substr(split);
}

TEST_F(TestLocalEviction, key_scan_requires_initialization_and_returns_an_empty_snapshot)
{
    UfsLocalKeyInfo *items = nullptr;
    uint64_t count = 7;
    EXPECT_EQ(system.ScanKeys(nullptr, &count), BIO_INVALID_PARAM);
    EXPECT_EQ(system.ScanKeys(&items, nullptr), BIO_INVALID_PARAM);
    EXPECT_EQ(system.ScanKeys(&items, &count), BIO_NOT_READY);
    EXPECT_EQ(items, nullptr);
    EXPECT_EQ(count, 0U);
    Start();
    EXPECT_EQ(system.ScanKeys(&items, &count), BIO_OK);
    EXPECT_EQ(items, nullptr);
    EXPECT_EQ(count, 0U);
    system.Stop();
    EXPECT_EQ(system.ScanKeys(&items, &count), BIO_NOT_READY);
}

TEST_F(TestLocalEviction, key_scan_excludes_unpublished_and_detached_entries_without_promoting)
{
    Start();
    Write("first", 'a', 1);
    Write("second", 'b', 2);
    ASSERT_EQ(Reserve("pending", FileBytes("pending")), BIO_OK);
    const auto order = CandidateKeys();
    std::unordered_map<std::string, uint64_t> snapshot;
    ASSERT_EQ(SnapshotKeys(snapshot), BIO_OK);
    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot.at("first"), 4097U);
    EXPECT_EQ(snapshot.at("second"), 4097U);
    EXPECT_EQ(CandidateKeys(), order);
    system.ReleaseWriteSpace("pending", FileBytes("pending"));

    EvictionCandidate detached;
    ASSERT_EQ(system.mEvictionPolicy->Remove("first", detached), BIO_OK);
    ASSERT_EQ(SnapshotKeys(snapshot), BIO_OK);
    EXPECT_EQ(snapshot.count("first"), 0U);
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.at("second"), 4097U);
    ASSERT_EQ(system.mEvictionPolicy->ColdInsert(detached), BIO_OK);
}

TEST_F(TestLocalEviction, key_scan_preserves_the_maximum_value_length)
{
    Start();
    Write("key", 'a', 1);
    EvictionCandidate file;
    ASSERT_EQ(system.mEvictionPolicy->Remove("key", file), BIO_OK);
    file.record.sizeBytes = FileBytes("key", LOCAL_FILE_MAX_VALUE_SIZE);
    ASSERT_EQ(system.mEvictionPolicy->ColdInsert(file), BIO_OK);
    std::unordered_map<std::string, uint64_t> snapshot;
    ASSERT_EQ(SnapshotKeys(snapshot), BIO_OK);
    EXPECT_EQ(snapshot.at("key"), LOCAL_FILE_MAX_VALUE_SIZE);
}

TEST(LocalFileKeyScan, merges_local_keys_without_replacing_existing_disk_metadata)
{
    UfsHelper helper;
    helper.mConfig.underFsType = BIO_UNDERFS_LOCAL;
    helper.scanLocalKeysOp = [](UfsLocalKeyInfo **items, uint64_t *count) noexcept -> int32_t {
        *items = static_cast<UfsLocalKeyInfo *>(std::calloc(2, sizeof(UfsLocalKeyInfo)));
        if (*items == nullptr) {
            return BIO_ALLOC_FAIL;
        }
        std::memcpy((*items)[0].key, "disk", sizeof("disk"));
        (*items)[0].valueLen = 3;
        std::memcpy((*items)[1].key, "local", sizeof("local"));
        (*items)[1].valueLen = LOCAL_FILE_MAX_VALUE_SIZE;
        *count = 2;
        return BIO_OK;
    };
    std::unordered_map<std::string, uint64_t> items{{"disk", 9}};
    ASSERT_EQ(helper.ScanLocalKeys(items), BIO_OK);
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items.at("disk"), 9U);
    EXPECT_EQ(items.at("local"), LOCAL_FILE_MAX_VALUE_SIZE);
    helper.mConfig.underFsType = BIO_UNDERFS_SHARED;
    EXPECT_EQ(helper.ScanLocalKeys(items), BIO_INVALID_PARAM);
    EXPECT_EQ(items.size(), 2U);
}

TEST(LocalFileKeyScan, returns_all_keys_in_one_scan_call)
{
    UfsHelper helper;
    helper.mConfig.underFsType = BIO_UNDERFS_LOCAL;
    static uint32_t scanCalls = 0;
    scanCalls = 0;
    helper.scanLocalKeysOp = [](UfsLocalKeyInfo **items, uint64_t *count) noexcept -> int32_t {
        ++scanCalls;
        constexpr uint64_t keyCount = 2050;
        *items = static_cast<UfsLocalKeyInfo *>(std::calloc(keyCount, sizeof(UfsLocalKeyInfo)));
        if (*items == nullptr) {
            return BIO_ALLOC_FAIL;
        }
        for (uint64_t i = 0; i < keyCount; ++i) {
            const auto key = "key-" + std::to_string(i);
            std::memcpy((*items)[i].key, key.c_str(), key.size() + 1);
            (*items)[i].valueLen = i + 1;
        }
        *count = keyCount;
        return BIO_OK;
    };
    std::unordered_map<std::string, uint64_t> items;
    ASSERT_EQ(helper.ScanLocalKeys(items), BIO_OK);
    EXPECT_EQ(scanCalls, 1U);
    ASSERT_EQ(items.size(), 2050U);
    for (uint64_t i = 0; i < 2050; ++i) {
        EXPECT_EQ(items.at("key-" + std::to_string(i)), i + 1);
    }
}

TEST_F(TestLocalEviction, recovery_rebuilds_only_the_assigned_slot)
{
    config.localConfig.deviceCount = 2;
    Start();
    ASSERT_EQ(system.Put("slot-zero", "zero", 4), BIO_OK);
    system.Stop();

    config.localConfig.slotIndex = 1;
    Start();
    EXPECT_EQ(system.Exist("slot-zero"), BIO_NOT_EXISTS);
    EXPECT_EQ(Usage().files, 0U);
    ASSERT_EQ(system.Put("slot-one", "one", 3), BIO_OK);
    system.Stop();

    LocalSystem replacement;
    ASSERT_EQ(replacement.Init(), BIO_OK);
    EXPECT_EQ(replacement.Exist("slot-zero"), BIO_NOT_EXISTS);
    char value[4]{};
    ASSERT_EQ(replacement.Get("slot-one", value, 3, 0), BIO_OK);
    EXPECT_EQ(std::string(value, 3), "one");
    EXPECT_EQ(replacement.mUsedBytes, FileBytes("slot-one", 3));
    replacement.Stop();

    config.localConfig.slotIndex = 0;
    Start();
    EXPECT_EQ(system.Exist("slot-one"), BIO_NOT_EXISTS);
    ASSERT_EQ(system.Get("slot-zero", value, 4, 0), BIO_OK);
    EXPECT_EQ(std::string(value, 4), "zero");
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, ignores_symlinks_temporary_and_unrelated_files)
{
    Start();
    Write("key", 'a', 1);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("key", path), BIO_OK);
    ASSERT_EQ(unlink(path.c_str()), 0);
    std::string outside = root + "/unrelated";
    std::ofstream(outside) << "retain";
    ASSERT_EQ(symlink(outside.c_str(), path.c_str()), 0);
    std::string temporary = path.substr(0, path.find_last_of('/')) + "/.tmp.in-progress";
    std::ofstream(temporary) << "retain";
    std::vector<std::string> files;
    uint64_t used = 0;
    EXPECT_EQ(ScanKeys(files, used), BIO_OK);
    EXPECT_TRUE(files.empty());
    EXPECT_EQ(used, 0U);
    EXPECT_EQ(access(outside.c_str(), F_OK), 0);
    EXPECT_EQ(access(temporary.c_str(), F_OK), 0);
}

TEST_F(TestLocalEviction, recovery_cleans_only_owned_regular_temporary_files)
{
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    std::string hash;
    std::string leaf;
    ASSERT_EQ(system.BuildHash("key", hash), BIO_OK);
    leaf = system.BuildLeafPath(hash);
    const std::string orphan = leaf + "/.tmp." + hash + ".A1b2C3";
    const std::string outside = root + "/unrelated";
    const std::string link = leaf + "/.tmp." + hash + ".B1c2D3";
    const std::string directory = leaf + "/.tmp." + hash + ".C1d2E3";
    const std::string unrelated = leaf + "/.tmp.in-progress";
    std::string wrongHash = hash;
    wrongHash[0] = wrongHash[0] == '0' ? '1' : '0';
    const std::string wrongShard = leaf + "/.tmp." + wrongHash + ".D1e2F3";
    std::ofstream(orphan) << "partial value";
    std::ofstream(outside) << "retain";
    std::ofstream(unrelated) << "retain";
    std::ofstream(wrongShard) << "retain";
    ASSERT_EQ(symlink(outside.c_str(), link.c_str()), 0);
    ASSERT_EQ(mkdir(directory.c_str(), 0700), 0);
    std::vector<std::string> files;
    uint64_t used = 0;
    ASSERT_EQ(ScanKeys(files, used), BIO_OK);
    EXPECT_EQ(access(orphan.c_str(), F_OK), 0);
    system.Stop();
    Start();
    EXPECT_NE(access(orphan.c_str(), F_OK), 0);
    struct stat status{};
    ASSERT_EQ(lstat(link.c_str(), &status), 0);
    EXPECT_TRUE(S_ISLNK(status.st_mode));
    ASSERT_EQ(lstat(directory.c_str(), &status), 0);
    EXPECT_TRUE(S_ISDIR(status.st_mode));
    EXPECT_EQ(access(outside.c_str(), F_OK), 0);
    EXPECT_EQ(access(unrelated.c_str(), F_OK), 0);
    EXPECT_EQ(access(wrongShard.c_str(), F_OK), 0);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_EQ(system.Exist("key"), BIO_OK);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, recovery_waits_for_write_reservations_before_cleaning_temporary_files)
{
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    std::string hash;
    std::string leaf;
    ASSERT_EQ(system.BuildHash("key", hash), BIO_OK);
    leaf = system.BuildLeafPath(hash);
    const std::string orphan = leaf + "/.tmp." + hash + ".A1b2C3";
    std::ofstream(orphan) << "partial value";
    ASSERT_EQ(Reserve("pending", 100), BIO_OK);
    EXPECT_EQ(system.RecoverEvictionState(), BIO_INNER_RETRY);
    EXPECT_EQ(access(orphan.c_str(), F_OK), 0);
    system.ReleaseWriteSpace("pending", 100);
    ASSERT_EQ(system.RecoverEvictionState(), BIO_OK);
    EXPECT_NE(access(orphan.c_str(), F_OK), 0);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().reservedBytes, 0U);
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, temporary_cleanup_failure_preserves_the_published_recovery_state)
{
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    std::string hash;
    std::string leaf;
    ASSERT_EQ(system.BuildHash("key", hash), BIO_OK);
    leaf = system.BuildLeafPath(hash);
    const std::string orphan = leaf + "/.tmp." + hash + ".A1b2C3";
    std::ofstream(orphan) << "partial value";
    MOCKER(unlinkat).stubs().will(invoke(FailEvictionUnlink));
    EXPECT_EQ(system.RecoverEvictionState(), BIO_UFS_IOERR);
    EXPECT_EQ(access(orphan.c_str(), F_OK), 0);
    EXPECT_EQ(Usage().usedBytes, FileBytes("key", 5));
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_EQ(system.Exist("key"), BIO_OK);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, startup_recovers_usage_and_reclaims_existing_files)
{
    Start();
    Write("old", 'a', 1);
    Write("new", 'b', 2);
    std::vector<std::string> order;
    uint64_t used = 0;
    ASSERT_EQ(ScanKeys(order, used), BIO_OK);
    ASSERT_EQ(order.size(), 2U);
    system.Stop();
    MockFileSystemCapacity(2 * FileBytes("old"));
    Start();
    EXPECT_EQ(system.Exist(order[0].c_str()), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist(order[1].c_str()), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, FileBytes("old"));
    EXPECT_EQ(Usage().files, 1U);
}

TEST_F(TestLocalEviction, restart_uses_scan_order_without_file_times_or_read_history)
{
    Start();
    Write("first", 'a', 1);
    Write("second", 'b', 2);
    Write("third", 'c', 3);
    std::vector<std::string> order;
    uint64_t used = 0;
    ASSERT_EQ(ScanKeys(order, used), BIO_OK);
    ASSERT_EQ(order.size(), 3U);
    for (size_t i = 0; i < order.size(); ++i) {
        std::string path;
        ASSERT_EQ(system.BuildFilePath(order[i].c_str(), path), BIO_OK);
        const auto time = static_cast<time_t>(order.size() - i);
        struct timespec times[2]{{time, 0}, {time, 0}};
        ASSERT_EQ(utimensat(AT_FDCWD, path.c_str(), times, 0), 0);
    }
    char byte = 0;
    ASSERT_EQ(system.Get(order[0].c_str(), &byte, 1, 0), BIO_OK);
    system.Stop();
    Start();
    EXPECT_EQ(CandidateKeys(), order);
    ASSERT_EQ(system.Get(order[0].c_str(), &byte, 1, 0), BIO_OK);
    EXPECT_EQ(CandidateKeys(), (std::vector<std::string>{order[1], order[2], order[0]}));
    SetEvictionTarget(Usage().usedBytes - 1);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    EXPECT_EQ(system.Exist(order[1].c_str()), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist(order[0].c_str()), BIO_OK);
    EXPECT_EQ(system.Exist(order[2].c_str()), BIO_OK);
}

TEST_F(TestLocalEviction, failed_rebuild_keeps_published_index_accounting_and_queue)
{
    Start();
    Write("first", 'a', 1);
    Write("second", 'b', 2);
    const auto order = CandidateKeys();
    const auto used = Usage().usedBytes;
    MOCKER(pread).expects(once()).will(invoke(FailedRecoveryRead));
    EXPECT_EQ(system.RecoverEvictionState(), BIO_UFS_IOERR);
    EXPECT_EQ(Usage().usedBytes, used);
    EXPECT_EQ(CandidateKeys(), order);
}

TEST_F(TestLocalEviction, empty_value_recovers_without_exposing_metadata)
{
    Start();
    ASSERT_EQ(system.Put("empty", nullptr, 0), BIO_OK);
    EXPECT_EQ(Usage().usedBytes, FileBytes("empty", 0));
    system.Stop();
    Start();
    FileSystem::ObjStat status{};
    ASSERT_EQ(system.Stat("empty", status), BIO_OK);
    EXPECT_EQ(status.size, 0U);
    char output = '?';
    size_t realLen = 1;
    EXPECT_EQ(system.GetWithRealLen("empty", &output, 1, 0, realLen), BIO_READ_EXCEED);
    EXPECT_EQ(realLen, 0U);
    EXPECT_EQ(output, '?');
    EXPECT_EQ(system.Get("empty", &output, 0, 0), BIO_OK);
}

TEST_F(TestLocalEviction, recovered_key_index_serves_requests_with_traced_hashing)
{
    TraceEnableGuard traceGuard;
    Start();
    Write("key", 'a', 1);
    system.Stop();
    events.clear();
    Start();
    EXPECT_TRUE(events.empty());
    std::unordered_map<std::string, uint64_t> snapshot;
    ASSERT_EQ(SnapshotKeys(snapshot), BIO_OK);
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.at("key"), 4097U);
    auto *hash = ock::htracer::HtracerManager::GetPointPtr(UFS_TRACE_HASH_KEY);
    const auto hashBefore = hash->GetGoodEnd();
    char value = 0;
    EXPECT_EQ(system.Get("key", &value, 1, 0), BIO_OK);
    EXPECT_EQ(value, 'a');
    EXPECT_EQ(system.Exist("key"), BIO_OK);
    FileSystem::ObjStat status{};
    EXPECT_EQ(system.Stat("key", status), BIO_OK);
    EXPECT_EQ(status.size, 4097U);
    EXPECT_EQ(system.Get("missing", &value, 1, 0), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Delete("key"), BIO_OK);
    EXPECT_EQ(hash->GetGoodEnd(), hashBefore + 4);
}

TEST_F(TestLocalEviction, long_keys_preserve_identity_after_duplicate_put_recovery_and_eviction)
{
    Start();
    const std::string prefix(LOCAL_FILE_MAX_KEY_SIZE - 1, 'k');
    const std::string hot = prefix + "a";
    const std::string cold = prefix + "b";
    Write(hot.c_str(), 'a', 1);
    Write(cold.c_str(), 'b', 2);
    ASSERT_EQ(system.Put(hot.c_str(), "new", 3), BIO_OK);
    system.Stop();
    events.clear();
    Start();
    EXPECT_TRUE(events.empty());
    std::unordered_map<std::string, uint64_t> snapshot;
    ASSERT_EQ(SnapshotKeys(snapshot), BIO_OK);
    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot.at(hot), 4097U);
    EXPECT_EQ(snapshot.at(cold), 4097U);
    char value[3]{};
    ASSERT_EQ(system.Get(hot.c_str(), value, sizeof(value), 0), BIO_OK);
    EXPECT_EQ(std::string(value, sizeof(value)), "aaa");
    EXPECT_EQ(Usage().usedBytes, FileBytes(cold) + FileBytes(hot));
    SetEvictionTarget(Usage().usedBytes - 1);
    ASSERT_EQ(system.CheckAndEvict(), BIO_OK);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.back(), std::make_pair(UFS_LOCAL_META_DELETE, cold));
    EXPECT_EQ(system.Exist(cold.c_str()), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist(hot.c_str()), BIO_OK);
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_EQ(Usage().usedBytes, FileBytes(hot));
    EXPECT_EQ(CandidateKeys(), std::vector<std::string>{hot});
}

TEST_F(TestLocalEviction, startup_scan_excludes_evicted_files_and_reports_only_deletion)
{
    Start();
    Write("old", 'a', 1);
    Write("new", 'b', 2);
    EXPECT_TRUE(events.empty());
    std::vector<std::string> order;
    uint64_t used = 0;
    ASSERT_EQ(ScanKeys(order, used), BIO_OK);
    ASSERT_EQ(order.size(), 2U);
    system.Stop();
    MockFileSystemCapacity(2 * FileBytes("old"));
    Start();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].first, UFS_LOCAL_META_DELETE);
    EXPECT_EQ(events[0].second, order[0]);
    std::unordered_map<std::string, uint64_t> snapshot;
    ASSERT_EQ(SnapshotKeys(snapshot), BIO_OK);
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.at(order[1]), 4097U);
    EXPECT_EQ(system.Exist(order[1].c_str()), BIO_OK);
}

TEST_F(TestLocalEviction, recovery_keeps_good_keys_in_the_same_leaf_as_a_corrupt_file)
{
    Start();
    const char *good = "review-recovery-412";
    const char *bad = "review-recovery-1148";
    ASSERT_EQ(system.Put(good, "valid", 5), BIO_OK);
    ASSERT_EQ(system.Put(bad, "discard", 7), BIO_OK);
    std::string goodPath;
    std::string badPath;
    ASSERT_EQ(system.BuildFilePath(good, goodPath), BIO_OK);
    ASSERT_EQ(system.BuildFilePath(bad, badPath), BIO_OK);
    ASSERT_EQ(goodPath.substr(0, goodPath.rfind('/')), badPath.substr(0, badPath.rfind('/')));
    system.Stop();

    const std::string damaged = "value-with-invalid-footer";
    {
        std::ofstream file(badPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.write(damaged.data(), damaged.size()));
    }
    UnderFsConfig::Initialize(config);
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_EQ(Usage().files, 1U);
    EXPECT_EQ(Usage().usedBytes, FileBytes(good, 5));
    EXPECT_EQ(system.Exist(bad), BIO_NOT_EXISTS);
    char recovered[5]{};
    ASSERT_EQ(system.Get(good, recovered, sizeof(recovered), 0), BIO_OK);
    EXPECT_EQ(std::string(recovered, sizeof(recovered)), "valid");
    std::unordered_map<std::string, uint64_t> snapshot;
    ASSERT_EQ(SnapshotKeys(snapshot), BIO_OK);
    EXPECT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.count(good), 1U);
    EXPECT_EQ(access(badPath.c_str(), F_OK), -1);
    std::ifstream file(QuarantinedPath(badPath), std::ios::binary);
    std::string contents(damaged.size(), '\0');
    ASSERT_TRUE(file.read(&contents[0], contents.size()));
    EXPECT_EQ(contents, damaged);
    EXPECT_TRUE(events.empty());

    system.Stop();
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_EQ(system.Exist(good), BIO_OK);
    EXPECT_EQ(system.Exist(bad), BIO_NOT_EXISTS);
}

TEST_F(TestLocalEviction, recovery_quarantines_truncated_value_and_key_path_mismatch)
{
    Start();
    Write("key", 'a', 1);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("key", path), BIO_OK);
    system.Stop();
    ASSERT_EQ(truncate(path.c_str(), FileBytes("key") - 1), 0);
    UnderFsConfig::Initialize(config);
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
    EXPECT_EQ(Usage().usedBytes, 0U);
    struct stat first{};
    const std::string quarantined = QuarantinedPath(path);
    ASSERT_EQ(stat(quarantined.c_str(), &first), 0);
    EXPECT_EQ(static_cast<uint64_t>(first.st_size), FileBytes("key") - 1);
    system.Stop();

    char metadata[LOCAL_FILE_MAX_METADATA_SIZE]{};
    size_t metadataSize = 0;
    ASSERT_EQ(LocalFilePack::Serialize("different-key", 4097, metadata, metadataSize), BIO_OK);
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        const std::string value(4097, 'a');
        ASSERT_TRUE(file.write(value.data(), value.size()));
        ASSERT_TRUE(file.write(metadata, metadataSize));
    }
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_EQ(system.Exist("key"), BIO_NOT_EXISTS);
    EXPECT_EQ(system.Exist("different-key"), BIO_NOT_EXISTS);
    EXPECT_EQ(Usage().files, 0U);
    struct stat copy{};
    ASSERT_EQ(stat(quarantined.c_str(), &copy), 0);
    EXPECT_EQ(copy.st_ino, first.st_ino);
    EXPECT_EQ(access((quarantined + ".1").c_str(), F_OK), 0);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, recovery_metadata_io_error_fails_startup_and_leaves_file_in_place)
{
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("key", path), BIO_OK);
    system.Stop();
    MOCKER(pread).expects(once()).will(invoke(FailedRecoveryRead));
    UnderFsConfig::Initialize(config);
    EXPECT_EQ(system.Init(), BIO_UFS_IOERR);
    EXPECT_EQ(access(path.c_str(), F_OK), 0);
    EXPECT_EQ(access(QuarantinedPath(path).c_str(), F_OK), -1);
    EXPECT_TRUE(events.empty());
}

TEST_F(TestLocalEviction, recovery_rejects_header_based_files_without_modifying_them)
{
    Start();
    Write("key", 'a', 1);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("key", path), BIO_OK);
    system.Stop();
    events.clear();
    // Header-based formats have no valid footer and remain untouched on rejection.
    std::array<char, 512> header{};
    memcpy(header.data(), "UBSIOKV1", 8);
    header[8] = 2;
    header[12] = 3;
    header[17] = 2;
    memcpy(header.data() + 24, "key", 3);
    const std::string value(512, 'x');
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.write(header.data(), header.size()));
        ASSERT_TRUE(file.write(value.data(), value.size()));
    }
    UnderFsConfig::Initialize(config);
    EXPECT_EQ(system.Init(), BIO_UFS_IOERR);
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(Usage().files, 0U);
    struct stat status{};
    ASSERT_EQ(stat(path.c_str(), &status), 0);
    EXPECT_EQ(status.st_size, header.size() + value.size());
    std::ifstream file(path, std::ios::binary);
    file.seekg(header.size());
    std::string storedValue(value.size(), '\0');
    ASSERT_TRUE(file.read(&storedValue[0], storedValue.size()));
    EXPECT_EQ(storedValue, value);
}

TEST_F(TestLocalEviction, recovery_quarantines_value_length_inconsistent_with_file_size)
{
    Start();
    ASSERT_EQ(system.Put("key", "payload", 7), BIO_OK);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("key", path), BIO_OK);
    system.Stop();
    char metadata[LOCAL_FILE_MAX_METADATA_SIZE]{};
    size_t metadataSize = 0;
    ASSERT_EQ(LocalFilePack::Serialize("key", 8, metadata, metadataSize), BIO_OK);
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.write("payload", 7));
        ASSERT_TRUE(file.write(metadata, metadataSize));
    }
    UnderFsConfig::Initialize(config);
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_EQ(access(path.c_str(), F_OK), -1);
    EXPECT_EQ(access(QuarantinedPath(path).c_str(), F_OK), 0);
}

TEST_F(TestLocalEviction, recovery_quarantines_unsupported_footer_without_altering_bytes)
{
    Start();
    ASSERT_EQ(system.Put("key", "value", 5), BIO_OK);
    std::string path;
    ASSERT_EQ(system.BuildFilePath("key", path), BIO_OK);
    system.Stop();
    char unsupportedFooter[13]{};
    std::memcpy(unsupportedFooter, "UBSIO", 5);
    unsupportedFooter[5] = 3;
    unsupportedFooter[9] = 5;
    std::string stored = "valuekey";
    stored.append(unsupportedFooter, sizeof(unsupportedFooter));
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.write(stored.data(), stored.size()));
    }
    UnderFsConfig::Initialize(config);
    ASSERT_EQ(system.Init(), BIO_OK);
    EXPECT_EQ(Usage().files, 0U);
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(access(path.c_str(), F_OK), -1);
    std::ifstream file(QuarantinedPath(path), std::ios::binary);
    std::string contents(stored.size(), '\0');
    ASSERT_TRUE(file.read(&contents[0], contents.size()));
    EXPECT_EQ(contents, stored);
}

TEST(LocalFilePack, validates_key_bounds_magic_and_footer_lengths)
{
    char metadata[LOCAL_FILE_MAX_METADATA_SIZE]{};
    size_t metadataSize = 0;
    const std::string key(LOCAL_FILE_MAX_KEY_SIZE, 'k');
    ASSERT_EQ(LocalFilePack::Serialize(key.c_str(), LOCAL_FILE_MAX_VALUE_SIZE, metadata, metadataSize), BIO_OK);
    EXPECT_EQ(metadataSize, 266U);
    EXPECT_EQ(static_cast<unsigned char>(metadata[key.size() + 5]), 255U);
    EXPECT_EQ(metadata[key.size() + 6], 0);
    for (size_t i = key.size() + 7; i < metadataSize; ++i) {
        EXPECT_EQ(static_cast<unsigned char>(metadata[i]), 255U);
    }
    std::string decoded;
    uint64_t size = 0;
    ASSERT_EQ(LocalFilePack::Deserialize(metadata, metadataSize, decoded, size), BIO_OK);
    EXPECT_EQ(decoded, key);
    EXPECT_EQ(size, LOCAL_FILE_MAX_VALUE_SIZE);
    EXPECT_EQ(LocalFilePack::Serialize((key + "x").c_str(), 1, metadata, metadataSize), BIO_INVALID_PARAM);
    EXPECT_EQ(LocalFilePack::Serialize("", 1, metadata, metadataSize), BIO_INVALID_PARAM);
    EXPECT_EQ(LocalFilePack::Serialize(nullptr, 1, metadata, metadataSize), BIO_INVALID_PARAM);
    EXPECT_EQ(LocalFilePack::Serialize("key", LOCAL_FILE_MAX_VALUE_SIZE + 1, metadata, metadataSize), BIO_INVALID_PARAM);
    EXPECT_EQ(metadataSize, 0U);
    EXPECT_EQ(LocalFilePack::Serialize("key", 1, nullptr, metadataSize), BIO_INVALID_PARAM);
    EXPECT_EQ(LocalFilePack::Deserialize(nullptr, LOCAL_FILE_FOOTER_SIZE, decoded, size), BIO_UFS_IOERR);
    EXPECT_EQ(LocalFilePack::Deserialize(metadata, LOCAL_FILE_FOOTER_SIZE - 1, decoded, size), BIO_UFS_IOERR);
    EXPECT_EQ(LocalFilePack::Deserialize(metadata, sizeof(metadata) + 1, decoded, size), BIO_UFS_IOERR);

    ASSERT_EQ(LocalFilePack::Serialize("key", 1, metadata, metadataSize), BIO_OK);
    EXPECT_EQ(metadataSize, 14U);
    EXPECT_EQ(std::string(metadata, 8), "keyUBSIO");
    EXPECT_EQ(static_cast<unsigned char>(metadata[8]), 3U);
    EXPECT_EQ(metadata[9], 0);
    EXPECT_EQ(static_cast<unsigned char>(metadata[10]), 1U);
    for (size_t i : {11U, 12U, 13U}) {
        EXPECT_EQ(metadata[i], 0);
    }
    metadata[3] = 'x';
    EXPECT_EQ(LocalFilePack::Deserialize(metadata, metadataSize, decoded, size), BIO_UFS_IOERR);
    ASSERT_EQ(LocalFilePack::Serialize("key", 1, metadata, metadataSize), BIO_OK);
    metadata[8] = 0;
    EXPECT_EQ(LocalFilePack::Deserialize(metadata, metadataSize, decoded, size), BIO_UFS_IOERR);
    metadata[8] = 4;
    EXPECT_EQ(LocalFilePack::Deserialize(metadata, metadataSize, decoded, size), BIO_UFS_IOERR);
    ASSERT_EQ(LocalFilePack::Serialize("key", 1, metadata, metadataSize), BIO_OK);
    metadata[9] = 1;
    EXPECT_EQ(LocalFilePack::Deserialize(metadata, metadataSize, decoded, size), BIO_UFS_IOERR);
    ASSERT_EQ(LocalFilePack::Serialize("key", 1, metadata, metadataSize), BIO_OK);
    metadata[1] = '\0';
    EXPECT_EQ(LocalFilePack::Deserialize(metadata, metadataSize, decoded, size), BIO_UFS_IOERR);
    ASSERT_EQ(LocalFilePack::Serialize("key", 0x01020304, metadata, metadataSize), BIO_OK);
    EXPECT_EQ(static_cast<unsigned char>(metadata[10]), 4U);
    EXPECT_EQ(static_cast<unsigned char>(metadata[11]), 3U);
    EXPECT_EQ(static_cast<unsigned char>(metadata[12]), 2U);
    EXPECT_EQ(static_cast<unsigned char>(metadata[13]), 1U);
    ASSERT_EQ(LocalFilePack::Deserialize(metadata, metadataSize, decoded, size), BIO_OK);
    EXPECT_EQ(size, 0x01020304U);
}

TEST(LocalFileMetadataEvents, buffers_startup_events_until_callback_is_registered)
{
    UfsHelper helper;
    std::vector<std::string> keys;
    UfsHelper::ReceiveLocalMetaEvent(&helper, UFS_LOCAL_META_DELETE, "early");
    UfsHelper::ReceiveLocalMetaEvent(&helper, UFS_LOCAL_META_DELETE, "evicted");
    EXPECT_TRUE(keys.empty());
    ASSERT_EQ(helper.mPendingLocalMetaEvents.size(), 2U);
    helper.RegLocalMetaEventCallback([&](const std::vector<UbsIoMetaEvent> &batch) noexcept {
        for (const auto &event : batch) {
            EXPECT_EQ(event.type, UBSIO_META_DELETE);
            keys.push_back(event.key);
        }
    });
    UfsHelper::ReceiveLocalMetaEvent(&helper, UFS_LOCAL_META_DELETE, "runtime");
    EXPECT_EQ(keys, (std::vector<std::string>{"early", "evicted", "runtime"}));
    EXPECT_TRUE(helper.mPendingLocalMetaEvents.empty());
    EXPECT_EQ(helper.mPendingLocalMetaEvents.capacity(), 0U);
}

TEST(LocalFileMetadataEvents, callback_registration_replaces_clears_and_replays_pending_events)
{
    UfsHelper helper;
    std::vector<std::string> first;
    std::vector<std::string> second;
    helper.RegLocalMetaEventCallback([&](const std::vector<UbsIoMetaEvent> &events) noexcept {
        for (const auto &event : events) {
            first.push_back(event.key);
        }
    });
    UfsHelper::ReceiveLocalMetaEvent(&helper, UFS_LOCAL_META_DELETE, "first");
    UbsIoMetaEventCallback replacement = [&](const std::vector<UbsIoMetaEvent> &events) noexcept {
        for (const auto &event : events) {
            second.push_back(event.key);
        }
    };
    helper.RegLocalMetaEventCallback(replacement);
    UfsHelper::ReceiveLocalMetaEvent(&helper, UFS_LOCAL_META_DELETE, "second");
    helper.RegLocalMetaEventCallback(nullptr);
    EXPECT_EQ(helper.mLocalMetaEventCallback, nullptr);
    UfsHelper::ReceiveLocalMetaEvent(&helper, UFS_LOCAL_META_DELETE, "pending");
    EXPECT_EQ(first, (std::vector<std::string>{"first"}));
    EXPECT_EQ(second, (std::vector<std::string>{"second"}));
    ASSERT_EQ(helper.mPendingLocalMetaEvents.size(), 1U);
    helper.RegLocalMetaEventCallback(replacement);
    EXPECT_EQ(first, (std::vector<std::string>{"first"}));
    EXPECT_EQ(second, (std::vector<std::string>{"second", "pending"}));
    EXPECT_TRUE(helper.mPendingLocalMetaEvents.empty());
    EXPECT_EQ(helper.mPendingLocalMetaEvents.capacity(), 0U);
}

TEST(LocalFileMetadataEvents, startup_deletions_are_reported_once_in_key_order)
{
    UfsHelper helper;
    constexpr uint32_t keyCount = 2050;
    for (uint32_t i = 0; i < keyCount; ++i) {
        const auto key = std::to_string(i);
        UfsHelper::ReceiveLocalMetaEvent(&helper, UFS_LOCAL_META_DELETE, key.c_str());
    }
    std::vector<std::string> keys;
    std::vector<size_t> batchSizes;
    helper.RegLocalMetaEventCallback([&](const std::vector<UbsIoMetaEvent> &events) noexcept {
        batchSizes.push_back(events.size());
        for (const auto &event : events) {
            EXPECT_EQ(event.type, UBSIO_META_DELETE);
            keys.push_back(event.key);
        }
    });
    EXPECT_EQ(batchSizes, (std::vector<size_t>{keyCount}));
    ASSERT_EQ(keys.size(), keyCount);
    for (uint32_t i = 0; i < keyCount; ++i) {
        EXPECT_EQ(keys[i], std::to_string(i));
    }
    EXPECT_TRUE(helper.mPendingLocalMetaEvents.empty());
    EXPECT_EQ(helper.mPendingLocalMetaEvents.capacity(), 0U);
}

}
