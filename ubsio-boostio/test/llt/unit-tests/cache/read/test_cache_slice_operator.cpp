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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include "gtest/gtest.h"
#include "cache_slice.h"
#include "cache_slice_operator.h"

using namespace ock::bio;

namespace {
SlicePtr MakeSlice(std::vector<char> &buffer, const std::vector<std::pair<uint32_t, uint32_t>> &ranges,
    FlowType flowType = FLOW_MEMORY)
{
    std::vector<FlowAddr> addrs;
    uint64_t length = 0;
    for (const auto &range : ranges) {
        addrs.emplace_back(reinterpret_cast<uint64_t>(buffer.data()), range.first, range.second);
        length += range.second;
    }
    return MakeRef<Slice>(length, addrs, flowType);
}

SlicePtr MakeDiskSlice(uint64_t length, uint64_t chunkId = 1)
{
    std::vector<FlowAddr> addrs;
    addrs.emplace_back(chunkId, 0, length);
    return MakeRef<Slice>(length, addrs, FLOW_DISK);
}
}

TEST(TestCacheSliceOperator, CopyMemoryToMemoryAcrossDifferentSegments)
{
    std::vector<char> source{ '0', 'a', 'b', 'c', 'd', 'e', 'f', '9' };
    std::vector<char> target(10, '-');
    auto from = MakeSlice(source, { { 1, 2 }, { 3, 4 } });
    auto to = MakeSlice(target, { { 2, 1 }, { 3, 3 }, { 6, 2 } });

    CacheSliceOperator op;
    ASSERT_EQ(op.Copy(from, to), BIO_OK);
    EXPECT_EQ(std::string(target.data() + 2, 6), "abcdef");
    EXPECT_EQ(target.front(), '-');
    EXPECT_EQ(target.back(), '-');

    EXPECT_EQ(op.Copy(from, from), BIO_OK);
    EXPECT_EQ(op.Copy(SlicePtr(), to), BIO_INVALID_PARAM);
    EXPECT_EQ(op.Copy(from, SlicePtr()), BIO_INVALID_PARAM);

    std::vector<char> shortBuffer(5, 0);
    auto shortSlice = MakeSlice(shortBuffer, { { 0, 5 } });
    EXPECT_EQ(op.Copy(from, shortSlice), BIO_INVALID_PARAM);
}

TEST(TestCacheSliceOperator, CopyCharacterBufferToAndFromSegmentedMemory)
{
    const std::array<char, 7> input{ 'A', 'B', 'C', 'D', 'E', 'F', 'G' };
    std::vector<char> storage(12, '.');
    auto slice = MakeSlice(storage, { { 1, 3 }, { 6, 4 } });
    CacheSliceOperator op;

    ASSERT_EQ(op.Copy(input.data(), slice), BIO_OK);
    EXPECT_EQ(std::string(storage.data() + 1, 3), "ABC");
    EXPECT_EQ(std::string(storage.data() + 6, 4), "DEFG");

    std::array<char, 7> output{};
    ASSERT_EQ(op.Copy(slice, output.data(), output.size()), BIO_OK);
    EXPECT_EQ(output, input);

    EXPECT_EQ(op.Copy(static_cast<const char *>(nullptr), slice), BIO_INVALID_PARAM);
    EXPECT_EQ(op.Copy(input.data(), SlicePtr()), BIO_INVALID_PARAM);
    EXPECT_EQ(op.Copy(slice, static_cast<char *>(nullptr), output.size()), BIO_INVALID_PARAM);
    EXPECT_EQ(op.Copy(slice, output.data(), 0), BIO_INVALID_PARAM);
    EXPECT_NE(op.Copy(slice, output.data(), 3), BIO_OK);
}

