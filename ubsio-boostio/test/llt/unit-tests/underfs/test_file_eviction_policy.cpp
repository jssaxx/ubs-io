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

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <tuple>
#include "file_eviction_policy.h"

using namespace ock::bio;

namespace {
class TestFileEvictionPolicy : public testing::Test {
protected:
    void SetUp() override
    {
        policy = CreateFileEvictionPolicy(FileEvictionPolicyType::LRU);
        ASSERT_NE(policy, nullptr);
    }

    void Write(const std::string &key, uint64_t bytes = 512)
    {
        FileRecord file{"aa/bbb", bytes};
        const BResult ret = policy->Insert(key);
        if (ret == BIO_ALREADY_EXISTS) {
            return;
        }
        ASSERT_EQ(ret, BIO_OK);
        ASSERT_EQ(policy->CommitWrite(key, file, []() { return BIO_OK; }), BIO_OK);
    }

    std::vector<std::string> Keys(size_t limit = 1024)
    {
        std::vector<std::string> result;
        const auto candidates = Take(limit);
        for (const auto &file : candidates) {
            result.push_back(file.key);
        }
        for (auto file = candidates.rbegin(); file != candidates.rend(); ++file) {
            EXPECT_EQ(policy->ColdInsert(*file), BIO_OK);
        }
        return result;
    }

    std::vector<EvictionCandidate> Take(size_t limit)
    {
        std::vector<EvictionCandidate> candidates;
        EXPECT_EQ(policy->TakeCandidates(limit,
            [](const FileRecord &) noexcept { return true; }, candidates), BIO_OK);
        return candidates;
    }

    BResult Lookup(const std::string &key, FileRecord &file)
    {
        FileReference reference;
        return policy->Lookup(key, file, reference);
    }

    std::unique_ptr<FileEvictionPolicy> policy;
};
}

TEST_F(TestFileEvictionPolicy, LruPromotesLookupHitsAndDuplicateInserts)
{
    Write("a");
    Write("b");
    Write("c");
    FileRecord file;
    ASSERT_EQ(Lookup("a", file), BIO_OK);
    EXPECT_EQ(Lookup("missing", file), BIO_NOT_EXISTS);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"b", "c", "a"}));
    EXPECT_EQ(Keys(2), (std::vector<std::string>{"b", "c"}));
    Write("b", 1024);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"c", "a", "b"}));
    EXPECT_EQ(policy->Size(), 3U);
}

TEST_F(TestFileEvictionPolicy, UnpublishedPlaceholdersAreInvisibleAndRollbackRemovesThem)
{
    FileRecord output;
    ASSERT_EQ(policy->Insert("new"), BIO_OK);
    EXPECT_EQ(Lookup("new", output), BIO_NOT_EXISTS);
    EXPECT_TRUE(Keys().empty());
    EXPECT_EQ(policy->Size(), 1U);
    EXPECT_EQ(policy->Insert("new"), BIO_ALREADY_EXISTS);
    EXPECT_TRUE(Take(1).empty());
    policy->RollbackWrite("new");
    EXPECT_EQ(Lookup("new", output), BIO_NOT_EXISTS);
    EXPECT_EQ(policy->Size(), 0U);
    Write("after-rollback");
    EXPECT_EQ(Keys(), (std::vector<std::string>{"after-rollback"}));
}

TEST_F(TestFileEvictionPolicy, DuplicateInsertPreservesPublishedMetadataAndPromotesTheKey)
{
    Write("old");
    Write("new");
    EXPECT_EQ(policy->Insert("old"), BIO_ALREADY_EXISTS);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"new", "old"}));
    FileRecord output;
    ASSERT_EQ(Lookup("old", output), BIO_OK);
    EXPECT_EQ(output.leaf, "aa/bbb");
    EXPECT_EQ(output.sizeBytes, 512U);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"new", "old"}));
}

TEST_F(TestFileEvictionPolicy, SizeTracksIndexEntriesAcrossWriteDetachRestoreAndRecovery)
{
    EXPECT_EQ(policy->Size(), 0U);
    Write("first");
    Write("second");
    EXPECT_EQ(policy->Size(), 2U);
    ASSERT_EQ(policy->Insert("pending"), BIO_OK);
    EXPECT_EQ(policy->Size(), 3U);
    policy->RollbackWrite("pending");
    EXPECT_EQ(policy->Size(), 2U);
    ASSERT_EQ(policy->Insert("first"), BIO_ALREADY_EXISTS);
    EXPECT_EQ(policy->Size(), 2U);
    FileRecord output;
    ASSERT_EQ(Lookup("second", output), BIO_OK);
    EXPECT_EQ(policy->Size(), 2U);
    const auto detached = Take(1);
    ASSERT_EQ(detached.size(), 1U);
    EXPECT_EQ(policy->Size(), 1U);
    ASSERT_EQ(policy->ColdInsert(detached[0]), BIO_OK);
    EXPECT_EQ(policy->Size(), 2U);
    EvictionCandidate removed;
    ASSERT_EQ(policy->Remove("first", removed), BIO_OK);
    EXPECT_EQ(policy->Size(), 1U);
    FileIndex recovered;
    recovered.emplace(std::piecewise_construct, std::forward_as_tuple("recovered"),
        std::forward_as_tuple(FileRecord{"aa/bbb", 2048}));
    ASSERT_EQ(policy->Recover(recovered), BIO_OK);
    EXPECT_EQ(policy->Size(), 1U);
    FileIndex empty;
    ASSERT_EQ(policy->Recover(empty), BIO_OK);
    EXPECT_EQ(policy->Size(), 0U);
}

