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

#include "test_mms.h"
#include "mms_c.h"
#include "mms_log.h"
#include "string"
#include "net_engine.h"
#include "mms_kv_server.h"

using namespace ock::mms;

bool TestMms::gSetup = false;
template <typename TReq, typename TResp>
union CatchUpSyncCallHelper {
    BResult (NetEngine::*SyncCall_stub)(const MmsNodeId &targetNodeId, uint32_t groupIndex, uint16_t opCode, TReq &req,
                                        TResp &resp) noexcept;

    BResult (*MockSyncCall_stub)(NetEngine *self, const MmsNodeId &targetNodeId, uint32_t groupIndex, uint16_t opCode,
                                 TReq &req, TResp &resp) noexcept;
};

void TestMms::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestMms::TearDown()
{
    return;
}
static MmsKvServerPtr gServer = MmsKvServer::Instance();
static int32_t gItemResults[16] = {0};
static char *gValueAddrs[16] = {nullptr};

static PutItems MakePut(const char *key, const char *value, uint32_t valueLen, uint32_t index = 0)
{
    return {key, value, valueLen, static_cast<uint16_t>(strlen(key)), 0, &gValueAddrs[index], &gItemResults[index]};
}

static GetItems MakeGet(const char *key, uint32_t offset, uint32_t length, char **value, uint32_t *realLength,
                        uint32_t index = 0)
{
    return {key, static_cast<uint16_t>(strlen(key)), offset, length, value, realLength, &gItemResults[index]};
}

static UpdateItems MakeUpdate(const char *key, const char *value, uint32_t offset, uint32_t valueLen,
                              uint32_t index = 0)
{
    return {key, value, static_cast<uint16_t>(strlen(key)), valueLen, offset, &gItemResults[index]};
}

static DeleteItems MakeDelete(const char *key, uint32_t index = 0)
{
    return {key, static_cast<uint16_t>(strlen(key)), 0, &gItemResults[index]};
}

static ReplaceItems MakeReplace(const char *key, const char *value, uint32_t offset, uint32_t valueLen,
                                uint32_t index = 0)
{
    return {key, value, static_cast<uint16_t>(strlen(key)), valueLen, offset, &gItemResults[index]};
}