TEST(TestCacheSliceOperator, CopyCharacterRangeHandlesBoundaries)
{
    std::vector<char> storage(12, '.');
    auto slice = MakeSlice(storage, { { 0, 3 }, { 4, 4 }, { 9, 3 } });
    CacheSliceOperator op;

    ASSERT_EQ(op.Copy("WXYZ", 2, 4, slice), BIO_OK);
    EXPECT_EQ(storage[2], 'W');
    EXPECT_EQ(std::string(storage.data() + 4, 3), "XYZ");

    auto snapshot = storage;
    EXPECT_EQ(op.Copy("ignored", 1, 0, slice), BIO_OK);
    EXPECT_EQ(storage, snapshot);
    EXPECT_EQ(op.Copy(static_cast<const char *>(nullptr), 0, 1, slice), BIO_INVALID_PARAM);
    EXPECT_EQ(op.Copy("x", 0, 1, MakeDiskSlice(1)), BIO_INVALID_PARAM);
}

TEST(TestCacheSliceOperator, GetPartialSliceSpansAddressBoundaries)
{
    std::vector<char> storage(24, 0);
    auto whole = MakeSlice(storage, { { 1, 4 }, { 8, 5 }, { 16, 3 } });
    CacheSliceOperator op;
    SlicePtr partial;

    ASSERT_EQ(op.GetSliceFromSliceIO(partial, whole, 3, 7), BIO_OK);
    ASSERT_NE(partial, nullptr);
    EXPECT_EQ(partial->GetLength(), 7);
    ASSERT_EQ(partial->GetAddrs().size(), 3);
    EXPECT_EQ(partial->GetAddrs()[0].chunkOffset, 4);
    EXPECT_EQ(partial->GetAddrs()[0].chunkLen, 1);
    EXPECT_EQ(partial->GetAddrs()[1].chunkOffset, 8);
    EXPECT_EQ(partial->GetAddrs()[1].chunkLen, 5);
    EXPECT_EQ(partial->GetAddrs()[2].chunkOffset, 16);
    EXPECT_EQ(partial->GetAddrs()[2].chunkLen, 1);

    ASSERT_EQ(op.GetSliceFromSliceIO(partial, whole, 4, 5), BIO_OK);
    ASSERT_EQ(partial->GetAddrs().size(), 1);
    EXPECT_EQ(partial->GetAddrs()[0].chunkOffset, 8);
    EXPECT_EQ(partial->GetAddrs()[0].chunkLen, 5);
    EXPECT_EQ(op.GetSliceFromSliceIO(partial, SlicePtr(), 0, 1), BIO_INVALID_PARAM);
}

TEST(TestCacheSliceOperator, RejectsUnsupportedDiskCopiesAndDetectsBatchCandidates)
{
    std::vector<char> memory(8, 0);
    auto memorySlice = MakeSlice(memory, { { 0, 8 } });
    auto disk = MakeDiskSlice(8, 11);
    auto otherDisk = MakeDiskSlice(8, 12);
    CacheSliceOperator op;

    EXPECT_EQ(op.Copy(disk, otherDisk), BIO_ERR);
    EXPECT_TRUE(CacheSliceOperator::CanBatchDiskToMemory(disk, memorySlice));
    EXPECT_FALSE(CacheSliceOperator::CanBatchDiskToMemory(memorySlice, disk));
    EXPECT_FALSE(CacheSliceOperator::CanBatchDiskToMemory(disk, otherDisk));
    EXPECT_FALSE(CacheSliceOperator::CanBatchDiskToMemory(SlicePtr(), memorySlice));
    EXPECT_FALSE(CacheSliceOperator::CanBatchDiskToMemory(disk, SlicePtr()));

    std::vector<FlowAddr> sameAddrs;
    sameAddrs.emplace_back(11, 0, 8);
    auto sameAddressMemory = MakeRef<Slice>(8, sameAddrs, FLOW_MEMORY);
    EXPECT_TRUE(CacheSliceOperator::CanBatchDiskToMemory(disk, sameAddressMemory));
}

