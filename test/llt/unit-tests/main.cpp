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

static void ExitBackgroundThreads(uint64_t ptId, uint64_t ptv)
{
    Cache::Instance().Recover();
    TestWCache::Stub();
    WCacheManager::Instance()->ExpiredClear(ptId, ptv);
    WCacheManager::Instance()->Flush(ptId, ptv);

    HTracerExit();
    Cache::Instance().Exit();
    FlowManager::Instance()->Exit();
    BdmDestory(0);
    Logger::ChangeLogLevel(-1);
    Logger::ChangeLogLevel(NO_3);
    Logger::Destroy();
    ZkFreeParaList();
    CmServerListenDiskFault(0, 0, 0);
    CmServerMonitorExit();
    CmServerViewExit();
    BioServerUninit();
    sleep(NO_24);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "RCACHE_MANAGER_GET_INSTANCE_FAIL", 0, 1, userParam);
    Cache::Instance().Init();
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_MANAGER_GET_INSTANCE_FAIL");
    LVOS_HVS_activeTracePoint(0, "WCACHE_MANAGER_GET_INSTANCE_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_MANAGER_INIT_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_MANAGER_INIT_FAIL_RESET", 0, 1, userParam);
    Cache::Instance().Init();
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_MANAGER_GET_INSTANCE_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_MANAGER_INIT_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_MANAGER_INIT_FAIL_RESET");
}

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
    TestHtracer::Stub();
    (void)system("chmod 777 /var/log/boostio/bio.log");
    (void)system("rm -rf test1");
    (void)system("rm -rf test2");
    (void)system("rm -rf ceph");
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
    (void)system("sed -i 's#bio.log.level = info#bio.log.level = debug#g' ./conf/bio.conf");
    (void)system("sed -i 's#bio.underfs.ceph.cfg.path = /etc/ceph/ceph.conf"
        "#bio.underfs.ceph.cfg.path = ./ceph.conf#g' ./conf/bio.conf");
    (void)system("touch ceph.conf");

    auto ret = BioInitialize(WorkerMode::CONVERGENCE);
    if (ret != RET_CACHE_OK) {
        std::cout << "boostio initialize failed, result:" << ret << "." << std::endl;
        return -1;
    }

    ::testing::InitGoogleTest(&argc, argv);
    int runRet = RUN_ALL_TESTS();

    (void)system("rm -rf conf");
    (void)system("rm -rf ceph.conf");

    ClearTraceInfo();
    uint64_t ptId = 1;
    uint64_t ptv = 2;
    Cache::Instance().ExpiredClear(ptId, ptv);
    Cache::Instance().Flush(ptId, ptv);

    std::cout << "Exiting background threads...maybe cost 80-90s" << std::endl;
    sleep(NO_60);
    ExitBackgroundThreads(ptId, ptv);
    std::cout << "All background threads exit" << std::endl;
    return runRet;
}