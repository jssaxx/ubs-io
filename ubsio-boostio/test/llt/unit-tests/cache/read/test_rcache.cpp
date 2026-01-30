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

#include <mockcpp/mockcpp.hpp>
#include <cstdint>
#include "underfs.h"
#include "bio_server.h"
#include "bio_mock.h"
#include "bio_config_instance.h"
#include "cache_slice_operator.h"
#include "rcache_manager.h"
#include "bdm_core.h"
#include "tracepoint.h"
#include "flow_manager.h"
#include "test_rcache.h"

using namespace ock::bio;

bool TestRCache::gSetup = false;

void TestRCache::SetUp()
{
    if (gSetup) {
        return;
    }

    gSetup = true;
    return;
}

void TestRCache::TearDown()
{
    return;
}

static RCacheManagerPtr gRCacheManager = RCacheManager::Instance();
static CacheSliceOperator gSlicerOperator;

static constexpr uint64_t G_PT_ID = 0;
static constexpr uint64_t G_PT_V = 1;
static constexpr Key G_KEY = "123123key";
static constexpr char *G_VALUE = "test/read/cache/data";

static auto rWriter = [](const SlicePtr &from, const SlicePtr &to) -> BResult {
    CacheSliceOperator sliceOperator;
    auto ret = sliceOperator.Copy(from, to);
    EXPECT_EQ(ret, BIO_OK);
    return ret;
};

TEST_F(TestRCache, test_rcache_put_ok)
{
    LOG_INFO("test_rcache_put_ok");
    uint64_t len = strlen(G_VALUE) + 1;
    WCacheSlicePtr slicePtr = nullptr;
    auto ret = gRCacheManager->AllocResources(G_PT_ID, len, slicePtr);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(slicePtr->GetLength(), len);
    ret = gSlicerOperator.Copy(G_VALUE, slicePtr.Get());
    EXPECT_EQ(ret, BIO_OK);

    ret = gRCacheManager->Put(G_PT_ID, G_KEY, slicePtr);
    EXPECT_EQ(ret, BIO_OK);

    uint64_t totalCap = 0;
    uint64_t usedCap = 0;
    ret = BdmGetCapacity(NO_MAX_VALUE32, &totalCap, &usedCap);
    EXPECT_EQ(ret, BDM_CODE_NOT_EXIST);
    ret = BdmGetCapacity(0, &totalCap, &usedCap);
    EXPECT_EQ(ret, BIO_OK);

    // evict memory data to disk
    uint64_t needEvictData = len;
    uint64_t haveEvictData = 0;
    gRCacheManager->GetRCacheInstanceByPtId(G_PT_ID)->EvictMemData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(needEvictData, haveEvictData);

    // evict disk data to underfs
    gRCacheManager->GetRCacheInstanceByPtId(G_PT_ID)->EvictDiskData(needEvictData, haveEvictData);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(needEvictData, haveEvictData);

    ret = gRCacheManager->Delete(G_PT_ID, G_KEY);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);

    ret = Cache::Instance().Delete(G_PT_ID, G_KEY);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_bio_server_expire_clear)
{
    LOG_INFO("test_bio_server_expire_clear");
    // case1: test invalid pt id
    CmPtInfo ptInfo;
    ptInfo.ptId = NO_2;
    ptInfo.version = G_PT_V;
    auto ret = BioServer::Instance()->GetMirrorCrb()->JobExpiredClear(ptInfo);
    EXPECT_EQ(ret, BIO_OK);

    // case2: test input pt version less than current rcache version
    ptInfo.ptId = G_PT_ID;
    ptInfo.version = 0;
    ret = BioServer::Instance()->GetMirrorCrb()->JobExpiredClear(ptInfo);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_rcache_put_ptid_not_exist)
{
    LOG_INFO("test_rcache_put_ptid_not_exist");
    uint64_t len = strlen(G_VALUE) + 1;
    WCacheSlicePtr slicePtr = nullptr;
    auto ret = gRCacheManager->AllocResources(G_PT_ID, len, slicePtr);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(slicePtr->GetLength(), len);
    ret = gSlicerOperator.Copy(G_VALUE, slicePtr.Get());
    EXPECT_EQ(ret, BIO_OK);

    uint64_t ptId = NO_10;
    ret = gRCacheManager->Put(ptId, G_KEY, slicePtr);
    EXPECT_EQ(ret, BIO_INNER_RETRY);

    ret = gRCacheManager->Delete(ptId, G_KEY);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
}