TEST(TestCacheSlice, SerializeAndDeserializeReadCacheSlice)
{
    std::vector<FlowAddr> addrs;
    addrs.emplace_back(101, 3, 5);
    addrs.emplace_back(202, 7, 9);
    auto original = MakeRef<RCacheSlice>(17, 14, addrs, FLOW_DISK);
    original->SetDataCrc(0x12345678);
    std::vector<char> data(original->GetSerializeLen());
    uint64_t serializedLength = 0;

    EXPECT_EQ(original->Serialize(nullptr, data.size(), serializedLength), BIO_INVALID_PARAM);
    EXPECT_NE(original->Serialize(data.data(), 1, serializedLength), BIO_OK);
    ASSERT_EQ(original->Serialize(data.data(), data.size(), serializedLength), BIO_OK);
    EXPECT_EQ(serializedLength, data.size());

    std::vector<FlowAddr> emptyAddrs;
    auto restored = MakeRef<RCacheSlice>(0, 0, emptyAddrs);
    EXPECT_EQ(restored->Deserialize(nullptr, data.size()), BIO_INVALID_PARAM);
    EXPECT_EQ(restored->Deserialize(data.data(), 1), BIO_INVALID_PARAM);
    ASSERT_EQ(restored->Deserialize(data.data(), data.size()), BIO_OK);
    EXPECT_EQ(restored->GetPtId(), 17);
    EXPECT_EQ(restored->GetLength(), 14);
    EXPECT_EQ(restored->GetFlowType(), FLOW_DISK);
    EXPECT_EQ(restored->GetDataCrc(), 0x12345678U);
    ASSERT_EQ(restored->GetAddrs().size(), 2);
    EXPECT_EQ(restored->GetAddrs()[1].chunkId, 202);
    EXPECT_FALSE(restored->ToString().empty());
}

TEST(TestCacheSlice, SerializeAndDeserializeWriteCacheSlice)
{
    std::vector<FlowAddr> addrs;
    addrs.emplace_back(303, 4, 6);
    WCacheSlice original(21, 22, 23, 6, addrs, FLOW_MEMORY);
    original.SetDataCrc(77);
    std::vector<char> data(original.GetSerializeLen());
    uint64_t serializedLength = 0;

    EXPECT_EQ(original.Serialize(nullptr, data.size(), serializedLength), BIO_INVALID_PARAM);
    EXPECT_NE(original.Serialize(data.data(), sizeof(uint64_t) - 1, serializedLength), BIO_OK);
    EXPECT_NE(original.Serialize(data.data(), sizeof(uint64_t), serializedLength), BIO_OK);
    EXPECT_NE(original.Serialize(data.data(), sizeof(uint64_t) * 2, serializedLength), BIO_OK);
    ASSERT_EQ(original.Serialize(data.data(), data.size(), serializedLength), BIO_OK);

    WCacheSlice restored;
    EXPECT_EQ(restored.Deserialize(nullptr, data.size()), BIO_INVALID_PARAM);
    EXPECT_EQ(restored.Deserialize(data.data(), sizeof(uint64_t) - 1), BIO_INVALID_PARAM);
    EXPECT_EQ(restored.Deserialize(data.data(), sizeof(uint64_t)), BIO_INVALID_PARAM);
    EXPECT_EQ(restored.Deserialize(data.data(), sizeof(uint64_t) * 2), BIO_INVALID_PARAM);
    ASSERT_EQ(restored.Deserialize(data.data(), data.size()), BIO_OK);
    EXPECT_EQ(restored.GetFlowId(), 21);
    EXPECT_EQ(restored.GetOffsetInFlow(), 22);
    EXPECT_EQ(restored.GetIndexInFlow(), 23);
    EXPECT_EQ(restored.GetLength(), 6);
    EXPECT_EQ(restored.GetDataCrc(), 77U);
    EXPECT_FALSE(restored.ToString().empty());
}

TEST(TestCacheSlice, SliceKeyValidationRejectsSentinelFields)
{
    EXPECT_TRUE(SliceKey(1, 2, FLOW_MEMORY, 3, 4).Validate());
    EXPECT_FALSE(SliceKey(NO_MAX_VALUE64, 2, FLOW_MEMORY, 3, 4).Validate());
    EXPECT_FALSE(SliceKey(1, NO_MAX_VALUE64, FLOW_MEMORY, 3, 4).Validate());
    EXPECT_FALSE(SliceKey(1, 2, FLOW_MEMORY, NO_MAX_VALUE64, 4).Validate());
}