TEST_F(TestFileEvictionPolicy, TakeCandidatesRemovesTheBatchFromEveryIndexView)
{
    Write("first");
    Write("second");
    Write("remaining");
    const auto batch = Take(2);
    ASSERT_EQ(batch.size(), 2U);
    EXPECT_EQ(batch[0].key, "first");
    EXPECT_EQ(batch[1].key, "second");
    FileRecord output;
    EXPECT_EQ(Lookup("first", output), BIO_NOT_EXISTS);
    EXPECT_EQ(Lookup("second", output), BIO_NOT_EXISTS);
    EvictionCandidate missing;
    EXPECT_EQ(policy->Remove("first", missing), BIO_NOT_EXISTS);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"remaining"}));
    std::vector<std::string> indexed;
    ASSERT_EQ(policy->ForEachFile([&](const std::string &key, const FileRecord &file) {
        indexed.push_back(key);
        EXPECT_EQ(file.sizeBytes, 512U);
        return BIO_OK;
    }), BIO_OK);
    EXPECT_EQ(indexed, (std::vector<std::string>{"remaining"}));
    EXPECT_EQ(policy->Size(), 1U);
}

TEST_F(TestFileEvictionPolicy, CallerVisitorControlsWhereSelectionStops)
{
    Write("first", 512);
    Write("second", 1024);
    Write("third", 1536);
    std::vector<uint64_t> visited;
    std::vector<EvictionCandidate> batch;
    ASSERT_EQ(policy->TakeCandidates(3, [&](const FileRecord &file) noexcept {
        visited.push_back(file.sizeBytes);
        return visited.size() < 2;
    }, batch), BIO_OK);
    ASSERT_EQ(batch.size(), 2U);
    EXPECT_EQ(visited, (std::vector<uint64_t>{512, 1024}));
    EXPECT_EQ(Keys(), (std::vector<std::string>{"third"}));
}

TEST_F(TestFileEvictionPolicy, ColdInsertRestoresDetachedRecordAtTheColdEnd)
{
    Write("cold");
    Write("hot");
    const auto batch = Take(1);
    ASSERT_EQ(batch.size(), 1U);
    EXPECT_EQ(policy->ColdInsert(batch[0]), BIO_OK);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"cold", "hot"}));
    FileRecord file;
    ASSERT_EQ(Lookup("cold", file), BIO_OK);
    EXPECT_EQ(file.sizeBytes, 512U);
    EvictionCandidate victim;
    ASSERT_EQ(policy->Remove("cold", victim), BIO_OK);
    EXPECT_EQ(Lookup("cold", file), BIO_NOT_EXISTS);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"hot"}));
}

TEST_F(TestFileEvictionPolicy, ReferencesSkipActiveKeysUntilEveryReaderHasReleased)
{
    Write("busy");
    Write("idle");
    FileRecord file;
    FileReference first;
    FileReference second;
    ASSERT_EQ(policy->Lookup("busy", file, first), BIO_OK);
    ASSERT_EQ(policy->Lookup("busy", file, second), BIO_OK);
    EXPECT_EQ(policy->Lookup("idle", file, first), BIO_INVALID_PARAM);
    EXPECT_EQ(policy->Insert("busy"), BIO_ALREADY_EXISTS);
    EvictionCandidate victim;
    EXPECT_EQ(policy->Remove("busy", victim), BIO_INNER_RETRY);
    FileIndex recovered;
    EXPECT_EQ(policy->Recover(recovered), BIO_INNER_RETRY);
    const auto batch = Take(2);
    ASSERT_EQ(batch.size(), 1U);
    EXPECT_EQ(batch[0].key, "idle");
    first.Reset();
    EXPECT_TRUE(Take(1).empty());
    second.Reset();
    const auto released = Take(1);
    ASSERT_EQ(released.size(), 1U);
    EXPECT_EQ(released[0].key, "busy");
    EXPECT_EQ(policy->Size(), 0U);
}

TEST_F(TestFileEvictionPolicy, InsertQueuesTheKeyBeforePublicationWithoutPublishingItsValue)
{
    FileRecord incoming{"aa/bbb", 1024};
    ASSERT_EQ(policy->Insert("key"), BIO_OK);
    EXPECT_TRUE(Take(1).empty());
    Write("later");
    FileRecord output;
    FileReference reader;
    EXPECT_EQ(policy->Lookup("key", output, reader), BIO_NOT_EXISTS);
    EXPECT_FALSE(reader.IsValid());
    bool published = false;
    ASSERT_EQ(policy->CommitWrite("key", incoming, [&]() noexcept {
        published = true;
        return BIO_OK;
    }), BIO_OK);
    EXPECT_TRUE(published);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"key", "later"}));
    EXPECT_EQ(policy->Size(), 2U);
    ASSERT_EQ(Lookup("key", output), BIO_OK);
    EXPECT_EQ(output.sizeBytes, 1024U);
}

