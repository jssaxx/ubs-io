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

#include <atomic>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "standalone_memory_pool.h"

using namespace ock::bio;

TEST(TestStandaloneMemoryPool, test_alloc_exhaust_and_reuse)
{
    StandaloneMemoryPoolPtr pool = MakeRef<StandaloneMemoryPool>();
    ASSERT_NE(pool, nullptr);
    ASSERT_EQ(pool->Start(IO_SIZE_4K, IO_SIZE_4K * NO_2), BIO_OK);

    uint64_t first = 0;
    uint64_t second = 0;
    uint64_t third = 0;
    EXPECT_EQ(pool->Alloc(NO_1024, &first), BIO_OK);
    EXPECT_EQ(pool->Alloc(NO_1024, &second), BIO_OK);
    EXPECT_NE(first, 0);
    EXPECT_NE(second, 0);
    EXPECT_NE(first, second);
    EXPECT_EQ(pool->Alloc(NO_1024, &third), BIO_ALLOC_FAIL);
    EXPECT_EQ(pool->GetUsedSize(), IO_SIZE_4K * NO_2);

    pool->Free(first);
    pool->Free(second);
    EXPECT_EQ(pool->GetUsedSize(), 0);

    EXPECT_EQ(pool->Alloc(NO_1024, &third), BIO_OK);
    EXPECT_NE(third, 0);
    pool->Free(third);
    pool->Stop();
}

TEST(TestStandaloneMemoryPool, test_alloc_rejects_invalid_state_and_size)
{
    StandaloneMemoryPoolPtr pool = MakeRef<StandaloneMemoryPool>();
    ASSERT_NE(pool, nullptr);

    uint64_t address = 0;
    EXPECT_EQ(pool->Alloc(NO_1024, &address), BIO_NOT_READY);
    EXPECT_EQ(address, 0);
    EXPECT_EQ(pool->Alloc(NO_1024, nullptr), BIO_INVALID_PARAM);
    EXPECT_EQ(pool->Start(0, IO_SIZE_4K), BIO_INVALID_PARAM);

    ASSERT_EQ(pool->Start(IO_SIZE_4K, IO_SIZE_4K), BIO_OK);
    EXPECT_EQ(pool->Alloc(IO_SIZE_4K + 1, &address), BIO_ALLOC_FAIL);
    EXPECT_EQ(address, 0);
    pool->Stop();
}

TEST(TestStandaloneMemoryPool, test_zero_pool_starts_but_cannot_allocate)
{
    StandaloneMemoryPoolPtr pool = MakeRef<StandaloneMemoryPool>();
    ASSERT_NE(pool, nullptr);
    ASSERT_EQ(pool->Start(IO_SIZE_4K, 0), BIO_OK);
    EXPECT_EQ(pool->GetPoolSize(), 0);
    EXPECT_EQ(pool->GetUsedSize(), 0);

    uint64_t address = 0;
    EXPECT_NE(pool->Alloc(NO_1024, &address), BIO_OK);
    EXPECT_EQ(address, 0);
    EXPECT_EQ(pool->GetUsedSize(), 0);
    pool->Stop();
}

TEST(TestStandaloneMemoryPool, test_invalid_free_address_does_not_change_used_size)
{
    StandaloneMemoryPoolPtr pool = MakeRef<StandaloneMemoryPool>();
    ASSERT_NE(pool, nullptr);
    ASSERT_EQ(pool->Start(IO_SIZE_4K, IO_SIZE_4K), BIO_OK);

    uint64_t address = 0;
    ASSERT_EQ(pool->Alloc(NO_1024, &address), BIO_OK);
    ASSERT_NE(address, 0);
    EXPECT_EQ(pool->GetUsedSize(), IO_SIZE_4K);

    pool->Free(address + 1);
    EXPECT_EQ(pool->GetUsedSize(), IO_SIZE_4K);

    pool->Free(address);
    EXPECT_EQ(pool->GetUsedSize(), 0);
    pool->Stop();
}

TEST(TestStandaloneMemoryPool, test_alloc_free_concurrent)
{
    constexpr uint32_t threadCount = 8;
    constexpr uint32_t loopCount = 1000;
    constexpr uint32_t blockCount = 128;

    StandaloneMemoryPoolPtr pool = MakeRef<StandaloneMemoryPool>();
    ASSERT_NE(pool, nullptr);
    ASSERT_EQ(pool->Start(IO_SIZE_4K, IO_SIZE_4K * blockCount), BIO_OK);

    std::atomic<uint32_t> failed{ 0 };
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (uint32_t idx = 0; idx < threadCount; idx++) {
        workers.emplace_back([&pool, &failed]() {
            for (uint32_t loop = 0; loop < loopCount; loop++) {
                uint64_t address = 0;
                auto ret = pool->Alloc(NO_1024, &address);
                if (ret != BIO_OK || address == 0) {
                    failed.fetch_add(NO_1);
                    continue;
                }
                pool->Free(address);
            }
        });
    }

    for (auto &worker : workers) {
        worker.join();
    }
    EXPECT_EQ(failed.load(), 0);
    EXPECT_EQ(pool->GetUsedSize(), 0);
    pool->Stop();
}
