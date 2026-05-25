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

#include "test_server.h"
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "mms_log.h"
#include "mms_conv.h"
#include "mms_types.h"
#include "mms_err.h"
#include "mms_message.h"
#include "mms_crb_scheduler.h"
#include "mms_server.h"
#include "mms_mock.h"

using namespace ock::mms;

bool TestServer::gSetup = false;
std::map<uint16_t, CmNodeInfo> fakeNodes;
std::map<uint16_t, CmPtInfo> fakePts;
std::unordered_map<std::string, std::string> fakeKvMap;
MmsNodeId g_MasterId = 0;
bool needSleep = false;

namespace {
std::mutex gNotifyMutex;
std::condition_variable gNotifyCv;
std::vector<std::pair<std::string, OperateType>> gNotifyEvents;

void ClearNotifyEvents()
{
    std::lock_guard<std::mutex> lock(gNotifyMutex);
    gNotifyEvents.clear();
}

void NotifyTestCallback(const char *key, OperateType opType)
{
    std::lock_guard<std::mutex> lock(gNotifyMutex);
    gNotifyEvents.emplace_back(key, opType);
    gNotifyCv.notify_all();
}

bool WaitForNotifyCount(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
{
    std::unique_lock<std::mutex> lock(gNotifyMutex);
    return gNotifyCv.wait_for(lock, timeout, [count] {
        return gNotifyEvents.size() >= count;
    });
}

std::vector<std::pair<std::string, OperateType>> GetNotifyEvents()
{
    std::lock_guard<std::mutex> lock(gNotifyMutex);
    return gNotifyEvents;
}

int32_t gItemResults[64] = {0};
char *gValueAddrs[64] = {nullptr};
char *gGetValues[64] = {nullptr};
uint32_t gRealLengths[64] = {0};

void ResetItemState()
{
    for (uint32_t index = 0; index < NO_64; index++) {
        gItemResults[index] = RET_MMS_OK;
        gValueAddrs[index] = nullptr;
        gGetValues[index] = nullptr;
        gRealLengths[index] = 0;
    }
}

PutItems MakePut(const char *key, const char *value, uint64_t valueLen, uint32_t index = 0, uint16_t isNotify = 0)
{
    return {key, value, static_cast<uint32_t>(valueLen), static_cast<uint16_t>(strlen(key)), isNotify,
            &gValueAddrs[index], &gItemResults[index]};
}

GetItems MakeGet(const char *key, uint32_t offset, uint64_t length, char *value, uint32_t index = 0)
{
    gGetValues[index] = value;
    gRealLengths[index] = 0;
    return {key, static_cast<uint16_t>(strlen(key)), offset, static_cast<uint32_t>(length), &gGetValues[index],
            &gRealLengths[index], &gItemResults[index]};
}

UpdateItems MakeUpdate(const char *key, const char *value, uint32_t offset, uint64_t valueLen, uint32_t index = 0)
{
    return {key, value, static_cast<uint16_t>(strlen(key)), static_cast<uint32_t>(valueLen), offset,
            &gItemResults[index]};
}

DeleteItems MakeDelete(const char *key, uint32_t index = 0, uint16_t isNotify = 0)
{
    return {key, static_cast<uint16_t>(strlen(key)), isNotify, &gItemResults[index]};
}

ReplaceItems MakeReplace(const char *key, const char *value, uint32_t offset, uint64_t valueLen, uint32_t index = 0)
{
    return {key, value, static_cast<uint16_t>(strlen(key)), static_cast<uint32_t>(valueLen), offset,
            &gItemResults[index]};
}
}

void TestServer::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;

    return;
}

void TestServer::TearDown()
{
    return;
}