TEST_F(TestRCache, test_rcache_get_ok)
{
    LOG_INFO("test_rcache_get_ok");
    uint64_t len = strlen(G_VALUE) + 1;
    WCacheSlicePtr slicePtr = nullptr;
    auto ret = gRCacheManager->AllocResources(G_PT_ID, len, slicePtr);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(slicePtr->GetLength(), len);
    ret = gSlicerOperator.Copy(G_VALUE, slicePtr.Get());
    EXPECT_EQ(ret, BIO_OK);

    Key key = "123123key1";
    ret = gRCacheManager->Put(G_PT_ID, key, slicePtr);
    EXPECT_EQ(ret, BIO_OK);

    char *buffer = static_cast<char *>(malloc(len));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = len;
    std::vector<FlowAddr> addrVec;
    addrVec.push_back(flowAddr);

    RCacheSlicePtr readSlicePtr = MakeRef<RCacheSlice>(G_PT_ID, len, addrVec, FLOW_MEMORY);
    EXPECT_NE(readSlicePtr, nullptr);

    uint64_t offset = 0;
    uint64_t realLen = 0;
    ret = gRCacheManager->Get(G_PT_ID, key, offset, readSlicePtr, rWriter, realLen);
    EXPECT_EQ(ret, BIO_OK);
    ret = memcmp(G_VALUE, reinterpret_cast<void *>(readSlicePtr->GetAddrs()[0].chunkId), realLen);
    EXPECT_EQ(ret, BIO_OK);
    free(buffer);

    ret = gRCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_rcache_load_ok)
{
    LOG_INFO("test_rcache_load_ok");
    Key key = "123123key2";
    uint64_t len = strlen(G_VALUE) + 1;
    auto ret = UnderFs::Instance()->Put(key, G_VALUE, len);
    EXPECT_EQ(ret, BIO_OK);

    uint64_t realLen;
    ret = Cache::Instance().Load(G_PT_ID, key, 0, len, realLen);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(realLen, len);

    char *buffer = static_cast<char *>(malloc(len));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = len;
    std::vector<FlowAddr> addrVec;
    addrVec.push_back(flowAddr);
    RCacheSlicePtr readSlicePtr = MakeRef<RCacheSlice>(G_PT_ID, len, addrVec, FLOW_MEMORY);
    EXPECT_NE(readSlicePtr, nullptr);

    ret = gRCacheManager->Get(G_PT_ID, key, 0ULL, readSlicePtr, rWriter, realLen);
    EXPECT_EQ(ret, BIO_OK);
    ret = memcmp(G_VALUE, reinterpret_cast<void *>(readSlicePtr->GetAddrs()[0].chunkId), len);
    EXPECT_EQ(ret, BIO_OK);
    free(buffer);

    ret = gRCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_rcache_delete_ok)
{
    LOG_INFO("test_rcache_delete_ok");
    uint64_t len = strlen(G_VALUE) + 1;
    WCacheSlicePtr slicePtr = nullptr;
    auto ret = gRCacheManager->AllocResources(G_PT_ID, len, slicePtr);
    EXPECT_EQ(ret, BIO_OK);
    EXPECT_EQ(slicePtr->GetLength(), len);
    ret = gSlicerOperator.Copy(G_VALUE, slicePtr.Get());
    EXPECT_EQ(ret, BIO_OK);

    Key key = "123123key3";
    ret = gRCacheManager->Put(G_PT_ID, key, slicePtr);
    EXPECT_EQ(ret, BIO_OK);

    ret = gRCacheManager->Delete(G_PT_ID, key);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_cache_create_rcache_err)
{
    LOG_INFO("test_cache_create_rcache_err");
    // case1: test destroy invalid rcache
    uint16_t ptId = NO_2;
    auto ret = Cache::Instance().DestroyRCache(ptId);
    EXPECT_EQ(ret, BIO_OK);

    // case2: test create existed rcache
    ret = Cache::Instance().CreateRCache(G_PT_ID, G_PT_V);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_cache_recover_return_err)
{
    LOG_INFO("test_cache_recover_return_err");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "CACHE_RECOVER_FM_GET_ALL_OBJECT_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, BIO_NOT_READY);
    BioHvsDeactiveTracePoint(0, "CACHE_RECOVER_FM_GET_ALL_OBJECT_FAIL");
}

