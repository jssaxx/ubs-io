/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include "bio_mock.h"
#include "net_engine.h"
#include "bio_server.h"
#include "net_stub.h"
#include "tracepoint.h"
#include "test_net.h"

#include "bio_client_log.h"

using namespace ock::bio;

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

TEST_F(TestNet, test_net_get_data_channel)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    BioNodeId targetNodeId = 0;
    uint32_t pid = 0;
    ChannelPtr channel;
    BResult ret = engine->GetDataChanel(targetNodeId, pid, channel);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);

    targetNodeId = 1;
    ret = engine->GetDataChanel(targetNodeId, pid, channel);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_init_common_allocator)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    BResult ret = engine->InitCommMemAllocator();
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_new_channel)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    ChannelPtr channel(ock::hcom::NetServiceDefaultImp::MakeChannel());
    int32_t ret = engine->NewChannel("127.0.0.1", channel, "bio-ctrl-2");
    EXPECT_EQ(ret, BIO_OK);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SERVER_NET_PEER_CONNECTION_REFUSED", 0, 1, userParam);
    ret = engine->NewChannel("127.0.0.1", channel, "bio-ctrl-2");
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_NET_PEER_CONNECTION_REFUSED");
}

TEST_F(TestNet, test_net_broken_channel)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    ChannelPtr channel(ock::hcom::NetServiceDefaultImp::MakeChannel());
    engine->ChannelBroken(channel);
}

int32_t TestNet::TestMsgHandle(ServiceContext &ctx)
{
    return BIO_OK;
}

TEST_F(TestNet, test_receive_handle)
{
    auto netEngine = BioServer::Instance()->GetNetEngine();
    auto ret = netEngine->RegisterNewRequestHandler(NO_100,
        std::bind(&TestNet::TestMsgHandle, this, std::placeholders::_1));
    EXPECT_EQ(ret, BIO_OK);

    ret = netEngine->RegisterNewRequestHandler(NO_100,
        std::bind(&TestNet::TestMsgHandle, this, std::placeholders::_1));
    EXPECT_EQ(ret, BIO_ALREADY_DONE);

    ret = netEngine->RegisterNewRequestHandler(NO_256,
        std::bind(&TestNet::TestMsgHandle, this, std::placeholders::_1));
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ServiceContext ctx;
    ret = netEngine->RequestReceived(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = netEngine->RequestIPCReceived(ctx);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_show)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint32_t executorNum = 0;
    NetOptions option;
    engine->Show(executorNum, option);

    int32_t fd = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
    uint32_t mKey = 0;
    engine->QueryShmInfo(fd, offset, length, mKey);
}

TEST_F(TestNet, test_net_check_connect_succ)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    BioNodeId targetNodeId = 1;
    BResult ret = engine->CheckConnect(targetNodeId);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_sync_call)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t req1 = NO_1024;
    uint64_t rsp1 = 0;
    BResult ret = engine->SyncCall<uint64_t, uint64_t>(1, 1, req1, rsp1);
    EXPECT_EQ(ret, BIO_OK);

    uint64_t req2 = NO_1024;
    uint64_t *rsp2 = nullptr;
    uint64_t rsp2Len = 0;
    ret = engine->SyncCall<uint64_t, uint64_t>(1, 1, req2, &rsp2, rsp2Len);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_async_call_without_resp)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t req1 = NO_1024;
    BResult ret = engine->AsyncCallWithoutResponse<uint64_t>(1, 1, req1);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_async_call)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t req1 = NO_1024;
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        return;
    };
    Callback callback(cbFunc, nullptr);
    engine->AsyncCall<uint64_t>(1, 1, req1, callback);
}

TEST_F(TestNet, test_net_read)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    NetRequest req;
    auto ret = engine->SyncRead(1, 0, req);
    EXPECT_EQ(ret, BIO_OK);

    ret = engine->SyncRead(1, req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_write)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    NetRequest req;
    auto ret = engine->SyncWrite(1, 0, req);
    EXPECT_EQ(ret, BIO_OK);

    ret = engine->SyncWrite(1, req);
    EXPECT_EQ(ret, BIO_OK);

    ServiceContext ctx;
    ret = engine->SyncWrite(ctx.Channel(), req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_sync_connect)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint32_t mPid = 0;
    ConnectInfo mConnectInfo(INVALID_NID, mPid, INVALID_NID);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SERVER_NET_FAIL_TO_CONNECT_CTRL_PLANE", 0, 1, userParam);
    BResult ret = engine->SyncConnect(mConnectInfo);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_NET_FAIL_TO_CONNECT_CTRL_PLANE");
}

TEST_F(TestNet, test_net_receive_fds)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    BioNodeId targetNodeId = 1;
    int32_t realFd = 1;
    BResult ret = engine->ReceiveFds(targetNodeId, &realFd, NO_1);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestNet, test_net_set_shminfo)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    int32_t mShmFd = -1;
    uint8_t *mShmAddr = nullptr;
    uint64_t mShmOffset = 0;
    uint64_t mShmLength = 0;
    engine->SetShmInfo(mShmFd, mShmAddr, mShmOffset, mShmLength);
}

TEST_F(TestNet, test_net_get_shmaddress)
{
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t mShmOffset = 0;
    engine->GetShmAddress(mShmOffset);
    mShmOffset = 1;
    engine->GetShmAddress(mShmOffset);
}

