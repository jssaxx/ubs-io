/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include "bio_mock.h"
#include "net_engine.h"
#include "test_rpc_engine.h"

using namespace ock::bio;

bool TestRpcEngine::gSetup = false;

void TestRpcEngine::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestRpcEngine::TearDown()
{
    return;
}

BResult SyncConnect_Stub(ConnectInfo &info)
{
    return BIO_OK;
}

BResult AsyncConnect_Stub(ConnectInfo &info, AsyncConnHandler handler, uintptr_t ctx)
{
    uintptr_t userCtx = 0;
    handler(userCtx, 0, info);
    return BIO_OK;
}

void TestRpcEngine::Stub()
{
    MOCKER_CPP(&NetEngine::SyncConnect, BResult (*)(ConnectInfo &info)).stubs().will(invoke(SyncConnect_Stub));
    MOCKER_CPP(&NetEngine::AsyncConnect, BResult (*)(ConnectInfo &info,
        AsyncConnHandler handler, uintptr_t ctx)).stubs().will(returnValue(0));
}

TEST_F(TestRpcEngine, test_rpc_engine_initialize) {}
