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
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "art_index/art_range.h"
#include "art_index/lsm_art_tree.h"

using namespace ock::mms;

namespace {
int CollectArtKey(void *data, const unsigned char *key, uint32_t keyLen, void *value)
{
    auto *keys = static_cast<std::vector<std::string> *>(data);
    keys->emplace_back(reinterpret_cast<const char *>(key), keyLen);
    return value == nullptr ? 0 : 0;
}

int StopAfterFirstArtKey(void *data, const unsigned char *key, uint32_t keyLen, void *value)
{
    (void)value;
    auto *keys = static_cast<std::vector<std::string> *>(data);
    keys->emplace_back(reinterpret_cast<const char *>(key), keyLen);
    return 1;
}

void InsertByteKeys(art_tree *tree, int count)
{
    for (int i = 1; i <= count; ++i) {
        unsigned char key[2] = {static_cast<unsigned char>(i), static_cast<unsigned char>('a' + (i % 26))};
        auto value = reinterpret_cast<void *>(static_cast<uintptr_t>(i + 1));
        ASSERT_EQ(art_insert(tree, key, sizeof(key), value), nullptr);
    }
}

void InsertStringKey(art_tree *tree, const std::string &key, uintptr_t value)
{
    ASSERT_EQ(art_insert(tree, reinterpret_cast<const unsigned char *>(key.data()), key.size(),
        reinterpret_cast<void *>(value)), nullptr);
}

art_range_bound MakeStringBound(const std::string &key)
{
    return {reinterpret_cast<const unsigned char *>(key.data()), static_cast<int>(key.size())};
}

void ExpectRangeCount(int keyCount, int begin, int end, size_t expected)
{
    art_tree tree;
    ASSERT_EQ(art_tree_init(&tree), 0);
    InsertByteKeys(&tree, keyCount);

    unsigned char startKey[2] = {static_cast<unsigned char>(begin), 0};
    unsigned char endKey[2] = {static_cast<unsigned char>(end), 255};
    art_range_bound start = {startKey, sizeof(startKey)};
    art_range_bound finish = {endKey, sizeof(endKey)};
    std::vector<std::string> keys;

    EXPECT_EQ(art_search_range_external(&tree, &start, &finish, CollectArtKey, &keys), 0);
    EXPECT_EQ(keys.size(), expected);

    keys.clear();
    EXPECT_EQ(art_search_range_external(&tree, &start, &finish, StopAfterFirstArtKey, &keys), 1);
    EXPECT_EQ(keys.size(), 1U);

    EXPECT_EQ(art_tree_destroy(&tree), 0);
}

void ExpectFullSubtreeIteration(int childCount)
{
    art_tree tree;
    ASSERT_EQ(art_tree_init(&tree), 0);

    unsigned char lowerSibling[2] = {0, 0};
    unsigned char upperSibling[2] = {250, 0};
    ASSERT_EQ(art_insert(&tree, lowerSibling, sizeof(lowerSibling), reinterpret_cast<void *>(1)), nullptr);
    ASSERT_EQ(art_insert(&tree, upperSibling, sizeof(upperSibling), reinterpret_cast<void *>(2)), nullptr);

    for (int i = 1; i <= childCount; ++i) {
        unsigned char key[2] = {10, static_cast<unsigned char>(i)};
        ASSERT_EQ(art_insert(&tree, key, sizeof(key), reinterpret_cast<void *>(static_cast<uintptr_t>(i + 2))),
            nullptr);
    }

    unsigned char startKey[2] = {1, 0};
    unsigned char endKey[2] = {249, 255};
    art_range_bound start = {startKey, sizeof(startKey)};
    art_range_bound finish = {endKey, sizeof(endKey)};
    std::vector<std::string> keys;

    EXPECT_EQ(art_search_range_external(&tree, &start, &finish, CollectArtKey, &keys), 0);
    EXPECT_EQ(keys.size(), static_cast<size_t>(childCount));

    keys.clear();
    EXPECT_EQ(art_search_range_external(&tree, &start, &finish, StopAfterFirstArtKey, &keys), 1);
    EXPECT_EQ(keys.size(), 1U);

    EXPECT_EQ(art_tree_destroy(&tree), 0);
}
}  // namespace

TEST(TestArtIndex, test_art_range_node_variants)
{
    ExpectRangeCount(4, 2, 3, 2);
    ExpectRangeCount(16, 4, 10, 7);
    ExpectRangeCount(32, 8, 24, 17);
    ExpectRangeCount(80, 40, 70, 31);
}

TEST(TestArtIndex, test_art_range_full_subtree_iteration_variants)
{
    ExpectFullSubtreeIteration(4);
    ExpectFullSubtreeIteration(16);
    ExpectFullSubtreeIteration(40);
    ExpectFullSubtreeIteration(80);
}