TEST_F(TestServer, test_mms_put_and_get)
{
    LOG_INFO("test_mms_put");
    auto key = "key";
    auto value = "value";
    uint64_t length = strlen(value);
    uint32_t itemNum = NO_1;
    auto offset = NO_0;

    PutItems item = MakePut(key, value, length);
    auto ret = MmsConv::Put(&item, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue = new char[length + 1]();
    GetItems getItem = MakeGet(key, offset, length, getValue);
    ret = MmsConv::Get(&getItem, itemNum);
    EXPECT_STREQ(getValue, "value");
    EXPECT_EQ(ret, MMS_OK);
    delete[] getValue;
}

TEST_F(TestServer, test_mms_put_returns_value_addr)
{
    auto key = "put_value_addr_key";
    auto value = "put_value_addr_value";
    uint64_t length = strlen(value);

    PutItems item = MakePut(key, value, length);
    auto ret = MmsConv::Put(&item, NO_1);
    EXPECT_EQ(ret, RET_MMS_OK);
    EXPECT_EQ(gItemResults[NO_0], RET_MMS_OK);
    ASSERT_NE(gValueAddrs[NO_0], nullptr);
    EXPECT_EQ(memcmp(gValueAddrs[NO_0], value, length), 0);
}

TEST_F(TestServer, test_mms_get_zero_copy)
{
    auto key = "get_zero_copy_key";
    auto value = "get_zero_copy_value";
    uint64_t length = strlen(value);

    PutItems putItem = MakePut(key, value, length);
    auto ret = MmsConv::Put(&putItem, NO_1);
    EXPECT_EQ(ret, RET_MMS_OK);
    ASSERT_NE(gValueAddrs[NO_0], nullptr);

    GetItems getItem = MakeGet(key, NO_0, length, nullptr);
    ret = MmsConv::Get(&getItem, NO_1);
    EXPECT_EQ(ret, RET_MMS_OK);
    EXPECT_EQ(gItemResults[NO_0], RET_MMS_OK);
    EXPECT_EQ(gRealLengths[NO_0], static_cast<uint32_t>(length));
    ASSERT_NE(gGetValues[NO_0], nullptr);
    EXPECT_EQ(gGetValues[NO_0], gValueAddrs[NO_0]);
    EXPECT_EQ(memcmp(gGetValues[NO_0], value, length), 0);
}

TEST_F(TestServer, test_mms_batch_put_and_batch_get)
{
    LOG_INFO("test_mms_batch_put");
    const char* key1 = "key1";
    const char* value1 = "value1";
    const char* key2 = "key2";
    const char* value2 = "value2";
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);

    PutItems items[2];
    items[0] = MakePut(key1, value1, length1, 0);
    items[1] = MakePut(key2, value2, length2, 1);
    uint32_t itemNum = NO_2;
    auto ret = MmsConv::Put(items, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    GetItems getItems[itemNum];
    auto getValue1 = new char[length1 + 1]();
    auto getValue2 = new char[length2 + 1]();

    getItems[0] = MakeGet(key1, NO_0, length1, getValue1, 0);
    getItems[1] = MakeGet(key2, NO_0, length2, getValue2, 1);

    ret = MmsConv::Get(getItems, itemNum);
    EXPECT_STREQ(getValue1, "value1");
    EXPECT_STREQ(getValue2, "value2");
    delete[] getValue1;
    delete[] getValue2;
}

TEST_F(TestServer, test_mms_update)
{
    LOG_INFO("test_mms_update");
    auto key = "key";
    auto value = "value";
    auto offset = NO_0;
    uint64_t length = strlen(value);
    uint32_t itemNum = NO_1;

    PutItems putItem = MakePut(key, value, length);
    auto ret = MmsConv::Put(&putItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto updatedValue = "eulav";

    UpdateItems updateItem = MakeUpdate(key, updatedValue, offset, length);
    ret = MmsConv::Update(&updateItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue = new char[length + 1]();
    GetItems getItem = MakeGet(key, offset, length, getValue);
    ret = MmsConv::Get(&getItem, itemNum);
    EXPECT_STREQ(getValue, updatedValue);
    EXPECT_EQ(ret, MMS_OK);
    delete[] getValue;
}

TEST_F(TestServer, test_mms_batch_update)
{
    LOG_INFO("test_mms_batch_update");
    const char* key1 = "key1";
    const char* value1 = "value1";
    const char* key2 = "key2";
    const char* value2 = "value2";
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);
    auto offset = NO_0;

    PutItems putItems[2];
    putItems[0] = MakePut(key1, value1, length1, 0);
    putItems[1] = MakePut(key2, value2, length2, 1);

    uint32_t itemNum = 2;
    auto ret = MmsConv::Put(putItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto updatedValue1 = "value3";
    auto updatedValue2 = "value4";

    UpdateItems updateItems[2];
    updateItems[0] = MakeUpdate(key1, updatedValue1, offset, length1, 0);
    updateItems[1] = MakeUpdate(key2, updatedValue2, offset, length2, 1);
    ret = MmsConv::Update(updateItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    GetItems getItems[itemNum];
    auto getValue1 = new char[length1 + 1]();
    auto getValue2 = new char[length2 + 1]();

    getItems[0] = MakeGet(key1, NO_0, length1, getValue1, 0);
    getItems[1] = MakeGet(key2, NO_0, length2, getValue2, 1);

    ret = MmsConv::Get(getItems, itemNum);
    EXPECT_STREQ(getValue1, updatedValue1);
    EXPECT_STREQ(getValue2, updatedValue2);
    delete[] getValue1;
    delete[] getValue2;

    DeleteItems deleteItem[2] = {MakeDelete(key1, 0), MakeDelete(key2, 1)};
    ret = MmsConv::Delete(deleteItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);
}

TEST_F(TestServer, test_mms_delete)
{
    LOG_INFO("test_mms_delete");
    auto key = "key3";
    auto value = "value";
    auto offset = NO_0;
    uint64_t length = strlen(value);
    uint32_t itemNum = NO_1;

    PutItems putItem = MakePut(key, value, length);
    auto ret = MmsConv::Put(&putItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    DeleteItems deleteItem = MakeDelete(key);
    ret = MmsConv::Delete(&deleteItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue = new char[length + 1]();
    GetItems getItem = MakeGet(key, offset, length, getValue);
    ret = MmsConv::Get(&getItem, itemNum);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);
    delete[] getValue;
}

TEST_F(TestServer, test_mms_notify_put_and_delete)
{
    const char *key1 = "notify_key_001";
    const char *key2 = "notify_key_002";
    const char *missKey = "notify_key_missing";
    const char *value1 = "notify_value_001";
    const char *value2 = "notify_value_002";
    PutItems putItems[] = {
        MakePut(key1, value1, strlen(value1), 0, 1),
        MakePut(key2, value2, strlen(value2), 1, 1),
    };

    ClearNotifyEvents();
    EXPECT_EQ(MmsConv::RegisterCallback(NotifyTestCallback), RET_MMS_OK);

    auto ret = MmsConv::Put(putItems, NO_2);
    EXPECT_EQ(ret, RET_MMS_OK);
    EXPECT_FALSE(WaitForNotifyCount(NO_1, std::chrono::milliseconds(200)));
    auto events = GetNotifyEvents();
    EXPECT_TRUE(events.empty());

    ret = MmsConv::Put(putItems, NO_1);
    EXPECT_EQ(ret, RET_MMS_OK);
    EXPECT_FALSE(WaitForNotifyCount(NO_3, std::chrono::milliseconds(200)));

    DeleteItems deleteItems[] = {
        MakeDelete(key1, 0, 1),
        MakeDelete(missKey, 1, 1),
    };
    ret = MmsConv::Delete(deleteItems, NO_2);
    EXPECT_EQ(ret, RET_MMS_OK);
    EXPECT_FALSE(WaitForNotifyCount(NO_1, std::chrono::milliseconds(200)));
    events = GetNotifyEvents();
    EXPECT_TRUE(events.empty());

    EXPECT_EQ(MmsConv::RegisterCallback(nullptr), RET_MMS_EPERM);
    ClearNotifyEvents();
    DeleteItems deleteItem = MakeDelete(key2);
    ret = MmsConv::Delete(&deleteItem, NO_1);
    EXPECT_EQ(ret, RET_MMS_OK);
    EXPECT_FALSE(WaitForNotifyCount(NO_1, std::chrono::milliseconds(200)));
}

TEST_F(TestServer, test_mms_replace)
{
    LOG_INFO("test_mms_replace");
    auto key = "key3";
    auto value = "value";
    uint64_t length = strlen(value);
    uint32_t offset = 0;
    uint32_t itemNum = NO_1;

    ReplaceItems replaceItem = MakeReplace(key, value, offset, length);
    auto ret = MmsConv::Replace(&replaceItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    DeleteItems deleteItem = MakeDelete(key);
    ret = MmsConv::Delete(&deleteItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue = new char[length + 1]();
    GetItems getItem = MakeGet(key, offset, length, getValue);
    ret = MmsConv::Get(&getItem, itemNum);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);
    delete[] getValue;
}

TEST_F(TestServer, test_mms_batch_replace)
{
    LOG_INFO("test_mms_batch_replace");
    const char* key1 = "key1";
    const char* value1 = "value1";
    const char* key2 = "key2";
    const char* value2 = "value2";
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);
    auto offset = NO_0;

    PutItems putItems[2];
    putItems[0] = MakePut(key1, value1, length1, 0);
    putItems[1] = MakePut(key2, value2, length2, 1);

    uint32_t itemNum = 2;
    auto ret = MmsConv::Put(putItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto replaceValue1 = "value3";
    auto replaceValue2 = "value4";

    ReplaceItems replaceItems[2];
    replaceItems[0] = MakeReplace(key1, replaceValue1, offset, length1, 0);
    replaceItems[1] = MakeReplace(key2, replaceValue2, offset, length2, 1);
    ret = MmsConv::Replace(replaceItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    GetItems getItems[itemNum];
    auto getValue1 = new char[length1 + 1]();
    auto getValue2 = new char[length2 + 1]();

    getItems[0] = MakeGet(key1, NO_0, length1, getValue1, 0);
    getItems[1] = MakeGet(key2, NO_0, length2, getValue2, 1);

    ret = MmsConv::Get(getItems, itemNum);
    EXPECT_STREQ(getValue1, replaceValue1);
    EXPECT_STREQ(getValue2, replaceValue2);
    delete[] getValue1;
    delete[] getValue2;
}

TEST_F(TestServer, test_mms_batch_replace_and_batch_get)
{
    LOG_INFO("test_mms_batch_replace_and_batch_get");
    const char* key1 = "key1";
    const char* value1 = "value1";
    const char* key2 = "key2";
    const char* value2 = "value2";
    uint32_t offset1 = 0;
    uint32_t offset2 = 0;
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);

    ReplaceItems items[2];
    items[0] = MakeReplace(key1, value1, offset1, length1, 0);
    items[1] = MakeReplace(key2, value2, offset2, length2, 1);

    uint32_t itemNum = NO_2;
    auto ret = MmsConv::Replace(items, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    GetItems getItems[itemNum];
    auto getValue1 = new char[length1 + 1]();
    auto getValue2 = new char[length2 + 1]();

    getItems[0] = MakeGet(key1, NO_0, length1, getValue1, 0);
    getItems[1] = MakeGet(key2, NO_0, length2, getValue2, 1);

    ret = MmsConv::Get(getItems, itemNum);
    EXPECT_STREQ(getValue1, "value1");
    EXPECT_STREQ(getValue2, "value2");
    delete[] getValue1;
    delete[] getValue2;
}

TEST_F(TestServer, test_mms_batch_delete)
{
    LOG_INFO("test_mms_batch_delete");
    const char* key1 = "test_mms_batch_delete_key1";
    const char* value1 = "test_mms_batch_delete_value1";
    const char* key2 = "test_mms_batch_delete_key2";
    const char* value2 = "test_mms_batch_delete_value2";
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);
    auto offset = NO_0;

    PutItems putItems[2];
    putItems[0] = MakePut(key1, value1, length1, 0);
    putItems[1] = MakePut(key2, value2, length2, 1);

    uint32_t itemNum = NO_2;
    auto ret = MmsConv::Put(putItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    DeleteItems deleteItems[] = {
        MakeDelete(key1, 0),
        MakeDelete(key2, 1)
    };
    ret = MmsConv::Delete(deleteItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue1 = new char[length1 + 1]();
    GetItems getItem1 = MakeGet(key1, offset, length1, getValue1);
    ret = MmsConv::Get(&getItem1, NO_1);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);

    auto getValue2 = new char[length2 + 1]();
    GetItems getItem2 = MakeGet(key2, offset, length2, getValue2);
    ret = MmsConv::Get(&getItem2, NO_1);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);
    
    delete[] getValue1;
    delete[] getValue2;
}

TEST_F(TestServer, test_mms_batch_put_partial_result)
{
    ResetItemState();
    const char *conflictKey = "partial_put_conflict_key";
    const char *successKey = "partial_put_success_key";
    const char *baseValue = "partial_put_base_value";
    const char *conflictValue = "partial_put_diff_value";
    const char *successValue = "partial_put_success_value";
    PutItems baseItem = MakePut(conflictKey, baseValue, strlen(baseValue));
    EXPECT_EQ(MmsConv::Put(&baseItem, NO_1), RET_MMS_OK);

    PutItems putItems[] = {
        MakePut(conflictKey, conflictValue, strlen(conflictValue), 0),
        MakePut(successKey, successValue, strlen(successValue), 1),
        MakePut(conflictKey, conflictValue, strlen(conflictValue), 2),
    };
    auto ret = MmsConv::Put(putItems, NO_3);
    EXPECT_EQ(ret, RET_MMS_ERROR);
    EXPECT_EQ(gItemResults[0], RET_MMS_ERROR);
    EXPECT_EQ(gItemResults[1], RET_MMS_OK);
    EXPECT_EQ(gItemResults[2], RET_MMS_ERROR);
    ASSERT_NE(gValueAddrs[1], nullptr);
    EXPECT_EQ(memcmp(gValueAddrs[1], successValue, strlen(successValue)), 0);
}

TEST_F(TestServer, test_mms_batch_get_partial_result)
{
    ResetItemState();
    const char *key1 = "partial_get_key_001";
    const char *key2 = "partial_get_key_002";
    const char *missingKey = "partial_get_missing_key";
    const char *value1 = "partial_get_value_001";
    const char *value2 = "partial_get_value_002";
    PutItems putItems[] = {
        MakePut(key1, value1, strlen(value1), 0),
        MakePut(key2, value2, strlen(value2), 1),
    };
    EXPECT_EQ(MmsConv::Put(putItems, NO_2), RET_MMS_OK);

    char getValue1[64] = {};
    char getValue2[64] = {};
    char missingValue[64] = {};
    GetItems getItems[] = {
        MakeGet(key1, NO_0, strlen(value1), getValue1, 0),
        MakeGet(missingKey, NO_0, sizeof(missingValue), missingValue, 1),
        MakeGet(key2, NO_0, strlen(value2), getValue2, 2),
    };
    auto ret = MmsConv::Get(getItems, NO_3);
    EXPECT_EQ(ret, RET_MMS_NOT_FOUND);
    EXPECT_EQ(gItemResults[0], RET_MMS_OK);
    EXPECT_EQ(gItemResults[1], RET_MMS_NOT_FOUND);
    EXPECT_EQ(gItemResults[2], RET_MMS_OK);
    EXPECT_STREQ(getValue1, value1);
    EXPECT_STREQ(getValue2, value2);
}

TEST_F(TestServer, test_mms_batch_update_partial_result)
{
    ResetItemState();
    const char *key1 = "partial_update_key_001";
    const char *key2 = "partial_update_key_002";
    const char *missingKey = "partial_update_missing_key";
    const char *value = "partial_update_value";
    const char *updatedValue1 = "partial_update_new_1";
    const char *updatedValue2 = "partial_update_new_2";
    PutItems putItems[] = {
        MakePut(key1, value, strlen(value), 0),
        MakePut(key2, value, strlen(value), 1),
    };
    EXPECT_EQ(MmsConv::Put(putItems, NO_2), RET_MMS_OK);

    UpdateItems updateItems[] = {
        MakeUpdate(key1, updatedValue1, NO_0, strlen(updatedValue1), 0),
        MakeUpdate(missingKey, updatedValue1, NO_0, strlen(updatedValue1), 1),
        MakeUpdate(key2, updatedValue2, NO_0, strlen(updatedValue2), 2),
    };
    auto ret = MmsConv::Update(updateItems, NO_3);
    EXPECT_EQ(ret, RET_MMS_NOT_FOUND);
    EXPECT_EQ(gItemResults[0], RET_MMS_OK);
    EXPECT_EQ(gItemResults[1], RET_MMS_NOT_FOUND);
    EXPECT_EQ(gItemResults[2], RET_MMS_OK);

    char getValue[64] = {};
    GetItems getItem = MakeGet(key2, NO_0, strlen(updatedValue2), getValue);
    EXPECT_EQ(MmsConv::Get(&getItem, NO_1), RET_MMS_OK);
    EXPECT_STREQ(getValue, updatedValue2);
}

TEST_F(TestServer, test_mms_batch_replace_partial_result)
{
    ResetItemState();
    const char *failKey = "partial_replace_fail_key";
    const char *successKey = "partial_replace_success_key";
    const char *value = "replace_value";
    const char *failValue = "replace_fail";
    const char *successValue = "replace_success";
    PutItems baseItem = MakePut(failKey, value, strlen(value));
    EXPECT_EQ(MmsConv::Put(&baseItem, NO_1), RET_MMS_OK);

    uint32_t invalidOffset = static_cast<uint32_t>(strlen(value) + NO_1);
    ReplaceItems replaceItems[] = {
        MakeReplace(failKey, failValue, invalidOffset, strlen(failValue), 0),
        MakeReplace(successKey, successValue, NO_0, strlen(successValue), 1),
    };
    auto ret = MmsConv::Replace(replaceItems, NO_2);
    EXPECT_EQ(ret, RET_MMS_ERROR);
    EXPECT_EQ(gItemResults[0], RET_MMS_ERROR);
    EXPECT_EQ(gItemResults[1], RET_MMS_OK);

    char getValue[64] = {};
    GetItems getItem = MakeGet(successKey, NO_0, strlen(successValue), getValue);
    EXPECT_EQ(MmsConv::Get(&getItem, NO_1), RET_MMS_OK);
    EXPECT_STREQ(getValue, successValue);
}

TEST_F(TestServer, test_mms_crb_delete)
{
    LOG_INFO("test_mms_crb_delete");
    std::string key1 = "test_cache_key1";
    std::string key2 = "test_cache_key2";
    std::string value1 = "test_cache_value";

    PutItems putItems = MakePut(key1.c_str(), value1.c_str(), value1.size());
    uint32_t itemNum = NO_1;
    auto ret = MmsConv::Put(&putItems, itemNum);
    EXPECT_EQ(ret, RET_MMS_OK);

    MmsServer::Instance()->GetCache()->SetRecoverStatus(true);

    BResult delRet = MmsServer::Instance()->GetCache()->Delete(key1.c_str(), static_cast<uint16_t>(key1.size()), NO_2);
    EXPECT_EQ(delRet, MMS_OK);

    delRet = MmsServer::Instance()->GetCache()->Delete(key2.c_str(), static_cast<uint16_t>(key2.size()), NO_2);
    EXPECT_EQ(delRet, MMS_KEY_NOT_EXISTS);

    MmsServer::Instance()->GetCache()->ClearDeletedData();
    MmsServer::Instance()->GetCache()->SetRecoverStatus(false);

    auto getValue1 = new char[value1.size()];
    GetItems getItem1 = MakeGet(key1.c_str(), 0, value1.size(), getValue1);
    ret = MmsConv::Get(&getItem1, NO_1);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);

    delete[] getValue1;
}

TEST_F(TestServer, test_mms_prefix_range_and_range_delete)
{
    MmsServer::Instance()->GetCache()->SetArtSwitch(true);
    const char *key1 = "range_key_001";
    const char *key2 = "range_key_002";
    const char *key3 = "range_key_003";
    const char *value1 = "range_value_001";
    const char *value2 = "range_value_002";
    const char *value3 = "range_value_003";
    PutItems putItems[] = {
        MakePut(key1, value1, strlen(value1), 0),
        MakePut(key2, value2, strlen(value2), 1),
        MakePut(key3, value3, strlen(value3), 2),
    };

    auto ret = MmsConv::Put(putItems, NO_3);
    EXPECT_EQ(ret, RET_MMS_OK);

    ValueInfo *values = nullptr;
    uint64_t itemNum = 0;
    ret = MmsConv::GetValuesByPrefix("range_key_", &values, &itemNum);
    EXPECT_EQ(ret, RET_MMS_OK);
    EXPECT_GE(itemNum, static_cast<uint64_t>(NO_3));
    MmsConv::FreeResources(&values, itemNum);
    EXPECT_EQ(values, nullptr);

    itemNum = 0;
    ret = MmsConv::GetValuesByRange("range_key_001", "range_key_003", &values, &itemNum);
    EXPECT_EQ(ret, RET_MMS_OK);
    EXPECT_GE(itemNum, static_cast<uint64_t>(NO_3));
    MmsConv::FreeResources(&values, itemNum);

    ret = MmsConv::BatchDeleteByRange("range_key_001", "range_key_003");
    EXPECT_EQ(ret, RET_MMS_OK);

    uint64_t realLength = strlen(value1);
    auto getValue = new char[realLength + 1]();
    GetItems getItem = MakeGet(key1, 0, realLength, getValue);
    ret = MmsConv::Get(&getItem, NO_1);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);
    delete[] getValue;
    MmsServer::Instance()->GetCache()->SetArtSwitch(false);
}

TEST_F(TestServer, test_mms_range_invalid_params)
{
    ValueInfo *values = nullptr;
    uint64_t itemNum = 0;

    EXPECT_EQ(MmsConv::GetValuesByPrefix(nullptr, &values, &itemNum), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::GetValuesByPrefix("", &values, &itemNum), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::GetValuesByPrefix("range", nullptr, &itemNum), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::GetValuesByPrefix("range", &values, nullptr), RET_MMS_EPERM);

    EXPECT_EQ(MmsConv::GetValuesByRange(nullptr, "z", &values, &itemNum), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::GetValuesByRange("a", nullptr, &values, &itemNum), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::GetValuesByRange("", "z", &values, &itemNum), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::GetValuesByRange("a", "", &values, &itemNum), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::GetValuesByRange("z", "a", &values, &itemNum), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::GetValuesByRange("a", "z", nullptr, &itemNum), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::GetValuesByRange("a", "z", &values, nullptr), RET_MMS_EPERM);

    EXPECT_EQ(MmsConv::BatchDeleteByRange(nullptr, "z"), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::BatchDeleteByRange("a", nullptr), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::BatchDeleteByRange("", "z"), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::BatchDeleteByRange("a", ""), RET_MMS_EPERM);
    EXPECT_EQ(MmsConv::BatchDeleteByRange("z", "a"), RET_MMS_EPERM);

    EXPECT_NO_THROW(MmsConv::FreeResources(nullptr, 0));
    EXPECT_NO_THROW(MmsConv::FreeResources(&values, 0));
    EXPECT_NO_THROW(MmsConv::FreeResources(&values, NO_1));
    EXPECT_EQ(values, nullptr);
}
