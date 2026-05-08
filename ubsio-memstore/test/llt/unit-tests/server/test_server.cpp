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