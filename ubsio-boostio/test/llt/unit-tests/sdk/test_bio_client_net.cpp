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

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "gtest/gtest.h"

#define private public
#include "bio_client_net.h"
#undef private

using namespace ock::bio;
using namespace ock::bio::net;

namespace {
StandaloneRuntimeConfigResponse MakeRuntimeConfig()
{
    StandaloneRuntimeConfigResponse rsp = {};
    rsp.serverPid = 123;
    rsp.scene = 1;
    rsp.alignSize = NO_4096;
    rsp.ioTimeOut = NO_60;
    rsp.netTimeOut = NO_16;
    rsp.netSegmentSize = NO_512;
    rsp.logLevel = 4;
    rsp.enableCrc = true;
    rsp.enableCli = true;
    rsp.enableHtrace = true;
    rsp.enablePrometheus = true;
    rsp.scrapeIntervalSec = 30;
    std::snprintf(rsp.listenAddress, sizeof(rsp.listenAddress), "%s", "127.0.0.1:9090");
    return rsp;
}

class ClientNetStateGuard {
public:
    explicit ClientNetStateGuard(BioClientNetPtr netParam) : net(netParam), mode(netParam->mMode),
        workScene(netParam->mWorkScene), workIoAlignSize(netParam->mWorkIoAlignSize),
        workIoTimeOut(netParam->mWorkIoTimeOut), workNetTimeOut(netParam->mWorkNetTimeOut),
        logLevel(netParam->mLogLevel), enableHtrace(netParam->mEnableHtrace), enableCrc(netParam->mEnableCrc),
        enableCli(netParam->mEnableCli), shmFd(netParam->mShmFd), serverPid(netParam->mServerPid),
        netSegmentSize(netParam->mNetSegmentSize), shmOffset(netParam->mShmOffset), shmLength(netParam->mShmLength),
        shmKey(netParam->mShmKey), enablePrometheus(netParam->mEnablePrometheus),
        prometheusListenAddress(netParam->mPrometheusListenAddress),
        prometheusScrapeIntervalSec(netParam->mPrometheusScrapeIntervalSec)
    {
    }

    ~ClientNetStateGuard()
    {
        net->mMode = mode;
        net->mWorkScene = workScene;
        net->mWorkIoAlignSize = workIoAlignSize;
        net->mWorkIoTimeOut = workIoTimeOut;
        net->mWorkNetTimeOut = workNetTimeOut;
        net->mLogLevel = logLevel;
        net->mEnableHtrace = enableHtrace;
        net->mEnableCrc = enableCrc;
        net->mEnableCli = enableCli;
        net->mShmFd = shmFd;
        net->mServerPid = serverPid;
        net->mNetSegmentSize = netSegmentSize;
        net->mShmOffset = shmOffset;
        net->mShmLength = shmLength;
        net->mShmKey = shmKey;
        net->mEnablePrometheus = enablePrometheus;
        net->mPrometheusListenAddress = prometheusListenAddress;
        net->mPrometheusScrapeIntervalSec = prometheusScrapeIntervalSec;
    }

    BioClientNetPtr net;
    WorkerMode mode;
    uint32_t workScene;
    uint32_t workIoAlignSize;
    uint32_t workIoTimeOut;
    uint32_t workNetTimeOut;
    int32_t logLevel;
    bool enableHtrace;
    bool enableCrc;
    bool enableCli;
    int32_t shmFd;
    int32_t serverPid;
    uint32_t netSegmentSize;
    uint64_t shmOffset;
    uint64_t shmLength;
    uint64_t shmKey;
    bool enablePrometheus;
    std::string prometheusListenAddress;
    uint32_t prometheusScrapeIntervalSec;
};
}

TEST(BioClientNetTest, standalone_runtime_config_validation_checks_boundaries)
{
    auto net = BioClientNet::Instance();
    auto rsp = MakeRuntimeConfig();
    EXPECT_TRUE(net->CheckRuntimeConfigResp(rsp));

    rsp = MakeRuntimeConfig();
    rsp.alignSize = 0;
    EXPECT_FALSE(net->CheckRuntimeConfigResp(rsp));

    rsp = MakeRuntimeConfig();
    rsp.alignSize = NO_4194304 + 1;
    EXPECT_FALSE(net->CheckRuntimeConfigResp(rsp));

    rsp = MakeRuntimeConfig();
    rsp.ioTimeOut = NO_60 - 1;
    EXPECT_FALSE(net->CheckRuntimeConfigResp(rsp));

    rsp = MakeRuntimeConfig();
    rsp.netTimeOut = NO_16 - 1;
    EXPECT_FALSE(net->CheckRuntimeConfigResp(rsp));

    rsp = MakeRuntimeConfig();
    rsp.logLevel = 5;
    EXPECT_FALSE(net->CheckRuntimeConfigResp(rsp));

    rsp = MakeRuntimeConfig();
    rsp.scene = 2;
    EXPECT_FALSE(net->CheckRuntimeConfigResp(rsp));
}

TEST(BioClientNetTest, apply_standalone_runtime_config_clears_shm_and_updates_getters)
{
    auto net = BioClientNet::Instance();
    ClientNetStateGuard guard(net);
    auto rsp = MakeRuntimeConfig();

    net->mMode = STANDALONE;
    net->mShmFd = 10;
    net->mShmOffset = 11;
    net->mShmLength = 12;
    net->mShmKey = 13;

    net->ApplyStandaloneRuntimeConfig(rsp);

    EXPECT_EQ(net->mServerPid, rsp.serverPid);
    EXPECT_EQ(net->mShmFd, -1);
    EXPECT_EQ(net->mShmOffset, 0);
    EXPECT_EQ(net->mShmLength, 0);
    EXPECT_EQ(net->mShmKey, 0);
    EXPECT_EQ(net->GetLocalMrKey(), 0);
    EXPECT_EQ(net->GetSegmentSize(), static_cast<int32_t>(rsp.netSegmentSize));
    EXPECT_EQ(net->GetNegoWorkScene(), rsp.scene);
    EXPECT_EQ(net->GetNegoWorkIoAlignSize(), rsp.alignSize);
    EXPECT_EQ(net->GetNegoWorkIoTimeOut(), rsp.ioTimeOut);
    EXPECT_EQ(net->GetNegoWorkNetTimeOut(), rsp.netTimeOut);
    EXPECT_EQ(net->GetNegoLogLevel(), rsp.logLevel);
    EXPECT_TRUE(net->GetCrcFlag());
    EXPECT_TRUE(net->GetCliFlag());
    EXPECT_TRUE(net->GetHtraceFlag());
    EXPECT_TRUE(net->GetPrometheusToggle());
    EXPECT_EQ(net->GetPrometheusListenAddress(), "127.0.0.1:9090");
    EXPECT_EQ(net->GetPrometheusScrapeIntervalSec(), 30U);
}