TEST_F(TestMms, test_mms_put)
{
    LOG_INFO("test_mms_put");
    PutItems items[2];
    uint32_t length = 7;
    PutItems item1 = MakePut("aaa", "value1", length, 0);
    PutItems item2 = MakePut("bbb", "value2", length, 1);
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsPut(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_put_invalid_batch)
{
    LOG_INFO("test_mms_put_invalid_batch");
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsPut(nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_put_invalid_parameter)
{
    LOG_INFO("test_mms_put_invalid_parameter");
    gServer->NotifyServiceable(true);
    PutItems items[1];

    // invalid key
    uint32_t length = 7;
    items[0] = MakePut("", "value1", length, 0);
    uint32_t itemNum = 1;
    auto ret = MmsPut(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid value
    items[0] = MakePut("key1", nullptr, length, 0);
    ret = MmsPut(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid length
    length = 0;
    items[0] = MakePut("key1", "value1", length, 0);
    ret = MmsPut(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_get)
{
    LOG_INFO("test_mms_get");
    GetItems items[2];
    uint32_t length = 7;
    char *value1 = reinterpret_cast<char *>(malloc(length));
    char *value2 = reinterpret_cast<char *>(malloc(length));
    uint32_t realLen1 = 0;
    uint32_t realLen2 = 0;
    uint32_t offset = 0;
    GetItems item1 = MakeGet("aaa", offset, length, &value1, &realLen1, 0);
    GetItems item2 = MakeGet("bbb", offset, length, &value2, &realLen2, 1);
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsGet(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
    free(value1);
    free(value2);
}

TEST_F(TestMms, test_mms_get_invalid_batch)
{
    LOG_INFO("test_mms_get_invalid_batch");
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsPut(nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_get_invalid_parameter)
{
    LOG_INFO("test_mms_get_invalid_parameter");
    gServer->NotifyServiceable(true);
    GetItems items[1];

    // invalid key
    uint32_t length = 7;
    char *value1 = reinterpret_cast<char *>(malloc(length));
    uint32_t realLen1 = 0;
    uint32_t offset = 0;
    items[0] = MakeGet("", offset, length, &value1, &realLen1);
    uint32_t itemNum = 1;
    auto ret = MmsGet(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid value
    realLen1 = 0;
    offset = 0;
    length = 7;
    items[0] = MakeGet("aaa", offset, length, nullptr, &realLen1);
    ret = MmsGet(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid length
    realLen1 = 0;
    offset = 0;
    length = 0;
    items[0] = MakeGet("aaa", offset, length, &value1, &realLen1);
    ret = MmsGet(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_update)
{
    LOG_INFO("test_mms_update");
    UpdateItems items[2];
    uint32_t offset = 0;
    uint32_t length = 7;
    UpdateItems item1 = MakeUpdate("aaa", "value1", offset, length, 0);
    UpdateItems item2 = MakeUpdate("bbb", "value2", offset, length, 1);
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsUpdate(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_update_invalid_batch)
{
    LOG_INFO("test_mms_update_invalid_batch");
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsUpdate(nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_update_invalid_parameter)
{
    LOG_INFO("test_mms_update_invalid_parameter");
    gServer->NotifyServiceable(true);
    UpdateItems items[1];

    // invalid key
    uint32_t length = 7;
    uint32_t offset = 0;
    items[0] = MakeUpdate("", "value1", offset, length);
    uint32_t itemNum = 1;
    auto ret = MmsUpdate(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid value
    items[0] = MakeUpdate("key1", nullptr, offset, length);
    ret = MmsUpdate(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid length
    length = 0;
    items[0] = MakeUpdate("key1", "value1", offset, length);
    ret = MmsUpdate(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_delete)
{
    LOG_INFO("test_mms_delete");
    DeleteItems items[2];
    DeleteItems item1 = MakeDelete("aaa", 0);
    DeleteItems item2 = MakeDelete("bbb", 1);
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsDelete(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_delete_invalid_batch)
{
    LOG_INFO("test_mms_delete_invalid_batch");
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsDelete(nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_delete_invalid_parameter)
{
    LOG_INFO("test_mms_delete_invalid_parameter");
    gServer->NotifyServiceable(true);
    DeleteItems items[1];

    // invalid key
    items[0] = MakeDelete("");
    uint32_t itemNum = 1;
    auto ret = MmsDelete(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_replace_unavailable)
{
    LOG_INFO("test_mms_replace_unavailable");
    ReplaceItems items[2];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = MakeReplace("aaa", "value1", offset, length, 0);
    ReplaceItems item2 = MakeReplace("bbb", "value2", offset, length, 1);
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    gServer->NotifyServiceable(false);
    auto ret = MmsReplace(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_invalid_batch)
{
    LOG_INFO("test_mms_replace_invalid_batch");
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsReplace(nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_replace_invalid_parameter)
{
    LOG_INFO("test_mms_replace_invalid_parameter");
    gServer->NotifyServiceable(true);
    ReplaceItems items[1];

    // invalid key
    uint32_t length = 7;
    uint32_t offset = 0;
    items[0] = MakeReplace("", "value1", offset, length);
    uint32_t itemNum = 1;
    auto ret = MmsReplace(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid value
    items[0] = MakeReplace("key1", nullptr, offset, length);
    ret = MmsReplace(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid length
    offset = 1;
    length = 0;
    items[0] = MakeReplace("key1", "value1", offset, length);
    ret = MmsReplace(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_replace_with_single_key)
{
    LOG_INFO("test_mms_replace_with_single_key");
    ReplaceItems items[1];
    uint32_t length = 7;
    uint32_t offset = 0;
    items[0] = MakeReplace("aaa", "value1", offset, length);
    uint32_t itemNum = 1;

    gServer->NotifyServiceable(true);
    auto ret = MmsReplace(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_with_multi_key)
{
    LOG_INFO("test_mms_replace_with_multi_key");
    ReplaceItems items[2];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = MakeReplace("aaa", "value1", offset, length, 0);
    ReplaceItems item2 = MakeReplace("aaa", "value2", offset, length, 1);
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsReplace(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_old_key)
{
    LOG_INFO("test_mms_replace_old_key");
    ReplaceItems items[2];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = MakeReplace("aaa", "value1", offset, length, 0);
    ReplaceItems item2 = MakeReplace("aaa", "value2", offset, length, 1);
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsReplace(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_mixed)
{
    LOG_INFO("test_mms_replace_mixed");
    ReplaceItems items[3];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = MakeReplace("aaa", "value1", offset, length, 0);
    ReplaceItems item2 = MakeReplace("aaa", "value2", offset, length, 1);
    ReplaceItems item3 = MakeReplace("bbb", "value3", offset, length, 2);
    items[0] = item1;
    items[1] = item2;
    items[2] = item3;
    uint32_t itemNum = 3;
    auto ret = MmsReplace(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_repeat_key)
{
    LOG_INFO("test_mms_replace_repeat_key");
    ReplaceItems items[3];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = MakeReplace("aaa", "value1", offset, length, 0);
    ReplaceItems item3 = MakeReplace("bbb", "value3", offset, length, 2);
    ReplaceItems item2 = MakeReplace("aaa", "value1", offset, length, 1);
    items[0] = item1;
    items[1] = item2;
    items[2] = item3;
    uint32_t itemNum = 3;
    auto ret = MmsReplace(items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}
