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
    LOG_INFO("test_net_get_data_channel");
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
    LOG_INFO("test_net_init_common_allocator");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    BResult ret = engine->InitCommMemAllocator();
    EXPECT_EQ(ret, BIO_OK);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SERVER_NET_FAIL_TO_REGISTER_BY_SIZE", 0, 1, userParam);
    ret = engine->InitCommMemAllocator();
    EXPECT_EQ(ret, BIO_NOT_READY);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_NET_FAIL_TO_REGISTER_BY_SIZE");
}

TEST_F(TestNet, test_net_init_shm_allocator)
{
    LOG_INFO("test_net_init_shm_allocator");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SERVER_NET_FAIL_TO_REGISTER_BY_SIZE", 0, 1, userParam);
    BResult ret = engine->InitShmMemAllocator();
    EXPECT_EQ(ret, BIO_NOT_READY);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_NET_FAIL_TO_REGISTER_BY_SIZE");

    LVOS_HVS_activeTracePoint(0, "SERVER_NET_FAIL_TO_CREATE_MEMORY_FILE", 0, 1, userParam);
    ret = engine->InitShmMemAllocator();
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_NET_FAIL_TO_CREATE_MEMORY_FILE");

    LVOS_HVS_activeTracePoint(0, "SERVER_NET_FAIL_TO_TRUNCATE_FILE_WITH_SIZE", 0, 1, userParam);
    ret = engine->InitShmMemAllocator();
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_NET_FAIL_TO_TRUNCATE_FILE_WITH_SIZE");

    LVOS_HVS_activeTracePoint(0, "SERVER_NET_FAIL_TO_MMAP_SHM_SIZE", 0, 1, userParam);
    ret = engine->InitShmMemAllocator();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_NET_FAIL_TO_MMAP_SHM_SIZE");
}

