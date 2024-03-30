/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"
#include "test_bio_server.h"

bool TestBioServer::gSetup = false;

void TestBioServer::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestBioServer::TearDown()
{
    return;
}

TEST_F(TestBioServer, test_bio_server_initialize)
{
}