TEST(TestArtIndex, test_art_range_invalid_args)
{
    art_tree tree;
    ASSERT_EQ(art_tree_init(&tree), 0);

    unsigned char key[1] = {1};
    art_range_bound bound = {key, sizeof(key)};
    std::vector<std::string> keys;

    EXPECT_EQ(art_search_range_external(nullptr, &bound, &bound, CollectArtKey, &keys), 0);
    EXPECT_EQ(art_search_range_external(&tree, &bound, &bound, nullptr, &keys), 0);
    EXPECT_EQ(art_search_range_external(&tree, &bound, &bound, CollectArtKey, nullptr), 0);
    EXPECT_EQ(art_search_range_external(&tree, &bound, &bound, CollectArtKey, &keys), 0);

    EXPECT_EQ(art_tree_destroy(&tree), 0);
}

TEST(TestArtIndex, test_art_range_long_prefix_bounds)
{
    art_tree tree;
    ASSERT_EQ(art_tree_init(&tree), 0);

    const std::string prefix = "very-long-common-prefix-for-art-range-";
    std::vector<std::string> values = {prefix + "000", prefix + "010", prefix + "020", prefix + "zzz"};
    for (size_t i = 0; i < values.size(); ++i) {
        InsertStringKey(&tree, values[i], static_cast<uintptr_t>(i + 1));
    }

    std::vector<std::string> keys;
    std::string startText = prefix + "000";
    std::string endText = prefix + "zzz";
    art_range_bound start = MakeStringBound(startText);
    art_range_bound finish = MakeStringBound(endText);
    EXPECT_EQ(art_search_range_external(&tree, &start, &finish, CollectArtKey, &keys), 0);
    EXPECT_EQ(keys.size(), values.size());

    keys.clear();
    std::string afterText = prefix + "zzzz";
    art_range_bound after = MakeStringBound(afterText);
    EXPECT_EQ(art_search_range_external(&tree, &after, &after, CollectArtKey, &keys), 0);
    EXPECT_TRUE(keys.empty());

    keys.clear();
    std::string beforeText = "before-range";
    art_range_bound before = MakeStringBound(beforeText);
    EXPECT_EQ(art_search_range_external(&tree, &before, &before, CollectArtKey, &keys), 0);
    EXPECT_TRUE(keys.empty());

    EXPECT_EQ(art_tree_destroy(&tree), 0);
}

TEST(TestArtIndex, test_art_range_leaf_length_comparison)
{
    art_tree tree;
    ASSERT_EQ(art_tree_init(&tree), 0);
    InsertStringKey(&tree, "abc", 1);

    std::vector<std::string> keys;
    std::string exactText = "abc";
    art_range_bound exact = MakeStringBound(exactText);
    EXPECT_EQ(art_search_range_external(&tree, &exact, &exact, CollectArtKey, &keys), 0);
    ASSERT_EQ(keys.size(), 1U);
    EXPECT_EQ(keys.front(), "abc");

    keys.clear();
    std::string longerText = "abcd";
    art_range_bound longer = MakeStringBound(longerText);
    EXPECT_EQ(art_search_range_external(&tree, &longer, &longer, CollectArtKey, &keys), 0);
    EXPECT_TRUE(keys.empty());

    EXPECT_EQ(art_tree_destroy(&tree), 0);
}

TEST(TestArtIndex, test_lsm_art_tree_search_prefix_and_range)
{
    LsmArtTree tree;
    int alpha = 1;
    int beta = 2;
    int gamma = 3;

    tree.Insert(std::string("alpha-1"), &alpha);
    tree.Insert(std::string("alpha-2"), &beta);
    tree.Insert(std::string("beta-1"), &gamma);
    tree.Delete(std::string("alpha-2"));

    std::vector<std::string> keys;
    const unsigned char prefix[] = "alpha";
    EXPECT_EQ(tree.SearchPrefix(prefix, 5, CollectArtKey, &keys), 0);
    EXPECT_EQ(keys.size(), 1U);
    EXPECT_EQ(keys.front(), "alpha-1");

    keys.clear();
    art_range_bound start = {reinterpret_cast<const unsigned char *>("alpha-0"), 7};
    art_range_bound finish = {reinterpret_cast<const unsigned char *>("beta-9"), 6};
    EXPECT_EQ(tree.SearchRange(start, finish, CollectArtKey, &keys), 0);
    EXPECT_EQ(keys.size(), 2U);
    EXPECT_NE(std::find(keys.begin(), keys.end(), "alpha-1"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "beta-1"), keys.end());
}

TEST(TestArtIndex, test_lsm_art_tree_background_flush)
{
    LsmArtTree tree;
    std::vector<int> values(FLUSH_WATERMARK + 4);
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<int>(i);
        tree.Insert(std::string("flush-key-") + std::to_string(i), &values[i]);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::vector<std::string> keys;
    const unsigned char prefix[] = "flush-key-";
    EXPECT_EQ(tree.SearchPrefix(prefix, 10, CollectArtKey, &keys), 0);
    EXPECT_GE(keys.size(), values.size());
}
