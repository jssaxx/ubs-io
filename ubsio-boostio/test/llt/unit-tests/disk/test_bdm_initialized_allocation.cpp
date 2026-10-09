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
#include <cstring>
#include <vector>
#include "gtest/gtest.h"
#include "bdm_allocator.h"
#include "bdm_core.h"

namespace {
constexpr uint64_t UNIT_CHUNK = 4096;

class TestInitializedAllocation : public testing::Test {
protected:
    void SetUp() override
    {
        para.metaOps.itemPtr = reinterpret_cast<uintptr_t>(this);
        para.metaOps.writeMeta = WriteMeta;
        para.metaOps.readMeta = ReadMeta;
        para.minChunkSize = UNIT_CHUNK;
        para.maxChunkSize = 8 * UNIT_CHUNK;
        para.totalSize = 8 * UNIT_CHUNK;
        metadata.assign(UNIT_CHUNK, 0);
        data.assign(para.totalSize, 'X');
        allocator = BdmAllocatorCreate(&para, 0);
        ASSERT_NE(allocator, 0U);
        events.clear();
    }

    void TearDown() override
    {
        if (allocator != 0) {
            BdmAllocatorDestroy(allocator);
        }
    }

    static int32_t WriteMeta(uintptr_t context, uint64_t offset, void *buf, uint64_t len)
    {
        auto &self = *reinterpret_cast<TestInitializedAllocation *>(context);
        self.events.push_back('M');
        if (offset + len > self.metadata.size()) {
            self.metadata.resize(offset + len);
        }
        std::memcpy(self.metadata.data() + offset, buf, len);
        return BDM_CODE_OK;
    }

    static int32_t ReadMeta(uintptr_t context, uint64_t offset, void *buf, uint64_t len)
    {
        auto &self = *reinterpret_cast<TestInitializedAllocation *>(context);
        if (offset > self.metadata.size() || len > self.metadata.size() - offset) {
            return BDM_CODE_ERR;
        }
        std::memcpy(buf, self.metadata.data() + offset, len);
        return BDM_CODE_OK;
    }

    static int32_t Initialize(uintptr_t context, uint64_t chunkId, uint64_t len)
    {
        auto &self = *reinterpret_cast<TestInitializedAllocation *>(context);
        self.events.push_back('Z');
        if (self.failInitialization) {
            return BDM_CODE_ERR_IO;
        }
        std::fill(self.data.begin() + chunkId * UNIT_CHUNK,
            self.data.begin() + chunkId * UNIT_CHUNK + len, 0);
        return BDM_CODE_OK;
    }

    BdmAllocatorPara para{};
    BdmAllocator allocator = 0;
    std::vector<char> metadata;
    std::vector<char> data;
    std::vector<char> events;
    bool failInitialization = false;
};

TEST_F(TestInitializedAllocation, initialize_before_persisting_split_and_exact_allocations)
{
    for (uint64_t size : {UNIT_CHUNK, 7 * UNIT_CHUNK}) {
        events.clear();
        uint64_t chunkId = UINT64_MAX;
        ASSERT_EQ(BdmAllocatorAllocChunkWithInit(allocator, 77, 0, size, &chunkId,
            Initialize, reinterpret_cast<uintptr_t>(this)), BDM_CODE_OK);
        ASSERT_GE(events.size(), 2U);
        EXPECT_EQ(events.front(), 'Z');
        EXPECT_TRUE(std::all_of(events.begin() + 1, events.end(), [](char event) { return event == 'M'; }));
        EXPECT_TRUE(std::all_of(data.begin() + chunkId * UNIT_CHUNK,
            data.begin() + chunkId * UNIT_CHUNK + size, [](char byte) { return byte == 0; }));
    }
    uint64_t total = 0, used = 0;
    ASSERT_EQ(BdmAllocatorGetCap(allocator, &total, &used), BDM_CODE_OK);
    EXPECT_EQ(used, total);
}

TEST_F(TestInitializedAllocation, initialization_failure_preserves_free_range_and_persisted_metadata)
{
    auto before = metadata;
    failInitialization = true;
    uint64_t chunkId = UINT64_MAX;
    EXPECT_EQ(BdmAllocatorAllocChunkWithInit(allocator, 77, 0, UNIT_CHUNK, &chunkId,
        Initialize, reinterpret_cast<uintptr_t>(this)), BDM_CODE_ERR_IO);
    EXPECT_EQ(metadata, before);
    EXPECT_EQ(events, std::vector<char>({'Z'}));
    EXPECT_EQ(chunkId, UINT64_MAX);
    uint64_t total = 0, used = 0;
    ASSERT_EQ(BdmAllocatorGetCap(allocator, &total, &used), BDM_CODE_OK);
    EXPECT_EQ(used, 0U);

    failInitialization = false;
    ASSERT_EQ(BdmAllocatorAllocChunkWithInit(allocator, 77, 0, UNIT_CHUNK, &chunkId,
        Initialize, reinterpret_cast<uintptr_t>(this)), BDM_CODE_OK);
    EXPECT_EQ(chunkId, 0U);
}

TEST_F(TestInitializedAllocation, normal_allocation_and_restore_do_not_initialize_existing_data)
{
    uint64_t chunkId = UINT64_MAX;
    ASSERT_EQ(BdmAllocatorAllocChunk(allocator, 77, 0, UNIT_CHUNK, &chunkId), BDM_CODE_OK);
    EXPECT_EQ(std::find(events.begin(), events.end(), 'Z'), events.end());
    EXPECT_TRUE(std::all_of(data.begin(), data.end(), [](char byte) { return byte == 'X'; }));
    ASSERT_EQ(BdmAllocatorDestroy(allocator), BDM_CODE_OK);
    allocator = BdmAllocatorCreate(&para, 1);
    ASSERT_NE(allocator, 0U);
    uint64_t restored = UINT64_MAX, size = 0, owner = 0, offset = 0;
    ASSERT_EQ(BdmAllocatorGetNextChunk(allocator, &restored, &size, &owner, &offset), BDM_CODE_OK);
    EXPECT_EQ(restored, chunkId);
    EXPECT_EQ(owner, 77U);
    EXPECT_EQ(size, UNIT_CHUNK);
    EXPECT_TRUE(std::all_of(data.begin(), data.end(), [](char byte) { return byte == 'X'; }));
}
}
