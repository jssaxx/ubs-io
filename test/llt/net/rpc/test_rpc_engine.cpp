//
// Created by root on 12/11/23.
//

#include "gtest/gtest.h"
#include <mockcpp/mokc.h>
#include <mockcpp/mockcpp.hpp>
#include "bio_mock.h"
#include "test_rpc_engine.h"
#include "net_engine.h"

using namespace ock::bio;

bool TestRpcEngine::g_setup = false;

void TestRpcEngine::SetUp()
{
    if (g_setup) {
        return;
    }
    // XXX
    g_setup = true;
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
        AsyncConnHandler handler, uintptr_t ctx)).stubs().will(invoke(AsyncConnect_Stub));
}

TEST_F(TestRpcEngine, test_rpc_engine_initialize) {}
