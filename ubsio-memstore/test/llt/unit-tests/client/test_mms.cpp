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

TEST_F(TestMms, test_mms_put)
{
    LOG_INFO("test_mms_put");
    uint64_t userId = 1;
    PutItems items[2];
    uint32_t length = 7;
    PutItems item1 = {"aaa", "value1", length};
    PutItems item2 = {"bbb", "value2", length};
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsPut(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_put_invalid_batch)
{
    LOG_INFO("test_mms_put_invalid_batch");
    uint64_t userId = 1;
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsPut(userId, nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_put_invalid_parameter)
{
    LOG_INFO("test_mms_put_invalid_parameter");
    gServer->NotifyServiceable(true);
    uint64_t userId = 1;
    PutItems items[1];

    // invalid key
    uint32_t length = 7;
    items[0] = {"", "value1", length};
    uint32_t itemNum = 1;
    auto ret = MmsPut(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid value
    items[0] = {"key1", nullptr, length};
    ret = MmsPut(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid length
    length = 0;
    items[0] = {"key1", "value1", length};
    ret = MmsPut(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_get)
{
    LOG_INFO("test_mms_get");
    uint64_t userId = 1;
    GetItems items[2];
    uint32_t length = 7;
    char *value1 = reinterpret_cast<char *>(malloc(length));
    char *value2 = reinterpret_cast<char *>(malloc(length));
    uint64_t realLen1 = 0;
    uint64_t realLen2 = 0;
    uint32_t offset = 0;
    GetItems item1 = {"aaa", offset, length, value1, &realLen1};
    GetItems item2 = {"bbb", offset, length, value2, &realLen2};
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsGet(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
    free(value1);
    free(value2);
}

TEST_F(TestMms, test_mms_get_invalid_batch)
{
    LOG_INFO("test_mms_get_invalid_batch");
    uint64_t userId = 1;
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsPut(userId, nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_get_invalid_parameter)
{
    LOG_INFO("test_mms_get_invalid_parameter");
    gServer->NotifyServiceable(true);
    uint64_t userId = 1;
    GetItems items[1];

    // invalid key
    uint32_t length = 7;
    char *value1 = reinterpret_cast<char *>(malloc(length));
    uint64_t realLen1 = 0;
    uint32_t offset = 0;
    items[0] = {"", offset, length, value1, &realLen1};
    uint32_t itemNum = 1;
    auto ret = MmsGet(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid value
    realLen1 = 0;
    offset = 0;
    length = 7;
    items[0] = {"aaa", offset, length, nullptr, &realLen1};
    ret = MmsGet(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid length
    realLen1 = 0;
    offset = 0;
    length = 0;
    items[0] = {"aaa", offset, length, value1, &realLen1};
    ret = MmsGet(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_update)
{
    LOG_INFO("test_mms_update");
    uint64_t userId = 1;
    UpdateItems items[2];
    uint32_t offset = 0;
    uint32_t length = 7;
    UpdateItems item1 = {"aaa", "value1", offset, length};
    UpdateItems item2 = {"bbb", "value2", offset, length};
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsUpdate(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_update_invalid_batch)
{
    LOG_INFO("test_mms_update_invalid_batch");
    uint64_t userId = 1;
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsUpdate(userId, nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_update_invalid_parameter)
{
    LOG_INFO("test_mms_update_invalid_parameter");
    gServer->NotifyServiceable(true);
    uint64_t userId = 1;
    UpdateItems items[1];

    // invalid key
    uint32_t length = 7;
    uint32_t offset = 0;
    items[0] = {"", "value1", offset, length};
    uint32_t itemNum = 1;
    auto ret = MmsUpdate(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid value
    items[0] = {"key1", nullptr, offset, length};
    ret = MmsUpdate(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid length
    length = 0;
    items[0] = {"key1", "value1", offset, length};
    ret = MmsUpdate(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_delete)
{
    LOG_INFO("test_mms_delete");
    uint64_t userId = 1;
    DeleteItems items[2];
    DeleteItems item1 = {"aaa"};
    DeleteItems item2 = {"bbb"};
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsDelete(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_delete_invalid_batch)
{
    LOG_INFO("test_mms_delete_invalid_batch");
    uint64_t userId = 1;
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsDelete(userId, nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_delete_invalid_parameter)
{
    LOG_INFO("test_mms_delete_invalid_parameter");
    gServer->NotifyServiceable(true);
    uint64_t userId = 1;
    DeleteItems items[1];

    // invalid key
    items[0] = {""};
    uint32_t itemNum = 1;
    auto ret = MmsDelete(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_replace_unavailable)
{
    LOG_INFO("test_mms_replace_unavailable");
    uint64_t userId = 1;
    ReplaceItems items[2];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = {"aaa", "value1", offset, length};
    ReplaceItems item2 = {"bbb", "value2", offset, length};
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    gServer->NotifyServiceable(false);
    auto ret = MmsReplace(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_invalid_batch)
{
    LOG_INFO("test_mms_replace_invalid_batch");
    uint64_t userId = 1;
    uint32_t itemNum = 0;

    gServer->NotifyServiceable(true);
    auto ret = MmsReplace(userId, nullptr, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_replace_invalid_parameter)
{
    LOG_INFO("test_mms_replace_invalid_parameter");
    gServer->NotifyServiceable(true);
    uint64_t userId = 1;
    ReplaceItems items[1];

    // invalid key
    uint32_t length = 7;
    uint32_t offset = 0;
    items[0] = {"", "value1", offset, length};
    uint32_t itemNum = 1;
    auto ret = MmsReplace(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid value
    items[0] = {"key1", nullptr, offset, length};
    ret = MmsReplace(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);

    // invalid length
    offset = 1;
    length = 0;
    items[0] = {"key1", "value1", offset, length};
    ret = MmsReplace(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_EPERM);
}

TEST_F(TestMms, test_mms_replace_with_single_key)
{
    LOG_INFO("test_mms_replace_with_single_key");
    uint64_t userId = 1;
    ReplaceItems items[1];
    uint32_t length = 7;
    uint32_t offset = 0;
    items[0] = {"aaa", "value1", offset, length};
    uint32_t itemNum = 1;

    gServer->NotifyServiceable(true);
    auto ret = MmsReplace(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_with_multi_key)
{
    LOG_INFO("test_mms_replace_with_multi_key");
    uint64_t userId = 1;
    ReplaceItems items[2];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = {"aaa", "value1", offset, length};
    ReplaceItems item2 = {"aaa", "value2", offset, length};
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsReplace(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_old_key)
{
    LOG_INFO("test_mms_replace_old_key");
    uint64_t userId = 1;
    ReplaceItems items[2];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = {"aaa", "value1", offset, length};
    ReplaceItems item2 = {"aaa", "value2", offset, length};
    items[0] = item1;
    items[1] = item2;
    uint32_t itemNum = 2;
    auto ret = MmsReplace(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_mixed)
{
    LOG_INFO("test_mms_replace_mixed");
    uint64_t userId = 1;
    ReplaceItems items[3];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = {"aaa", "value1", offset, length};
    ReplaceItems item2 = {"aaa", "value2", offset, length};
    ReplaceItems item3 = {"bbb", "value3", offset, length};
    items[0] = item1;
    items[1] = item2;
    items[2] = item3;
    uint32_t itemNum = 3;
    auto ret = MmsReplace(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

TEST_F(TestMms, test_mms_replace_repeat_key)
{
    LOG_INFO("test_mms_replace_repeat_key");
    uint64_t userId = 1;
    ReplaceItems items[3];
    uint32_t length = 7;
    uint32_t offset = 0;
    ReplaceItems item1 = {"aaa", "value1", offset, length};
    ReplaceItems item3 = {"bbb", "value3", offset, length};
    ReplaceItems item2 = {"aaa", "value1", offset, length};
    items[0] = item1;
    items[1] = item2;
    items[2] = item3;
    uint32_t itemNum = 3;
    auto ret = MmsReplace(userId, items, itemNum);
    EXPECT_EQ(ret, RET_MMS_UNAVAILABLE);
}

