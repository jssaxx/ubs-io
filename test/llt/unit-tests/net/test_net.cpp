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
