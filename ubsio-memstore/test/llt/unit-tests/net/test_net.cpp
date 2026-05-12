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
#include "net_engine.h"
#include "mms_server.h"

using namespace ock::mms;

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
