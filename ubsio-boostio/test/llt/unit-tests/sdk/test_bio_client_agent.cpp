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
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "gtest/gtest.h"
#include <mockcpp/mockcpp.hpp>
#include "bio_mock.h"

#define private public
#include "bio_client_agent.h"
#undef private

#include "bio_crc_util.h"
#include "bio_client.h"
#include "bio_server.h"
#include "bio_server_c.h"
#include "bio_c.h"

using namespace ock::bio;
using namespace ock::bio::agent;

namespace {
constexpr char TEST_GET_VALUE[] = "standalone-async-get";

int32_t RuntimeConfigOpStub(StandaloneRuntimeConfigResponse *rsp)
{
    if (rsp == nullptr) {
        return BIO_INVALID_PARAM;
    }
    rsp->serverPid = getpid();
    rsp->alignSize = NO_4096;
    rsp->ioTimeOut = NO_60;
    rsp->netTimeOut = NO_16;
    rsp->logLevel = 1;
    rsp->scene = 0;
    rsp->sdkPoolSize = IO_SIZE_4K;
    rsp->dataMsgBlockSize = IO_SIZE_4K;
    return BIO_OK;
}

void *LoadWithoutUnderFsType(BioClientAgent *, const char *name)
{
    if (std::strcmp(name, "GetUnderFsType") == 0) {
        return nullptr;
    }
    return reinterpret_cast<void *>(RuntimeConfigOpStub);
}

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
        handler(agentParam->handler), getRuntimeConfigOp(agentParam->getRuntimeConfigOp), getOp(agentParam->getOp)
    {
    }

    ~AgentStateGuard()
    {
        agent->mMode = mode;
        agent->handler = handler;
        agent->getRuntimeConfigOp = getRuntimeConfigOp;
        agent->getOp = getOp;
    }

    BioClientAgentPtr agent;
    WorkerMode mode;
    void *handler;
    BioClientAgent::GetRuntimeConfigFuncPtr getRuntimeConfigOp;
    BioClientAgent::GetFuncPtr getOp;
};

struct AsyncGetCallbackState {
    bool called{false};
    int32_t result{BIO_INNER_ERR};
    uint64_t realLen{0};
    uint32_t respLen{0};
};
}

TEST(BioClientAgentTest, standalone_runtime_config_requires_direct_mode_and_loaded_op)
{
    auto agent = BioClientAgent::Instance();
    AgentStateGuard guard(agent);
    StandaloneRuntimeConfigResponse rsp = {};

    agent->mMode = SEPARATES;
    agent->getRuntimeConfigOp = RuntimeConfigOpStub;
    EXPECT_EQ(agent->GetRuntimeConfig(rsp), BIO_NOT_READY);

    agent->mMode = STANDALONE;
    agent->getRuntimeConfigOp = nullptr;
    EXPECT_EQ(agent->GetRuntimeConfig(rsp), BIO_NOT_READY);
}

TEST(BioClientAgentTest, standalone_runtime_config_uses_loaded_direct_op)
{
    auto agent = BioClientAgent::Instance();
    AgentStateGuard guard(agent);
    StandaloneRuntimeConfigResponse rsp = {};

    agent->mMode = STANDALONE;
    agent->getRuntimeConfigOp = RuntimeConfigOpStub;

    EXPECT_EQ(agent->GetRuntimeConfig(rsp), BIO_OK);
    EXPECT_EQ(rsp.serverPid, getpid());
    EXPECT_EQ(rsp.alignSize, NO_4096);
    EXPECT_EQ(rsp.ioTimeOut, NO_60);
    EXPECT_EQ(rsp.netTimeOut, NO_16);
    EXPECT_EQ(rsp.sdkPoolSize, IO_SIZE_4K);
    EXPECT_EQ(rsp.dataMsgBlockSize, IO_SIZE_4K);
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

TEST(BioClientAgentTest, UnderFsTypeSymbolIsRequiredDuringOperationInitialization)
{
    BioClientAgent agent;
    MOCKER_CPP(&BioClientAgent::LoadFunction, void *(*)(BioClientAgent *, const char *))
        .stubs()
        .will(invoke(LoadWithoutUnderFsType));
    EXPECT_EQ(agent.InitOperation(), BIO_INNER_ERR);
    EXPECT_EQ(agent.getUnderFsTypeOp, nullptr);
    GlobalMockObject::verify();
}

TEST(BioClientAgentTest, UnderFsTypeRequiresReadyRuntimeAndPreservesOutputOnFailure)
{
    BioUnderFsType type = BIO_UNDERFS_BUTT;
    EXPECT_EQ(BioGetUnderFsType(nullptr), RET_CACHE_EPERM);
    auto client = BioClient::Instance();
    auto clientAgent = agent::BioClientAgent::Instance();
    const bool originalReady = client->Ready();
    const auto originalMode = clientAgent->mMode;
    const auto originalOp = clientAgent->getUnderFsTypeOp;
    auto config = BioServer::Instance()->GetConfig();
    auto &underFsType = const_cast<BioConfig::UnderFsConfig &>(config->GetUnderFsConfig()).underFsType;
    const auto originalUnderFsType = underFsType;
    client->SetStartWorker(false);
    EXPECT_EQ(BioGetUnderFsType(&type), RET_CACHE_NOT_READY);
    EXPECT_EQ(type, BIO_UNDERFS_BUTT);
    client->SetStartWorker(true);
    clientAgent->mMode = STANDALONE;
    clientAgent->getUnderFsTypeOp = nullptr;
    EXPECT_EQ(BioGetUnderFsType(&type), RET_CACHE_ERROR);
    EXPECT_EQ(type, BIO_UNDERFS_BUTT);
    clientAgent->getUnderFsTypeOp = GetUnderFsType;
    EXPECT_EQ(GetUnderFsType(nullptr), BIO_INVALID_PARAM);
    for (auto expected : {BIO_UNDERFS_NONE, BIO_UNDERFS_CEPH, BIO_UNDERFS_HDFS, BIO_UNDERFS_LOCAL, BIO_UNDERFS_SHARED}) {
        underFsType = expected;
        EXPECT_EQ(BioGetUnderFsType(&type), RET_CACHE_OK);
        EXPECT_EQ(type, expected);
    }
    underFsType = BIO_UNDERFS_BUTT;
    EXPECT_EQ(BioGetUnderFsType(&type), RET_CACHE_ERROR);
    EXPECT_EQ(type, BIO_UNDERFS_SHARED);
    underFsType = originalUnderFsType;
    clientAgent->getUnderFsTypeOp = originalOp;
    clientAgent->mMode = originalMode;
    client->SetStartWorker(originalReady);
}
