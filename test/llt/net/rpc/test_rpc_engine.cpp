//
// Created by root on 12/11/23.
//

#include "gtest/gtest.h"
#include <mockcpp/mokc.h>
#include <mockcpp/mockcpp.hpp>
#include "bio_mock.h"
#include "test_rpc_engine.h"
#include "rpc_engine.h"

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

void TestRpcEngine::Stub()
{
    MOCKER_CPP(&RpcEngine::SyncConnect, BResult (*)(ConnectInfo &info)).stubs().will(invoke(SyncConnect_Stub));
}

TEST_F(TestRpcEngine, test_rpc_engine_initialize) {}