TEST_F(TestNet, test_net_new_channel)
{
    LOG_INFO("test_net_new_channel");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    ChannelPtr channel(ock::hcom::NetServiceDefaultImp::MakeChannel());
    int32_t ret = engine->NewChannel("127.0.0.1", channel, "bio-ctrl-2");
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_broken_channel)
{
    LOG_INFO("test_net_broken_channel");
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
    LOG_INFO("test_receive_handle");
    auto netEngine = BioServer::Instance()->GetNetEngine();
    auto ret =
        netEngine->RegisterNewRequestHandler(NO_100, std::bind(&TestNet::TestMsgHandle, this, std::placeholders::_1));
    EXPECT_EQ(ret, BIO_OK);

    ret = netEngine->RegisterNewRequestHandler(NO_100, std::bind(&TestNet::TestMsgHandle, this, std::placeholders::_1));
    EXPECT_EQ(ret, BIO_ALREADY_DONE);

    ret = netEngine->RegisterNewRequestHandler(NO_256, std::bind(&TestNet::TestMsgHandle, this, std::placeholders::_1));
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    ServiceContext ctx; // default opcode is equal to 0
    ret = netEngine->RequestReceived(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = netEngine->RequestIPCReceived(ctx);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_show)
{
    LOG_INFO("test_net_show");
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

TEST_F(TestNet, test_net_check_connect)
{
    LOG_INFO("test_net_check_connect");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    BioNodeId targetNodeId = 1;
    BResult ret = engine->CheckConnect(targetNodeId);
    EXPECT_EQ(ret, BIO_OK);

    targetNodeId = NO_100;
    ret = engine->CheckConnect(targetNodeId);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestNet, test_net_sync_call_intf_1)
{
    LOG_INFO("test_net_sync_call_intf_1");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t request = NO_1024;
    uint64_t response = 0;
    BResult ret = engine->SyncCall<uint64_t, uint64_t>(1, 1, request, response);
    EXPECT_EQ(ret, BIO_OK);

    uint16_t opCode = NO_256;
    ret = engine->SyncCall<uint64_t, uint64_t>(1, opCode, request, response);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    BioNodeId targetNodeId = NO_100;
    ret = engine->SyncCall<uint64_t, uint64_t>(targetNodeId, 1, request, response);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestNet, test_net_sync_call_intf_2)
{
    LOG_INFO("test_net_sync_call_intf_2");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t request = NO_1024;
    uint64_t *response = nullptr;
    uint64_t rspLen = 0;
    auto ret = engine->SyncCall<uint64_t, uint64_t>(1, 1, request, &response, rspLen);
    EXPECT_EQ(ret, BIO_OK);
    if (response != nullptr) {
        free(response);
    }

    uint16_t opCode = NO_256;
    ret = engine->SyncCall<uint64_t, uint64_t>(1, opCode, request, &response, rspLen);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    BioNodeId targetNodeId = NO_100;
    ret = engine->SyncCall<uint64_t, uint64_t>(targetNodeId, 1, request, &response, rspLen);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestNet, test_net_async_call_without_resp)
{
    LOG_INFO("test_net_async_call_without_resp");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t request = NO_1024;
    auto ret = engine->AsyncCallWithoutResponse<uint64_t>(1, 1, request);
    EXPECT_EQ(ret, BIO_OK);

    uint16_t opCode = NO_256;
    ret = engine->AsyncCallWithoutResponse<uint64_t>(1, opCode, request);
    EXPECT_EQ(ret, BIO_INVALID_PARAM);

    BioNodeId targetNodeId = NO_100;
    ret = engine->AsyncCallWithoutResponse<uint64_t>(targetNodeId, 1, request);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestNet, test_net_async_call)
{
    LOG_INFO("test_net_async_call");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t request = NO_1024;
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        LOG_INFO("test_net_async_call done");
        if (resp != nullptr) {
            free(resp);
        }
        return;
    };
    Callback callback(cbFunc, nullptr);
    engine->AsyncCall<uint64_t>(1, 1, request, callback);

    uint16_t opCode = NO_256;
    engine->AsyncCall<uint64_t>(1, opCode, request, callback);

    BioNodeId targetNodeId = NO_100;
    engine->AsyncCall<uint64_t>(targetNodeId, 1, request, callback);

    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SERVER_NET_FAILED_ASYNC_CALL_WITH_OP", 0, 1, userParam);
    engine->AsyncCall<uint64_t>(1, 1, request, callback);
    LVOS_HVS_deactiveTracePoint(0, "SERVER_NET_FAILED_ASYNC_CALL_WITH_OP");
}

TEST_F(TestNet, test_net_async_call_buff)
{
    LOG_INFO("test_net_async_call_buff");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t request = NO_1024;
    auto cbFunc = [](void *ctx, void *resp, uint32_t len, int32_t result) {
        LOG_INFO("test_net_async_call_buff done");
        if (resp != nullptr) {
            free(resp);
        }
        return;
    };
    Callback callback(cbFunc, nullptr);

    engine->AsyncCallBuff(1, 1, &request, sizeof(uint64_t), callback);

    uint16_t opCode = NO_256;
    engine->AsyncCallBuff(1, opCode, &request, sizeof(uint64_t), callback);

    BioNodeId targetNodeId = NO_100;
    engine->AsyncCallBuff(targetNodeId, 1, &request, sizeof(uint64_t), callback);
}

TEST_F(TestNet, test_net_read)
{
    LOG_INFO("test_net_read");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    NetRequest request;
    auto ret = engine->SyncRead(1, 0, request);
    EXPECT_EQ(ret, BIO_OK);

    BioNodeId targetNodeId = NO_100;
    ret = engine->SyncRead(targetNodeId, 0, request);
    EXPECT_EQ(ret, BIO_NET_RETRY);

    ret = engine->SyncRead(1, request);
    EXPECT_EQ(ret, BIO_OK);

    targetNodeId = NO_100;
    ret = engine->SyncRead(targetNodeId, request);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestNet, test_net_write)
{
    LOG_INFO("test_net_write");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    NetRequest request;
    auto ret = engine->SyncWrite(1, 0, request);
    EXPECT_EQ(ret, BIO_OK);

    BioNodeId targetNodeId = NO_100;
    ret = engine->SyncWrite(targetNodeId, 0, request);
    EXPECT_EQ(ret, BIO_NET_RETRY);

    ret = engine->SyncWrite(1, request);
    EXPECT_EQ(ret, BIO_OK);

    targetNodeId = NO_100;
    ret = engine->SyncWrite(targetNodeId, request);
    EXPECT_EQ(ret, BIO_NET_RETRY);

    ServiceContext ctx;
    ret = engine->SyncWrite(ctx.Channel(), request);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestNet, test_net_receive_fds)
{
    LOG_INFO("test_net_receive_fds");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();

    int32_t realFd = 1;
    BResult ret = engine->ReceiveFds(1, &realFd, NO_1);
    EXPECT_EQ(ret, BIO_OK);

    BioNodeId targetNodeId = NO_100;
    ret = engine->ReceiveFds(targetNodeId, &realFd, NO_1);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestNet, test_net_set_shm_info)
{
    LOG_INFO("test_net_set_shm_info");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    int32_t mShmFd = -1;
    uint8_t *mShmAddr = nullptr;
    uint64_t mShmOffset = 0;
    uint64_t mShmLength = 0;
    engine->SetShmInfo(mShmFd, mShmAddr, mShmOffset, mShmLength);
}

TEST_F(TestNet, test_net_get_shm_address)
{
    LOG_INFO("test_net_get_shm_address");
    NetEnginePtr engine = BioServer::Instance()->GetNetEngine();
    uint64_t mShmOffset = 0;
    auto ret = engine->GetShmAddress(mShmOffset);
}
