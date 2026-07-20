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
    uint64_t userId = NO_1;
    auto key = "key";
    auto value = "value";
    uint64_t length = strlen(value);
    uint32_t itemNum = NO_1;
    auto offset = NO_0;

    PutItems item = { const_cast<char *>(key), const_cast<char *>(value), length };
    auto ret = MmsConv::Put(userId, &item, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue = new char[length + 1]();
    GetItems getItem = { const_cast<char *>(key), offset, length, const_cast<char *>(getValue), &length};
    ret = MmsConv::Get(userId, &getItem, itemNum);
    EXPECT_STREQ(getValue, "value");
    EXPECT_EQ(ret, MMS_OK);
    delete[] getValue;
}

TEST_F(TestServer, test_mms_batch_put_and_batch_get)
{
    LOG_INFO("test_mms_batch_put");
    uint64_t userId = NO_2;
    const char* key1 = "key1";
    const char* value1 = "value1";
    const char* key2 = "key2";
    const char* value2 = "value2";
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);

    PutItems items[2];
    items[0] = {const_cast<char *>(key1), const_cast<char *>(value1), length1};
    items[1] = {const_cast<char *>(key2), const_cast<char *>(value2), length2};
    uint32_t itemNum = NO_2;
    auto ret = MmsConv::Put(userId, items, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    GetItems getItems[itemNum];
    auto getValue1 = new char[length1 + 1]();
    auto getValue2 = new char[length2 + 1]();

    getItems[0].key = const_cast<char *>("key1");
    getItems[0].value = const_cast<char *>(getValue1);
    getItems[0].offset = NO_0;
    getItems[0].length = length1;
    getItems[0].realLength = &length1;

    getItems[1].key = const_cast<char *>("key2");
    getItems[1].value = const_cast<char *>(getValue2);
    getItems[1].offset = NO_0;
    getItems[1].length = length2;
    getItems[1].realLength = &length2;

    ret = MmsConv::Get(userId, getItems, itemNum);
    EXPECT_STREQ(getValue1, "value1");
    EXPECT_STREQ(getValue2, "value2");
    delete[] getValue1;
    delete[] getValue2;
}

