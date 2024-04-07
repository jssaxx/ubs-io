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
#include "test_disk.h"

using namespace ock::bio;
using namespace ock::htracer;

static bool DiskPathInvalid()
{
    std::string filename = "./conf/bio.conf";
    std::string target = "/dev/sdxx:/dev/sdyy";

    std::ifstream file(filename);
    std::string line;

    while (getline(file, line)) {
        if (line.find(target) != std::string::npos) {
            return true;
        }
    }
    return false;
}

int main(int argc, char *argv[])
{
    TestCm::Stub();
    TestRpcEngine::Stub();
    TestHtracer::Stub();
    (void)system("rm -rf conf");
    (void)system("mkdir conf");
    (void)system("cp ../configs/* conf");
    (void)system("sed -i 's/bio.mem.size_in_gb = .*/bio.mem.size_in_gb = 1/g' ./conf/bio.conf");
    (void)system("sed -i 's/bio.cm.zk_host =.*/bio.cm.zk_host = 127.0.0.1:2181/g' ./conf/bio.conf");
    if (DiskPathInvalid()) {
        TestDisk::Stub();
        (void)system("sed -i 's/bio.disk.path = .*/bio.disk.path = test1:test2/g' ./conf/bio.conf");
        (void)system("touch test1");
        (void)system("touch test2");
    }
    (void)system("sed -i 's#bio.underfs.ceph.cfg.path = /etc/ceph/ceph.conf"
        "#bio.underfs.ceph.cfg.path = ./ceph.conf#g' ./conf/bio.conf");
    (void)system("touch ceph.conf");

    auto bioServer = BioServer::Instance();
    auto ret = bioServer->Start();
    ASSERT_EQ(ret, BIO_OK);

    ::testing::InitGoogleTest(&argc, argv);
    int runRet = RUN_ALL_TESTS();

    (void)system("rm -rf conf");
    (void)system("rm -rf ceph.conf");
    if (DiskPathInvalid()) {
        (void)system("rm -rf test1");
        (void)system("rm -rf test2");
    }

    ClearTraceInfo();

    ock::htracer::HTracerExit();
    WCacheManager::Instance()->Exit();
    RCacheManager::Instance()->Exit();
    return runRet;
}
