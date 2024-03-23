/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "bio_server.h"
#include "htracer.h"
#include "wcache_manager.h"
#include "test_cm.h"
#include "test_rpc_engine.h"
#include "test_htracer.h"

using namespace ock::bio;

int main(int argc, char *argv[])
{
    TestCm::Stub();
    TestRpcEngine::Stub();
    TestHtracer::Stub();
    (void)system("rm -rf conf");
    (void)system("mkdir conf");
    (void)system("cp ../configs/* conf");
    (void)system("sed -i 's/bio.mem.size_in_gb = .*/bio.mem.size_in_gb = 1/g' ./conf/bio.conf");
    (void)system("sed -i 's/bio.disk.size_in_gb = .*/bio.disk.size_in_gb = 1/g' ./conf/bio.conf");
    (void)system("sed -i 's/bio.disk.path = .*/bio.disk.path = test1:test2/g' ./conf/bio.conf");

    auto bioServer = BioServer::Instance();
    auto ret = bioServer->Start();
    EXPECT_EQ(ret, BIO_OK);

    ::testing::InitGoogleTest(&argc, argv);
    int runRet = RUN_ALL_TESTS();

    (void)system("rm -rf conf");

    ock::htracer::HTracerExit();
    WCacheManager::Instance()->Exit();
    return runRet;
}
