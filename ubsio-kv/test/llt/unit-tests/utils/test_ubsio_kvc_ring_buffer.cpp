/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <string>
#include "test_common.h"
#include "ubsio_kvc_ring_buffer.h"

using namespace ock::ubsio;

namespace {
TEST_F(KvTest, RingBufferCoversWrapFullAndFlexibleOperations)
{
    RingBuffer<int> invalid(0);
    EXPECT_EQ(invalid.Initialize(), -1);
    invalid.Capacity(3);
    EXPECT_EQ(invalid.Capacity(), 3U);
    EXPECT_EQ(invalid.Initialize(), 0);
    EXPECT_EQ(invalid.Initialize(), 0);

    EXPECT_TRUE(invalid.PushBack(1));
    EXPECT_TRUE(invalid.PushBack(2));
    EXPECT_TRUE(invalid.PushFront(0));
    EXPECT_FALSE(invalid.PushBack(3));

    int value = -1;
    EXPECT_TRUE(invalid.PopFront(value));
    EXPECT_EQ(value, 0);

    int values[3]{};
    EXPECT_TRUE(invalid.PushBack(3));
    EXPECT_TRUE(invalid.PopFrontN(values, 3));
    EXPECT_EQ(values[0], 1);
    EXPECT_FALSE(invalid.PopFront(value));
    EXPECT_FALSE(invalid.PopFrontN(values, 1));

    EXPECT_TRUE(invalid.PushBack(4));
    EXPECT_TRUE(invalid.PushBack(5));
    uint32_t count = 3;
    EXPECT_TRUE(invalid.PopFrontNFlex(values, count));
    EXPECT_EQ(count, 2U);
    count = 1;
    EXPECT_FALSE(invalid.PopFrontNFlex(values, count));

    invalid.UnInitialize();
    invalid.UnInitialize();
}

TEST_F(KvTest, RingBufferBulkAndBlockingQueueUtilities)
{
    RingBuffer<int> ring(4);
    ring.Capacity(5);
    EXPECT_EQ(ring.Capacity(), 5U);
    ASSERT_EQ(ring.Initialize(), 0);
    ring.Capacity(9);
    EXPECT_EQ(ring.Capacity(), 5U);

    int backItems[]{1, 2, 3};
    EXPECT_TRUE(ring.PushBackN(backItems, 3));
    EXPECT_EQ(ring.Size(), 3U);
    EXPECT_NE(ring.ToString().find("capacity 5, count 3"), std::string::npos);
    EXPECT_FALSE(ring.PushBackN(backItems, 3));

    int frontItems[]{9, 8};
    EXPECT_TRUE(ring.PushFrontN(frontItems, 2));
    EXPECT_FALSE(ring.PushFrontN(frontItems, 1));
    int values[5]{};
    EXPECT_TRUE(ring.PopFrontN(values, 5));
    EXPECT_EQ(values[0], 8);
    EXPECT_EQ(values[1], 9);
    EXPECT_EQ(values[4], 3);
    EXPECT_EQ(ring.Size(), 0U);
    EXPECT_FALSE(ring.PushBackN(backItems, 6));
    EXPECT_FALSE(ring.PushFrontN(frontItems, 6));

    EXPECT_TRUE(ring.PushBackN(backItems, 3));
    EXPECT_TRUE(ring.PopFrontN(values, 3));

    RingBufferBlockingQueue<int> queue(2);
    ASSERT_EQ(queue.Initialize(), 0);
    const int one = 1;
    int two = 2;
    EXPECT_TRUE(queue.Enqueue(one));
    EXPECT_TRUE(queue.Enqueue(two));
    EXPECT_FALSE(queue.Enqueue(one));
    EXPECT_FALSE(queue.Enqueue(two));

    int value = 0;
    EXPECT_TRUE(queue.Dequeue(value));
    EXPECT_EQ(value, 1);
    const int zero = 0;
    EXPECT_TRUE(queue.EnqueueFirst(zero));
    EXPECT_FALSE(queue.EnqueueFirst(zero));
    EXPECT_FALSE(queue.EnqueueFirst(two));
    EXPECT_TRUE(queue.Dequeue(value));
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(queue.Dequeue(value));
    EXPECT_EQ(value, 2);

    int three = 3;
    EXPECT_TRUE(queue.EnqueueFirst(three));
    EXPECT_TRUE(queue.Enqueue(two));
    EXPECT_FALSE(queue.EnqueueFirst(three));
    EXPECT_TRUE(queue.Dequeue(value));
    EXPECT_EQ(value, 3);
    EXPECT_TRUE(queue.Dequeue(value));
    EXPECT_EQ(value, 2);
}
}
