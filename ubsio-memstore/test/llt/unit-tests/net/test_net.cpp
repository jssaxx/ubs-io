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

#include "test_net.h"
#include <fstream>
#include <set>
#include "net_engine.h"
#define private public
#define protected public
#include "net_multicast_engine.h"
#undef protected
#undef private
#include "mms_server.h"

using namespace ock::mms;

namespace {
int DecryptOk(const char *cipherText, const size_t cipherTextLen, char *plainText, size_t *plainTextLen)
{
    (void)cipherText;
    (void)cipherTextLen;
    const char *password = "plain";
    if (*plainTextLen < strlen(password)) {
        return -1;
    }
    (void)memcpy_s(plainText, *plainTextLen, password, strlen(password));
    *plainTextLen = strlen(password);
    return 0;
}

int DecryptFail(const char *cipherText, const size_t cipherTextLen, char *plainText, size_t *plainTextLen)
{
    (void)cipherText;
    (void)cipherTextLen;
    (void)plainText;
    (void)plainTextLen;
    return -1;
}
}  // namespace

bool TestNet::gSetup = false;

void TestNet::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestNet::TearDown()
{
    return;
}

TEST_F(TestNet, test_net_get_channel)
{
    LOG_INFO("test_net_get_channel");
    NetEnginePtr engine = MmsServer::Instance()->GetNetEngine();
    MmsNodeId targetNodeId = 0;
    uint32_t pid = 0;
    uint32_t groupIndex = 0;
    ChannelPtr channel;

    BResult ret = engine->GetChanel(targetNodeId, channel, groupIndex);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);

    ret = engine->GetChanel(targetNodeId, pid, channel, groupIndex);
    EXPECT_EQ(ret, MMS_NOT_EXISTS);
}

TEST_F(TestNet, test_multicast_engine_basic_branches)
{
    NetMulticastEngine engine;

    ock::hcom::SubscriptionInfoPtr info = new ock::hcom::SubscriptionInfo();
    EXPECT_NE(engine.NewSubscriptionCallBack(info), MMS_OK);
    EXPECT_EQ(engine.StartPublisherService(), MMS_NOT_READY);
    EXPECT_EQ(engine.StartSubscriberService(), MMS_NOT_READY);

    ock::mms::SubscriptionInfo subInfo;
    ock::mms::MulticastAsyncHandler handler;
    EXPECT_EQ(engine.SyncConnect(subInfo), MMS_NOT_READY);
    EXPECT_EQ(engine.AsyncConnect(subInfo, handler), MMS_NOT_READY);

    EXPECT_EQ(engine.RegisterSubscriberBrokenHandler(nullptr), MMS_INVALID_PARAM);
    EXPECT_EQ(engine.RegisterSubscriberBrokenHandler([](uint16_t nodeId) { (void)nodeId; }), MMS_OK);
    EXPECT_EQ(engine.RegisterSubscriberBrokenHandler([](uint16_t nodeId) { (void)nodeId; }), MMS_ERR);

    EXPECT_FALSE(engine.RemoteSendCheck({}));
    EXPECT_FALSE(engine.IsSubscriberExist("10.10.10.1"));
    engine.mSubScribers.emplace("10.10.10.1", ock::hcom::SubscriberPtr(nullptr));
    EXPECT_TRUE(engine.IsSubscriberExist("10.10.10.1"));

    std::unordered_set<std::string> remoteIps = {"10.10.10.2"};
    EXPECT_FALSE(engine.RemoteSendCheck(remoteIps));
    engine.mSubScribersRemote.emplace("10.10.10.2");
    EXPECT_TRUE(engine.RemoteSendCheck(remoteIps));
    EXPECT_TRUE(engine.CheckConnectDone(NO_2));

    EXPECT_FALSE(engine.CheckRemoteNodeStatus("192.0.2.88"));
    engine.Stop();
}

TEST_F(TestNet, test_multicast_tls_private_key_callback)
{
    NetMulticastEngine engine;
    MmsConfig::NetConfig options;
    options.privateKeyPath = "/tmp/mms_ut_private.key";
    options.privateKeyPasswordPath = "/tmp/mms_ut_key.pass";

    {
        std::ofstream file(options.privateKeyPasswordPath);
        file << "cipher" << std::endl;
    }

    engine.RegisterDecryptHandler(DecryptOk);
    auto callback = engine.CreatePrivateKeyCallback(options);
    std::string path;
    void *pwd = nullptr;
    int len = 0;
    ock::hcom::UBSHcomTLSEraseKeypass erase;
    EXPECT_TRUE(callback("key", path, pwd, len, erase));
    EXPECT_EQ(path, options.privateKeyPath);
    EXPECT_EQ(len, NO_5);
    ASSERT_NE(pwd, nullptr);
    erase(pwd, len);

    engine.RegisterDecryptHandler(DecryptFail);
    pwd = nullptr;
    len = 0;
    EXPECT_FALSE(callback("key", path, pwd, len, erase));
    EXPECT_EQ(pwd, nullptr);

    options.privateKeyPasswordPath = "/tmp/mms_ut_missing.pass";
    EXPECT_FALSE(callback("key", path, pwd, len, erase));
}