TEST_F(TestFileEvictionPolicy, DuplicatePendingInsertPromotesWithoutAddingAWriteReference)
{
    EXPECT_EQ(policy->Insert(""), BIO_INVALID_PARAM);
    ASSERT_EQ(policy->Insert("pending"), BIO_OK);
    Write("later");
    ASSERT_EQ(policy->Insert("pending"), BIO_ALREADY_EXISTS);
    FileRecord incoming{"aa/bbb", 1024};
    ASSERT_EQ(policy->CommitWrite("pending", incoming, []() noexcept { return BIO_OK; }), BIO_OK);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"later", "pending"}));
    const auto batch = Take(2);
    ASSERT_EQ(batch.size(), 2U);
    EXPECT_EQ(batch[1].key, "pending");
    EXPECT_EQ(batch[1].record.sizeBytes, 1024U);
    EXPECT_EQ(policy->Size(), 0U);
}

TEST_F(TestFileEvictionPolicy, RecoverLinksTheRecoveredOrderAndKeepsSnapshotsIndependent)
{
    Write("old");
    const auto snapshot = Take(1);
    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_EQ(policy->ColdInsert(snapshot[0]), BIO_OK);
    FileIndex recovered;
    for (const char *key : {"first", "second", "third"}) {
        recovered.emplace(std::piecewise_construct, std::forward_as_tuple(key),
            std::forward_as_tuple(FileRecord{"aa/bbb", 2048}));
    }
    std::vector<std::string> order;
    for (const auto &entry : recovered) {
        order.push_back(entry.first);
    }
    ASSERT_EQ(policy->Recover(recovered), BIO_OK);
    EXPECT_EQ(Keys(), order);
    EXPECT_EQ(snapshot[0].key, "old");
    ASSERT_EQ(policy->Insert("pending"), BIO_OK);
    EXPECT_EQ(policy->Recover(recovered), BIO_INNER_RETRY);
    EXPECT_EQ(policy->Size(), 4U);
    policy->RollbackWrite("pending");
    EXPECT_EQ(Keys(), order);
}

TEST_F(TestFileEvictionPolicy, ConcurrentMetadataAndQueueOperationsRemainConsistent)
{
    std::atomic<unsigned> failures{0};
    std::vector<std::thread> threads;
    for (unsigned rank = 0; rank < 8; ++rank) {
        threads.emplace_back([&, rank]() {
            for (unsigned i = 0; i < 64; ++i) {
                const auto key = std::to_string(rank) + "/" + std::to_string(i);
                FileRecord file{"aa/bbb", 512};
                if (policy->Insert(key) != BIO_OK ||
                    policy->CommitWrite(key, file, []() { return BIO_OK; }) != BIO_OK) {
                    ++failures;
                    continue;
                }
                FileRecord output;
                if (Lookup(key, output) != BIO_OK) {
                    ++failures;
                }
                EvictionCandidate victim;
                if (policy->Remove(key, victim) != BIO_OK) {
                    ++failures;
                }
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    EXPECT_EQ(failures.load(), 0U);
    EXPECT_EQ(policy->Size(), 0U);
    EXPECT_TRUE(Keys().empty());
    EXPECT_EQ(CreateFileEvictionPolicy(FileEvictionPolicyType::INVALID), nullptr);
    EXPECT_EQ(CreateFileEvictionPolicy(static_cast<FileEvictionPolicyType>(1)), nullptr);
}

TEST_F(TestFileEvictionPolicy, PublicationErrorsRollbackTheInsertedEntryAndKeepPublishedMetadata)
{
    Write("key");
    FileRecord incoming{"cc/ddd", 1024};
    ASSERT_EQ(policy->Insert("new"), BIO_OK);
    EXPECT_EQ(policy->CommitWrite("new", incoming, []() noexcept -> BResult {
        return BIO_UFS_IOERR;
    }), BIO_UFS_IOERR);
    policy->RollbackWrite("new");
    FileRecord output;
    EXPECT_EQ(Lookup("new", output), BIO_NOT_EXISTS);
    EXPECT_EQ(policy->Size(), 1U);
    ASSERT_EQ(Lookup("key", output), BIO_OK);
    EXPECT_EQ(output.leaf, "aa/bbb");
    EXPECT_EQ(output.sizeBytes, 512U);

    EXPECT_EQ(policy->ForEachFile([](const std::string &, const FileRecord &) -> BResult {
        return BIO_UFS_IOERR;
    }), BIO_UFS_IOERR);
    EXPECT_EQ(Keys(), (std::vector<std::string>{"key"}));
}