TEST_F(TestServer, test_mms_update)
{
    LOG_INFO("test_mms_update");
    uint64_t userId = NO_3;
    auto key = "key";
    auto value = "value";
    auto offset = NO_0;
    uint64_t length = strlen(value);
    uint32_t itemNum = NO_1;

    PutItems putItem = { const_cast<char *>(key), const_cast<char *>(value), length };
    auto ret = MmsConv::Put(userId, &putItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto updatedValue = "eulav";

    UpdateItems updateItem = { const_cast<char *>(key), const_cast<char *>(updatedValue), offset, length };
    ret = MmsConv::Update(userId, &updateItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue = new char[length + 1]();
    GetItems getItem = { const_cast<char *>(key), offset, length, const_cast<char *>(getValue), &length };
    ret = MmsConv::Get(userId, &getItem, itemNum);
    EXPECT_STREQ(getValue, updatedValue);
    EXPECT_EQ(ret, MMS_OK);
    delete[] getValue;
}

TEST_F(TestServer, test_mms_batch_update)
{
    LOG_INFO("test_mms_batch_update");
    uint64_t userId = NO_4;
    const char* key1 = "key1";
    const char* value1 = "value1";
    const char* key2 = "key2";
    const char* value2 = "value2";
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);
    auto offset = NO_0;

    PutItems putItems[2];
    putItems[0] = {const_cast<char *>(key1), const_cast<char *>(value1), length1};
    putItems[1] = {const_cast<char *>(key2), const_cast<char *>(value2), length2};

    uint32_t itemNum = 2;
    auto ret = MmsConv::Put(userId, putItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto updatedValue1 = "value3";
    auto updatedValue2 = "value4";

    UpdateItems updateItems[2];
    updateItems[0] =  {const_cast<char *>(key1), const_cast<char *>(updatedValue1), offset, length1};
    updateItems[1] = {const_cast<char *>(key2), const_cast<char *>(updatedValue2), offset, length2};
    ret = MmsConv::Update(userId, updateItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    GetItems getItems[itemNum];
    auto getValue1 = new char[length1 + 1]();
    auto getValue2 = new char[length2 + 1]();

    getItems[0].key = const_cast<char *>("key1");
    getItems[0].value = const_cast<char *>(getValue1);
    getItems[0].offset = NO_0;
    getItems[0].length = length1;
    getItems[0].realLength = &length1;

    getItems[1].key = const_cast<char *>("key2");
    getItems[1].value = const_cast<char *>(getValue2);
    getItems[1].offset = NO_0;
    getItems[1].length = length2;
    getItems[1].realLength = &length2;

    ret = MmsConv::Get(userId, getItems, itemNum);
    EXPECT_STREQ(getValue1, updatedValue1);
    EXPECT_STREQ(getValue2, updatedValue2);
    delete[] getValue1;
    delete[] getValue2;

    DeleteItems deleteItem[2] = {{ const_cast<char *>(key1) }, {const_cast<char *>(key2)}};
    ret = MmsConv::Delete(userId, deleteItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);
}

TEST_F(TestServer, test_mms_delete)
{
    LOG_INFO("test_mms_delete");

    uint64_t userId = NO_5;
    auto key = "key3";
    auto value = "value";
    auto offset = NO_0;
    uint64_t length = strlen(value);
    uint32_t itemNum = NO_1;

    PutItems putItem = { const_cast<char *>(key), const_cast<char *>(value), length };
    auto ret = MmsConv::Put(userId, &putItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    DeleteItems deleteItem = { const_cast<char *>(key) };
    ret = MmsConv::Delete(userId, &deleteItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue = new char[length + 1]();
    GetItems getItem = { const_cast<char *>(key), offset, length, const_cast<char *>(getValue), &length };
    ret = MmsConv::Get(userId, &getItem, itemNum);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);
    delete[] getValue;
}

TEST_F(TestServer, test_mms_replace)
{
    LOG_INFO("test_mms_replace");

    uint64_t userId = NO_5;
    auto key = "key3";
    auto value = "value";
    uint64_t length = strlen(value);
    uint32_t offset = 0;
    uint32_t itemNum = NO_1;

    ReplaceItems replaceItem = { const_cast<char *>(key), const_cast<char *>(value), offset, length };
    auto ret = MmsConv::Replace(userId, &replaceItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    DeleteItems deleteItem = { const_cast<char *>(key) };
    ret = MmsConv::Delete(userId, &deleteItem, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue = new char[length + 1]();
    GetItems getItem = { const_cast<char *>(key), offset, length, const_cast<char *>(getValue), &length };
    ret = MmsConv::Get(userId, &getItem, itemNum);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);
    delete[] getValue;
}

TEST_F(TestServer, test_mms_batch_replace)
{
    LOG_INFO("test_mms_batch_replace");
    uint64_t userId = NO_4;
    const char* key1 = "key1";
    const char* value1 = "value1";
    const char* key2 = "key2";
    const char* value2 = "value2";
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);
    auto offset = NO_0;

    PutItems putItems[2];
    putItems[0] = {const_cast<char *>(key1), const_cast<char *>(value1), length1};
    putItems[1] = {const_cast<char *>(key2), const_cast<char *>(value2), length2};

    uint32_t itemNum = 2;
    auto ret = MmsConv::Put(userId, putItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto replaceValue1 = "value3";
    auto replaceValue2 = "value4";

    ReplaceItems replaceItems[2];
    replaceItems[0] = {const_cast<char *>(key1), const_cast<char *>(replaceValue1), offset, length1};
    replaceItems[1] = {const_cast<char *>(key2), const_cast<char *>(replaceValue2), offset, length2};
    ret = MmsConv::Replace(userId, replaceItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    GetItems getItems[itemNum];
    auto getValue1 = new char[length1 + 1]();
    auto getValue2 = new char[length2 + 1]();

    getItems[0].key = const_cast<char *>("key1");
    getItems[0].value = const_cast<char *>(getValue1);
    getItems[0].offset = NO_0;
    getItems[0].length = length1;
    getItems[0].realLength = &length1;

    getItems[1].key = const_cast<char *>("key2");
    getItems[1].value = const_cast<char *>(getValue2);
    getItems[1].offset = NO_0;
    getItems[1].length = length2;
    getItems[1].realLength = &length2;

    ret = MmsConv::Get(userId, getItems, itemNum);
    EXPECT_STREQ(getValue1, replaceValue1);
    EXPECT_STREQ(getValue2, replaceValue2);
    delete[] getValue1;
    delete[] getValue2;
}

TEST_F(TestServer, test_mms_batch_replace_and_batch_get)
{
    LOG_INFO("test_mms_batch_replace_and_batch_get");
    uint64_t userId = NO_2;
    const char* key1 = "key1";
    const char* value1 = "value1";
    const char* key2 = "key2";
    const char* value2 = "value2";
    uint32_t offset1 = 0;
    uint32_t offset2 = 0;
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);

    ReplaceItems items[2];
    items[0] = {const_cast<char *>(key1), const_cast<char *>(value1), offset1, length1};
    items[1] = {const_cast<char *>(key2), const_cast<char *>(value2), offset2, length2};

    uint32_t itemNum = NO_2;
    auto ret = MmsConv::Replace(userId, items, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    GetItems getItems[itemNum];
    auto getValue1 = new char[length1 + 1]();
    auto getValue2 = new char[length2 + 1]();

    getItems[0].key = const_cast<char *>("key1");
    getItems[0].value = const_cast<char *>(getValue1);
    getItems[0].offset = NO_0;
    getItems[0].length = length1;
    getItems[0].realLength = &length1;

    getItems[1].key = const_cast<char *>("key2");
    getItems[1].value = const_cast<char *>(getValue2);
    getItems[1].offset = NO_0;
    getItems[1].length = length2;
    getItems[1].realLength = &length2;

    ret = MmsConv::Get(userId, getItems, itemNum);
    EXPECT_STREQ(getValue1, "value1");
    EXPECT_STREQ(getValue2, "value2");
    delete[] getValue1;
    delete[] getValue2;
}

TEST_F(TestServer, test_mms_batch_delete)
{
    LOG_INFO("test_mms_batch_delete");
    uint64_t userId = NO_6;
    const char* key1 = "test_mms_batch_delete_key1";
    const char* value1 = "test_mms_batch_delete_value1";
    const char* key2 = "test_mms_batch_delete_key2";
    const char* value2 = "test_mms_batch_delete_value2";
    uint64_t length1 = strlen(value1);
    uint64_t length2 = strlen(value2);
    auto offset = NO_0;

    PutItems putItems[2];
    putItems[0] = {const_cast<char *>(key1), const_cast<char *>(value1), length1};
    putItems[1] = {const_cast<char *>(key2), const_cast<char *>(value2), length2};

    uint32_t itemNum = NO_2;
    auto ret = MmsConv::Put(userId, putItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    DeleteItems deleteItems[] = {
        { const_cast<char *>(key1) },
        { const_cast<char *>(key2) }
    };
    ret = MmsConv::Delete(userId, deleteItems, itemNum);
    EXPECT_EQ(ret, MMS_OK);

    auto getValue1 = new char[length1 + 1]();
    GetItems getItem1 = {const_cast<char *>(key1), offset, length1, const_cast<char *>(getValue1), &length1};
    ret = MmsConv::Get(userId, &getItem1, NO_1);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);

    auto getValue2 = new char[length2 + 1]();
    GetItems getItem2 = {const_cast<char *>(key2), offset, length2, const_cast<char *>(getValue2), &length2};
    ret = MmsConv::Get(userId, &getItem2, NO_1);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);
    
    delete[] getValue1;
    delete[] getValue2;
}

TEST_F(TestServer, test_mms_crb_delete)
{
    LOG_INFO("test_mms_crb_delete");
    uint64_t userId = NO_6;
    std::string key1 = "test_cache_key1";
    std::string key2 = "test_cache_key2";
    std::string value1 = "test_cache_value";

    PutItems putItems = {const_cast<char *>(key1.c_str()), const_cast<char *>(value1.c_str()), value1.size(), NO_1};
    uint32_t itemNum = NO_1;
    auto ret = MmsConv::Put(userId, &putItems, itemNum);
    EXPECT_EQ(ret, RET_MMS_OK);

    MmsServer::Instance()->GetCache()->SetRecoverStatus(true);

    BResult delRet = MmsServer::Instance()->GetCache()->Delete(key1.c_str(), NO_2);
    EXPECT_EQ(delRet, MMS_OK);

    delRet = MmsServer::Instance()->GetCache()->Delete(key2.c_str(), NO_2);
    EXPECT_EQ(delRet, MMS_KEY_NOT_EXISTS);

    MmsServer::Instance()->GetCache()->ClearDeletedData();
    MmsServer::Instance()->GetCache()->SetRecoverStatus(false);

    uint64_t realLength = 0;
    auto getValue1 = new char[value1.size()];
    GetItems getItem1 = {const_cast<char *>(key1.c_str()), 0, value1.size(), const_cast<char *>(getValue1),
                         &realLength};
    ret = MmsConv::Get(userId, &getItem1, NO_1);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);

    delete[] getValue1;
}
