/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "test_wcache.h"
/*
TEST_F(WCacheTest, allocateFlowId) {
    uint64_t flowIdTest = 0;
    int ret = wCacheManager->AllocateFlowId(100, flowIdTest);
    EXPECT_EQ(ret, 0);
}

TEST_F(WCacheTest, createWCache) {
    uint64_t flowIdTest = 0;
    int ret = wCacheManager->AllocateFlowId(100, flowIdTest);
    EXPECT_EQ(ret, 0);
    ret = wCacheManager->CreateWCache(flowIdTest);
    EXPECT_EQ(ret, 0);
}

TEST_F(WCacheTest, deleteWCache) {
    int ret = wCacheManager->DeleteWCache(Pt);
    EXPECT_EQ(ret, 0);
}

BResult GetSlice(uint64_t flowIdTest, uint64_t flowOffset, uint64_t length) {
    SliceKey sliceKey{flowIdTest, flowOffset, FLOW_MEMORY, length};
    std::vector<FlowAddr> newAddrsTest;
    WCacheSlicePtr sliceTest = MakeRef<WCacheSlice>(0, 0, 0, 0, newAddrsTest);
    return wCacheManager->GetWCacheSlice(sliceKey, sliceTest);
}

TEST_F(WCacheTest, getWCacheSliceSuccess) {
    int ret = GetSlice(flowId, 0, 1024);
    EXPECT_EQ(ret, 0);
}

TEST_F(WCacheTest, getWCacheSliceFlowIdInValide) {
    int ret = GetSlice(NO_MAX_VALUE64, 0, 1024);
    EXPECT_EQ(ret, 3);
}

TEST_F(WCacheTest, getWCacheSliceFlowOffsetInValide) {
    int ret = GetSlice(flowId, NO_MAX_VALUE64, 1024);
    EXPECT_EQ(ret, 3);
}

TEST_F(WCacheTest, getWCacheSliceLengthInValide) {
    int ret = GetSlice(flowId, 0, NO_MAX_VALUE64);
    EXPECT_EQ(ret, 3);
}

TEST_F(WCacheTest, getWCacheSliceFlowIdUnExist) {
    int ret = GetSlice(30, 0, 1024);
    EXPECT_EQ(ret, 7);
}

BResult Put(uint64_t flowIdTest) {
    WCacheSlicePtr sliceTest = MakeRef<WCacheSlice>(flowIdTest, 0, 0, 1024, newAddrs);

    return wCacheManager->Put(key, sliceTest, reader);
}

TEST_F(WCacheTest, putSuccess) {
    EXPECT_EQ(Put(flowId), 0);
}

TEST_F(WCacheTest, putFailFlowIdUnExist) {
    EXPECT_EQ(Put(30), 7);
}

TEST_F(WCacheTest, putFailKeyNull) {
    int ret = wCacheManager->Put(nullptr, slice, reader);
    EXPECT_EQ(ret, 3);
}

TEST_F(WCacheTest, putFailSliceNull) {
    int ret = wCacheManager->Put(key, nullptr, reader);
    EXPECT_EQ(ret, 3);
}

TEST_F(WCacheTest, putFailReaderNull) {
    int ret = wCacheManager->Put(key, slice, nullptr);
    EXPECT_EQ(ret, 3);
}
*/