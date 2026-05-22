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

#include "test_memory.h"

#include <cstring>
#include <memory>
#include <vector>

#include "mms_mem_allocator.h"

using namespace ock::mms;

namespace {
constexpr uint32_t TEST_BUDDY_BLOCK_SIZE = 1024;
constexpr uint64_t TEST_BUDDY_NUMA_SIZE = 512 * 1024;
constexpr uint64_t TEST_BUDDY_LARGE_ALLOC_SIZE = 64 * 1024;

class BuddyAllocatorTestEnv {
public:
    explicit BuddyAllocatorTestEnv(uint64_t firstNumaSize, uint64_t secondNumaSize = 0,
                                   uint32_t blockSize = TEST_BUDDY_BLOCK_SIZE)
        : mAllocator(MMAP_AREA_VALUE),
          mFirstNumaMem(new char[firstNumaSize]),
          mSecondNumaMem(secondNumaSize == 0 ? nullptr : new char[secondNumaSize])
    {
        mOptions.numaNum = secondNumaSize == 0 ? 1 : 2;
        mOptions.numaId[0] = 0;
        mOptions.numaSize[0] = firstNumaSize;
        mOptions.numaAddress[0] = reinterpret_cast<uint64_t>(mFirstNumaMem.get());
        if (secondNumaSize != 0) {
            mOptions.numaId[1] = 1;
            mOptions.numaSize[1] = secondNumaSize;
            mOptions.numaAddress[1] = reinterpret_cast<uint64_t>(mSecondNumaMem.get());
        }

        mOptions.allocMode = MemAllocOptions::ALLOC_MODE_BUDDY;
        mOptions.blockNum = 1;
        mOptions.blockRate[0] = 10;
        mOptions.blockSize[0] = blockSize;
    }

    ~BuddyAllocatorTestEnv()
    {
        (void)mAllocator.Reset();
    }

    BResult Initialize()
    {
        return mAllocator.Initialize(mOptions);
    }

    MemAllocOptions &Options()
    {
        return mOptions;
    }

private:
    MmsMemAllocator mAllocator;
    MemAllocOptions mOptions{};
    std::unique_ptr<char[]> mFirstNumaMem;
    std::unique_ptr<char[]> mSecondNumaMem;
};

MmsMemAllocatorPtr GetValueAllocator()
{
    return MmsMemAllocator::Instance(MMAP_AREA_VALUE);
}
}

bool TestMemory::gSetup = false;

void TestMemory::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestMemory::TearDown()
{
    return;
}

TEST_F(TestMemory, test_buddy_allocator_reject_invalid_config)
{
    BuddyAllocatorTestEnv env(TEST_BUDDY_NUMA_SIZE);
    env.Options().blockNum = 0;
    EXPECT_EQ(MMS_INVALID_PARAM, env.Initialize());

    BuddyAllocatorTestEnv zeroBlockSizeEnv(TEST_BUDDY_NUMA_SIZE, 0, 0);
    EXPECT_EQ(MMS_INVALID_PARAM, zeroBlockSizeEnv.Initialize());
}

TEST_F(TestMemory, test_buddy_allocator_alloc_small_block_and_free_to_thread_cache)
{
    auto allocator = GetValueAllocator();

    uint16_t numaId = 0;
    uintptr_t blockAddr = 0;
    ASSERT_EQ(MMS_OK, allocator->MmsAlloc(128, numaId, blockAddr));
    ASSERT_NE(0U, blockAddr);
    EXPECT_GE(allocator->GetBlockSize(blockAddr), 128U);
    std::memset(reinterpret_cast<void *>(blockAddr), 0x5A, 128);

    EXPECT_EQ(MMS_OK, allocator->MmsFree(blockAddr));

    uintptr_t reusedBlockAddr = 0;
    ASSERT_EQ(MMS_OK, allocator->MmsAlloc(128, numaId, reusedBlockAddr));
    EXPECT_EQ(blockAddr, reusedBlockAddr);
    EXPECT_EQ(MMS_OK, allocator->MmsFree(reusedBlockAddr));
}

TEST_F(TestMemory, test_buddy_allocator_alloc_large_direct_block)
{
    auto allocator = GetValueAllocator();

    uint16_t numaId = 0;
    uintptr_t blockAddr = 0;
    ASSERT_EQ(MMS_OK, allocator->MmsAlloc(TEST_BUDDY_LARGE_ALLOC_SIZE, numaId, blockAddr));
    ASSERT_NE(0U, blockAddr);
    EXPECT_GE(allocator->GetBlockSize(blockAddr), TEST_BUDDY_LARGE_ALLOC_SIZE);
    std::memset(reinterpret_cast<void *>(blockAddr), 0xA5, TEST_BUDDY_LARGE_ALLOC_SIZE);
    EXPECT_EQ(MMS_OK, allocator->MmsFree(blockAddr));
}

TEST_F(TestMemory, test_buddy_allocator_thread_cache_drain_when_full)
{
    auto allocator = GetValueAllocator();

    std::vector<uintptr_t> blockAddrs;
    blockAddrs.reserve(BUDDY_CACHE_LIMIT_PER_THREAD + 1);
    uint16_t numaId = 0;
    for (uint64_t index = 0; index <= BUDDY_CACHE_LIMIT_PER_THREAD; ++index) {
        uintptr_t blockAddr = 0;
        ASSERT_EQ(MMS_OK, allocator->MmsAlloc(128, numaId, blockAddr));
        blockAddrs.emplace_back(blockAddr);
    }

    for (auto blockAddr : blockAddrs) {
        EXPECT_EQ(MMS_OK, allocator->MmsFree(blockAddr));
    }

    uintptr_t blockAddr = 0;
    ASSERT_EQ(MMS_OK, allocator->MmsAlloc(128, numaId, blockAddr));
    EXPECT_EQ(MMS_OK, allocator->MmsFree(blockAddr));
}

TEST_F(TestMemory, test_buddy_allocator_return_block_to_pool)
{
    auto allocator = GetValueAllocator();

    uint16_t numaId = 0;
    uintptr_t blockAddr = 0;
    ASSERT_EQ(MMS_OK, allocator->MmsAlloc(512, numaId, blockAddr));
    EXPECT_EQ(MMS_OK, allocator->ReturnBuddyBlockToPool(blockAddr));

    uintptr_t nextBlockAddr = 0;
    ASSERT_EQ(MMS_OK, allocator->MmsAlloc(512, numaId, nextBlockAddr));
    EXPECT_EQ(MMS_OK, allocator->MmsFree(nextBlockAddr));
}

TEST_F(TestMemory, test_buddy_allocator_reject_oversize_request)
{
    uint16_t numaId = 0;
    uintptr_t blockAddr = 0;
    EXPECT_EQ(MMS_ALLOC_FAIL, GetValueAllocator()->MmsAlloc(UINT64_MAX, numaId, blockAddr));
}

TEST_F(TestMemory, test_buddy_allocator_initialize_two_numa_pools)
{
    BuddyAllocatorTestEnv env(TEST_BUDDY_NUMA_SIZE, TEST_BUDDY_NUMA_SIZE);
    EXPECT_EQ(MMS_OK, env.Initialize());
}
