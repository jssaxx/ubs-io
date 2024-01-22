/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <gtest/gtest.h>
#include <mockcpp/mokc.h>
#include <mockcpp/mockcpp.hpp>
#include <cstdint>
#include "flow_manager.h"
#include "cache_slice_operator.h"
#include "rcache_manager.h"
#include "underfs.h"
#include "bdm_core.h"

using namespace ock::bio;

constexpr static uint64_t s_PtId = 1;
constexpr char *key = "123/123/123";
constexpr char *value = "test/read/cache/data";

static RCacheManager rcacheManager;
static CacheSliceOperator mSlicerOperator;
static bool setup = false;

class RCacheTest : public testing::Test {
public:
    static BResult SliceCopy(const SlicePtr &from, const SlicePtr &to)
    {
        if (from->GetFlowType() == FLOW_MEMORY) {
            memcpy(reinterpret_cast<void *>(to->GetAddrs()[0].chunkId+ to->GetAddrs()[0].chunkOffset),
                   reinterpret_cast<void *>(from->GetAddrs()[0].chunkId + from->GetAddrs()[0].chunkOffset),
                   from->GetLength());
        }
        return BIO_OK;
    }

    static BResult GetStub(const char *k, char *v)
    {
        memcpy(v, value, strlen(value) + 1);
        return BIO_OK; // 打桩接口
    }

    static BResult DiskAllocStub(uint32_t mediaId, uint64_t bucketId, uint64_t len, uint64_t *chunkId)
    {
        return BIO_OK;
    }

    static void DiskFreeStub(uint32_t mediaId, uint64_t len, uint64_t chunkId)
    {
        return;
    }

protected:
    void SetUp() override
    {
        if (setup) {
            return;
        }
        LoggerOptions loggerOptions;
        loggerOptions.minLogLevel = SPDLOG_LEVEL_INFO;
        loggerOptions.path = "./bio.log";
        auto logger = Logger::Instance(loggerOptions);
        auto ret = logger->Init();
        EXPECT_EQ(ret, 0);

        flowManager = FlowManager::Instance();
        ret = flowManager->Init();
        EXPECT_EQ(ret, 0);

        MemAllocator memAllocator;
        memAllocator.alloc = [](uint64_t size, uint64_t *addr) {
            *addr = reinterpret_cast<uint64_t>(malloc(size));
            return 0;
        };
        memAllocator.free = [](uint64_t addr) { free(reinterpret_cast<void *>(addr)); };
        FlowManager::RegisterMemAllocator(memAllocator);

        DiskAllocator diskAllocator;
        diskAllocator.alloc = DiskAllocStub;
        diskAllocator.free  = DiskFreeStub;

        ret = rcacheManager.Init();
        EXPECT_EQ(ret, 0);
        ret = rcacheManager.CreateRCache(s_PtId);
        EXPECT_EQ(ret, 0);

        //MOCKER(UnderFs::Instance()).stubs().will(returnValue(underFsMock));

        setup = true;
    }

    void TearDown() override
    {
        /*
        BResult ret = rcacheManager.DeleteRCache(s_PtId);
        EXPECT_EQ(ret, 0);

        rcacheManager.Exit();

        flowManager->Exit();
         */
    }

private:
    static FlowManagerPtr flowManager;
protected:
    //MockObject<UnderFsPtr> underFsMock;
};

FlowManagerPtr RCacheTest::flowManager = nullptr;

#if 0
TEST_F(RCacheTest, test_rcache_put_ok)
{
    uint64_t len = strlen(value) + 1;
    WCacheSlicePtr slicePtr = nullptr;

    auto ret = rcacheManager.AllocResources(s_PtId, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = mSlicerOperator.Copy(value, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = rcacheManager.Put(s_PtId, key, slicePtr);
    EXPECT_EQ(ret, 0);

    MOCKER(BdmRead).stubs().will(returnValue(0));
    MOCKER(BdmWrite).stubs().will(returnValue(0));
    uint64_t needEvictData = len;
    uint64_t haveEvictData;
    rcacheManager.GetRCacheInstanceByPtId(s_PtId)->EvictMemData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(needEvictData, haveEvictData);

    rcacheManager.GetRCacheInstanceByPtId(s_PtId)->EvictDiskData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(needEvictData, haveEvictData);
}

TEST_F(RCacheTest, test_rcache_get_ok)
{
    uint64_t len = strlen(value) + 1;
    char *key1 = "123/123/key1";
    WCacheSlicePtr slicePtr = nullptr;
    RCacheSlicePtr readSlicePtr = nullptr;

    auto ret = rcacheManager.AllocResources(s_PtId, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = mSlicerOperator.Copy(value, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = rcacheManager.Put(s_PtId, key1, slicePtr);
    EXPECT_EQ(ret, 0);

    FlowAddr flowAddr;
    flowAddr.chunkId = (uint64_t)malloc(len);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = len;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    readSlicePtr = MakeRef<RCacheSlice>(s_PtId, len, addrs, FLOW_MEMORY);
    EXPECT_NE(readSlicePtr, nullptr);
    ret = rcacheManager.Get(s_PtId, key1, 0ULL, readSlicePtr, RCacheTest::SliceCopy);
    ret = memcmp(value, reinterpret_cast<void *>(readSlicePtr->GetAddrs()[0].chunkId), len);
    EXPECT_EQ(ret, 0);
}

TEST_F(RCacheTest, test_rcache_load_ok)
{
    char *key2 = "123/123/key2";
    uint64_t len = strlen(value) + 1;
    RCacheSlicePtr readSlicePtr = nullptr;

    //MOCK_METHOD(underFsMock, Get).stubs().will(invoke(GetStub));
    auto ret = rcacheManager.Load(s_PtId, key2, 0, len);
    EXPECT_EQ(ret, 0);

    FlowAddr flowAddr;
    flowAddr.chunkId = (uint64_t)malloc(len);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = len;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    readSlicePtr = MakeRef<RCacheSlice>(s_PtId, len, addrs, FLOW_MEMORY);
    EXPECT_NE(readSlicePtr, nullptr);
    ret = rcacheManager.Get(s_PtId, key2, 0ULL, readSlicePtr, RCacheTest::SliceCopy);
    ret = memcmp(value, reinterpret_cast<void *>(readSlicePtr->GetAddrs()[0].chunkId), len);
    EXPECT_EQ(ret, 0);
}

TEST_F(RCacheTest, test_rcache_delete_ok)
{
    char *key4 = "123/123/key4";
    uint64_t len = strlen(value) + 1;
    WCacheSlicePtr slicePtr = nullptr;

    auto ret = rcacheManager.AllocResources(s_PtId, len, slicePtr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(slicePtr->GetLength(), len);

    ret = mSlicerOperator.Copy(value, slicePtr.Get());
    EXPECT_EQ(ret, 0);

    ret = rcacheManager.Put(s_PtId, key4, slicePtr);
    EXPECT_EQ(ret, 0);

    ret = rcacheManager.Delete(s_PtId, key4);
    EXPECT_EQ(ret, 0);
}
#endif