TEST_F(TestRCache, test_cache_init_rcache_init_err)
{
    LOG_INFO("test_cache_init_rcache_init_err");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "RCACHE_MANAGER_INIT_FAIL", 0, 1, userParam);
    auto ret = Cache::Instance().Init();
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "RCACHE_MANAGER_INIT_FAIL");
}

TEST_F(TestRCache, test_rcache_slice)
{
    LOG_INFO("test_rcache_slice");
    uint64_t length = NO_1024;
    char *buffer = static_cast<char*>(malloc(length));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = length;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    RCacheSlicePtr readSlicePtr = MakeRef<RCacheSlice>(G_PT_ID, length, addrs, FLOW_MEMORY);
    readSlicePtr->GetSerializeLen();

    auto ret = readSlicePtr->Serialize(nullptr, 0, length);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = readSlicePtr->Deserialize(nullptr, length);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    uint64_t outLength = 0;
    char* sliceBuf = static_cast<char *>(malloc(length));
    ret = readSlicePtr->Serialize(sliceBuf, length, outLength);
    EXPECT_EQ(ret, BIO_OK);

    ret = readSlicePtr->Deserialize(sliceBuf, length);
    EXPECT_EQ(ret, BIO_OK);

    ret = readSlicePtr->Deserialize(sliceBuf, 0);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    free(sliceBuf);
    free(buffer);
}

TEST_F(TestRCache, test_wcache_slice)
{
    LOG_INFO("test_wcache_slice");
    uint64_t length = NO_1024;
    char *buffer = static_cast<char*>(malloc(length));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = length;
    std::vector<FlowAddr> addrs;
    addrs.push_back(flowAddr);
    WCacheSlicePtr wcacheSlice = MakeRef<WCacheSlice>(G_PT_ID, 0, 1, NO_1024, addrs);

    auto ret = wcacheSlice->Serialize(nullptr, 0, length);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = wcacheSlice->Deserialize(nullptr, length);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    char* sliceBuf = static_cast<char *>(malloc(length));
    ret = wcacheSlice->Deserialize(sliceBuf, 0);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    free(sliceBuf);
    free(buffer);
}

TEST_F(TestRCache, test_cache_recover_err)
{
    LOG_INFO("test_cache_recover_err");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "CACHE_RECOVER_TYPE_FAIL", 0, NO_24, userParam);
    BioHvsActiveTracePoint(0, "CACHE_RECOVER_TYPE_INNER_FAIL", 0, NO_24, userParam);
    BioHvsActiveTracePoint(0, "NO_PROCESS_CACHE_RECOVER", 0, NO_24, userParam);
    auto ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, BIO_OK);

    BioHvsActiveTracePoint(0, "CACHE_RECOVER_CACHE_FAIL", 0, 1, userParam);
    ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "CACHE_RECOVER_TYPE_FAIL");
    BioHvsDeactiveTracePoint(0, "CACHE_RECOVER_TYPE_INNER_FAIL");
    BioHvsDeactiveTracePoint(0, "NO_PROCESS_CACHE_RECOVER");
    BioHvsDeactiveTracePoint(0, "CACHE_RECOVER_CACHE_FAIL");

    BioHvsActiveTracePoint(0, "RECOVER_CACHE_FLOWID_FAIL", 0, 1, userParam);
    ret = Cache::Instance().Recover();
    EXPECT_EQ(ret, BIO_INNER_ERR);
    BioHvsDeactiveTracePoint(0, "RECOVER_CACHE_FLOWID_FAIL");
}

