/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"
#include "test_bio_server.h"

using namespace ock::bio;

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

TEST_F(TestBioServer, test_bio_server_initialize) {}

TEST_F(TestBioServer, test_evict_case_return_ok) {}