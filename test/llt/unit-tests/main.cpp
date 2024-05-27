/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "bio_server.h"
#include "htracer.h"
#include "wcache_manager.h"
#include "test_cm.h"
#include "test_net.h"
#include "test_htracer.h"
#include "test_disk.h"
#include "bdm_core.h"
#include "test_wcache.h"
#include "server/cm_server_view.h"
#include "server/cm_server_monitor.h"
#include "ut_common.h"
#include "flow_manager.h"
#include "bio_server_c.h"

using namespace ock::bio;
using namespace ock::htracer;

static bool DiskPathInvalid()
{
    std::string filename = "/opt/boostio/bin/conf/bio.conf";
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
    TestHtracer::Stub();
    (void)system("rm -rf test1");
    (void)system("rm -rf test2");
    (void)system("rm -rf ceph");
    (void)system("rm -rf conf");
    (void)system("mkdir -p /opt/boostio/bin/conf/");
    (void)system("cp ../configs/* /opt/boostio/bin/conf");
    (void)system("sed -i 's/bio.mem.size_in_gb = .*/bio.mem.size_in_gb = 1/g' /opt/boostio/bin/conf/bio.conf");
    (void)system("sed -i 's/bio.cm.zk_host =.*/bio.cm.zk_host = 127.0.0.1:2181/g' /opt/boostio/bin/conf/bio.conf");
    if (DiskPathInvalid()) {
        TestDisk::Stub();
        (void)system("sed -i 's/bio.disk.path = .*/bio.disk.path = test1:test2/g' /opt/boostio/bin/conf/bio.conf");
        (void)system("touch test1");
        (void)system("touch test2");
    }
    (void)system("sed -i 's#bio.log.level = info#bio.log.level = debug#g' /opt/boostio/bin/conf/bio.conf");
    (void)system("sed -i 's#bio.underfs.ceph.cfg.path = /etc/ceph/ceph.conf"
        "#bio.underfs.ceph.cfg.path = ./ceph.conf#g' /opt/boostio/bin/conf/bio.conf");
    (void)system("sed -i 's#bio.net.tls.enable.switch = true"
                 "#bio.net.tls.enable.switch = false#g' /opt/boostio/bin/conf/bio.conf");
    (void)system("sed -i 's#bio.net.hesc.server.tls.kfs.master.path = /path/server/master/kfsa"
                 "##g' /opt/boostio/bin/conf/bio.conf");
    (void)system("sed -i 's#bio.net.hesc.server.tls.kfs.pass.standby.path = /path/server/standby/kfsb"
                 "##g' /opt/boostio/bin/conf/bio.conf");
    (void)system("touch ceph.conf");

    std::cout << "Start boostio tester begin..." << std::endl;
    auto ret = BioInitialize(WorkerMode::CONVERGENCE, nullptr);
    if (ret != RET_CACHE_OK) {
        std::cout << "boostio initialize failed, result:" << ret << "." << std::endl;
        return -1;
    }
    std::cout << "Start boostio tester success." << std::endl;

    ::testing::InitGoogleTest(&argc, argv);
    int runRet = RUN_ALL_TESTS();

    (void)system("rm -rf conf");
    (void)system("rm -rf ceph.conf");
    sleep(NO_60);

    BioExit();
    std::cout << "Exit boostio tester success." << std::endl;
    return runRet;
}