TEST_F(TestRCache, test_cache_slice_operator_ok)
{
    LOG_INFO("test_cache_slice_operator_ok");
    CacheSliceOperator cacheSliceOperator;
    auto ret = cacheSliceOperator.Copy(nullptr, 0, NO_1024, nullptr);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    // disk slice
    char *buffer = static_cast<char*>(malloc(NO_1024));
    FlowAddr flowAddr;
    flowAddr.chunkId = reinterpret_cast<uint64_t>(buffer);
    flowAddr.chunkOffset = 0;
    flowAddr.chunkLen = NO_1024;
    std::vector<FlowAddr> addrVec;
    addrVec.push_back(flowAddr);
    SlicePtr slice = MakeRef<Slice>(NO_1024, addrVec, FLOW_DISK);

    // memory slice
    char *buffer1 = static_cast<char*>(malloc(NO_1024));
    FlowAddr flowAddr1;
    flowAddr1.chunkId = reinterpret_cast<uint64_t>(buffer1);
    flowAddr1.chunkOffset = 0;
    flowAddr1.chunkLen = NO_1024;
    std::vector<FlowAddr> addrVec1;
    addrVec1.push_back(flowAddr1);
    SlicePtr slice1 = MakeRef<Slice>(NO_1024, addrVec1, FLOW_MEMORY);

    // memory slice
    char *buffer2 = static_cast<char*>(malloc(NO_1024));
    FlowAddr flowAddr2;
    flowAddr2.chunkId = reinterpret_cast<uint64_t>(buffer1);
    flowAddr2.chunkOffset = 0;
    flowAddr2.chunkLen = NO_1024;
    std::vector<FlowAddr> addrVec2;
    addrVec2.push_back(flowAddr2);
    SlicePtr slice2 = MakeRef<Slice>(NO_1024, addrVec2, FLOW_MEMORY);

    std::string sliceStr(NO_2048, '1');
    std::string sliceStr1(NO_1024, '1');

    ret = cacheSliceOperator.Copy(nullptr, 0, NO_1024, slice);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    WCacheSlicePtr slicePtr = nullptr;
    ret = gRCacheManager->AllocResources(G_PT_ID, NO_1024, slicePtr);
    EXPECT_EQ(ret, BIO_OK);
    ret = cacheSliceOperator.Copy(sliceStr.c_str(), 0, NO_1024, slice);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = cacheSliceOperator.Copy(sliceStr1.c_str(), 0, NO_1024, slice1);
    EXPECT_EQ(ret, BIO_OK);

    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "SLICE_OPERATOR_4_FLOW_MEMORY", 0, 1, userParam);
    ret = cacheSliceOperator.Copy(sliceStr.c_str(), 0, NO_1024, slice1);
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "SLICE_OPERATOR_4_FLOW_MEMORY");

    ret = cacheSliceOperator.Copy(slicePtr.Get(), slicePtr.Get());
    EXPECT_EQ(ret, BIO_OK);

    SlicePtr slicePtr1 = nullptr;
    ret = cacheSliceOperator.Copy(slicePtr1, slice);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    ret = cacheSliceOperator.Copy(slice, slicePtr1);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    ret = cacheSliceOperator.Copy(slice, slice1);
    EXPECT_EQ(ret, BIO_DISK_IOERR);

    ret = cacheSliceOperator.Copy(slice, slice);
    EXPECT_EQ(ret, BIO_OK);
    BioHvsActiveTracePoint(0, "SLICE_COPY_DISK2MEMORY_OK", 0, 1, userParam);
    ret = cacheSliceOperator.Copy(slice, slice);
    EXPECT_EQ(ret, BIO_OK);
    BioHvsDeactiveTracePoint(0, "SLICE_COPY_DISK2MEMORY_OK");

    ret = cacheSliceOperator.Copy(slice1, slice);
    EXPECT_EQ(ret, BIO_DISK_IOERR);

    ret = cacheSliceOperator.Copy(nullptr, slice);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ret = cacheSliceOperator.Copy(sliceStr.c_str(), slicePtr1);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    ret = cacheSliceOperator.Copy(sliceStr.c_str(), slice1);
    EXPECT_EQ(ret, BIO_OK);
    BioHvsActiveTracePoint(0, "SLICE_OPERATOR_FLOW_MEMORY", 0, 1, userParam);
    ret = cacheSliceOperator.Copy(sliceStr.c_str(), slice1);
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "SLICE_OPERATOR_FLOW_MEMORY");
    ret = cacheSliceOperator.Copy(sliceStr.c_str(), slice);
    EXPECT_EQ(ret, BIO_DISK_IOERR);

    auto *value = new char[NO_1024];
    ret = cacheSliceOperator.Copy(nullptr, value, NO_1024);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    ret = cacheSliceOperator.Copy(slice, nullptr);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);
    BioHvsActiveTracePoint(0, "SLICE_OPERATOR_2_FLOW_MEMORY", 0, 1, userParam);
    ret = cacheSliceOperator.Copy(slice1, value, NO_1024);
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "SLICE_OPERATOR_2_FLOW_MEMORY");

    delete[] value;
    free(buffer);
    free(buffer1);
    free(buffer2);
}

