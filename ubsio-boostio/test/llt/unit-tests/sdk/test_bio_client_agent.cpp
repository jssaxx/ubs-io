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

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"

#define private public
#include "bio_client_agent.h"
#undef private

#include "bio_crc_util.h"

using namespace ock::bio;
using namespace ock::bio::agent;

namespace {
constexpr char TEST_GET_VALUE[] = "standalone-async-get";

int32_t StandaloneAsyncGetOpStub(GetRequest *req, GetResponse *rsp)
{
    if (req == nullptr || rsp == nullptr || req->address == 0) {
        return BIO_INVALID_PARAM;
    }

    auto valueLen = static_cast<uint64_t>(strlen(TEST_GET_VALUE));
    if (valueLen > req->size) {
        return BIO_INVALID_PARAM;
    }
    std::memcpy(reinterpret_cast<void *>(req->address), TEST_GET_VALUE, valueLen);
    rsp->isAlloc = false;
    rsp->num = 0;
    rsp->realLen = valueLen;
    rsp->dataCrc = BioCrcUtil::Crc32(reinterpret_cast<char *>(req->address), valueLen);
    return BIO_OK;
}

struct AgentStateGuard {
    explicit AgentStateGuard(BioClientAgentPtr agentParam) : agent(agentParam), mode(agentParam->mMode),
        getOp(agentParam->getOp)
    {
    }

    ~AgentStateGuard()
    {
        agent->mMode = mode;
        agent->getOp = getOp;
    }

    BioClientAgentPtr agent;
    WorkerMode mode;
    BioClientAgent::GetFuncPtr getOp;
};

struct AsyncGetCallbackState {
    bool called{false};
    int32_t result{BIO_INNER_ERR};
    uint64_t realLen{0};
    uint32_t respLen{0};
};
}

TEST(BioClientAgentTest, standalone_async_get_local_uses_direct_get)
{
    auto agent = BioClientAgent::Instance();
    AgentStateGuard guard(agent);
    agent->mMode = STANDALONE;
    agent->getOp = StandaloneAsyncGetOpStub;

    GetRequest req = {};
    req.length = sizeof(TEST_GET_VALUE);
    req.enableCrc = true;
    char value[sizeof(TEST_GET_VALUE)] = {};
    AsyncGetCallbackState state;
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        auto *state = static_cast<AsyncGetCallbackState *>(ctx);
        state->called = true;
        state->result = result;
        state->respLen = len;
        if (resp != nullptr) {
            state->realLen = static_cast<GetResponse *>(resp)->realLen;
        }
    };
    Callback callback(cbFunc, static_cast<void *>(&state));

    auto ret = agent->GetLocal(req, value, callback);

    EXPECT_EQ(ret, BIO_OK);
    EXPECT_TRUE(state.called);
    EXPECT_EQ(state.result, BIO_OK);
    EXPECT_EQ(state.respLen, sizeof(GetResponse));
    EXPECT_EQ(state.realLen, strlen(TEST_GET_VALUE));
    EXPECT_EQ(std::memcmp(value, TEST_GET_VALUE, strlen(TEST_GET_VALUE)), 0);
    EXPECT_EQ(req.isMr, 0);
    EXPECT_EQ(req.size, req.length);
    EXPECT_EQ(req.address, reinterpret_cast<uintptr_t>(value));
}
