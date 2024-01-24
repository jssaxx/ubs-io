/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <gtest/gtest.h>
#include "wcache.h"
#include "flow_manager.h"
#include "wcache_manager.h"
#include <cstdint>

using namespace ock::bio;
bool setup = false;

constexpr uint64_t Pt = 1;
constexpr char *key = "123/123/123";
uint64_t flowId = 0;

static WCacheManager *wCacheManager = new WCacheManager();
static WCacheSlicePtr slice;
static std::vector<FlowAddr> newAddrs;
auto reader = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    return 0;
};

class WCacheTest : public testing::Test {
protected:
    void SetUp() override {
        if (!setup) {
            newAddrs.emplace_back(1, 2, 1024);
            LoggerOptions loggerOptions;
            loggerOptions.minLogLevel = SPDLOG_LEVEL_INFO;
            loggerOptions.path = "./bio.log";
            auto logger = Logger::Instance(loggerOptions);
            auto ret = logger->Init();
            EXPECT_EQ(ret, 0);

            auto flowManager = FlowManager::Instance();
            ret = flowManager->Init();
            EXPECT_EQ(ret, 0);

            MemAllocator memAllocator;
            memAllocator.alloc = [](uint64_t size, uint64_t *addr) {
                *addr = reinterpret_cast<uint64_t>(malloc(size));
                return 0;
            };
            memAllocator.free = [](uint64_t addr) {
                free(reinterpret_cast<void *>(addr));
            };
            FlowManager::RegisterMemAllocator(memAllocator);

            RCacheManagerPtr rCacheManagerPtr = MakeRef<RCacheManager>();
            ret = wCacheManager->Init(rCacheManagerPtr);
            EXPECT_EQ(ret, 0);
            ret = wCacheManager->AllocateFlowId(Pt, flowId);
            EXPECT_EQ(ret, 0);
            ret = wCacheManager->CreateWCache(flowId);
            EXPECT_EQ(ret, 0);
            slice = MakeRef<WCacheSlice>(flowId, 0, 0, 1024, newAddrs);
            ret = wCacheManager->Put(key, slice, reader);
            EXPECT_EQ(ret, 0);
        }
        setup = true;
    }

    void TearDown() override {
    }
};