TEST_F(TestRCache, test_expired_clear_return_ok)
{
    LOG_INFO("test_expired_clear_return_ok");
    auto ret = Cache::Instance().ExpiredClear(G_PT_ID, G_PT_V);
    EXPECT_EQ(ret, BIO_OK);
    ret = gRCacheManager->ExpiredClear(G_PT_ID, G_PT_V);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_rcache_destroy_ok)
{
    LOG_INFO("test_rcache_destroy_ok");
    auto ret = Cache::Instance().DestroyRCache(G_PT_ID);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestRCache, test_rcache_recover_return_err)
{
    LOG_INFO("test_rcache_recover_return_err");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "FLOW_SEAL_ERR", 0, 1, userParam);
    FlowPtr flowPtr;
    auto ret = gRCacheManager->RecoverCache(flowPtr);
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "FLOW_SEAL_ERR");
}

TEST_F(TestRCache, test_rcache_recover_return_err2)
{
    LOG_INFO("test_rcache_recover_return_err2");
    BioTracepointParam userParam;
    BioHvsActiveTracePoint(0, "FLOW_SEAL_ERR", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "FLOW_SEAL_OK", 0, 1, userParam);
    BioHvsActiveTracePoint(0, "FLOW_DESTROY_OBJECT_ERR", 0, 1, userParam);
    FlowPtr flowPtr = MakeRef<Flow>(FLOW_META, FLOW_MEMORY, 0, 0, 0, 0);
    auto ret = gRCacheManager->RecoverCache(flowPtr);
    EXPECT_EQ(ret, BIO_ERR);
    BioHvsDeactiveTracePoint(0, "FLOW_SEAL_ERR");
    BioHvsDeactiveTracePoint(0, "FLOW_SEAL_OK");
    BioHvsDeactiveTracePoint(0, "FLOW_DESTROY_OBJECT_ERR");
}

