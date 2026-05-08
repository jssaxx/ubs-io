/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

* ubs-io is licensed under the Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*      http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PSL v2 for more details.
*/

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <arpa/inet.h>
#include <cstring>
#include <ifaddrs.h>
#include <netinet/in.h>
#include "tracer.h"
#include "server/cm_server_view.h"
#include "server/cm_server_monitor.h"
#include "ut_common.h"
#include "mms_conv.h"
#include "mms_c.h"
#include "test_cluster.h"
#include "mms_types.h"
#include "net_engine.h"

using namespace ock::mms;

static struct ifaddrs gLoopbackIfaddr;
static struct sockaddr_in gLoopbackAddr;

static int GetIfAddrsStub(struct ifaddrs **ifap)
{
    if (ifap == nullptr) {
        return -1;
    }

    (void)memset_s(&gLoopbackIfaddr, sizeof(gLoopbackIfaddr), 0, sizeof(gLoopbackIfaddr));
    (void)memset_s(&gLoopbackAddr, sizeof(gLoopbackAddr), 0, sizeof(gLoopbackAddr));
    gLoopbackAddr.sin_family = AF_INET;
    (void)inet_pton(AF_INET, "127.0.0.1", &gLoopbackAddr.sin_addr);
    gLoopbackIfaddr.ifa_addr = reinterpret_cast<struct sockaddr *>(&gLoopbackAddr);
    gLoopbackIfaddr.ifa_next = nullptr;
    *ifap = &gLoopbackIfaddr;
    return 0;
}

static void FreeIfAddrsStub(struct ifaddrs *ifa)
{
    return;
}

static void StubSingleNodeUtEnv()
{
    MOCKER(getifaddrs).stubs().will(invoke(GetIfAddrsStub));
    MOCKER(freeifaddrs).stubs().will(invoke(FreeIfAddrsStub));
    MOCKER_CPP(&NetEngine::Start).stubs().will(returnValue(static_cast<BResult>(MMS_OK)));
}

static void ServiceStateFunc(bool serviceable)
{
    std::string isNormal = serviceable ? "normal" : "fault";
    std::cout << "mms service state is: " << isNormal.c_str() << std::endl;
}

int main(int argc, char *argv[])
{
    TestCluster::Stub();
    StubSingleNodeUtEnv();
    // Log
    (void)system("sed -i 's#mms.log.level = info#mms.log.level = debug#g' ../conf/mms.conf");
    // tracer
    (void)system("sed -i 's/mms.trace.switch = false.*/mms.trace.switch = true/g' ../conf/mms.conf");
    // Multicast
    (void)system("sed -i 's/mms.multicast.switch =.*/mms.multicast.switch = false/g' ../conf/mms.conf");
    // Deployment
    (void)system("sed -i 's/mms.deployment.mode =.*/mms.deployment.mode = converge/g' ../conf/mms.conf");
    // Memory
    (void)system("sed -i 's/mms.mem.numa.id =.*/mms.mem.numa.id = 0/g' ../conf/mms.conf");
    (void)system("sed -i 's/mms.mem.numa.size =.*/mms.mem.numa.size = 1/g' ../conf/mms.conf");
    // Network
    (void)system("sed -i 's#mms.net.rpc.ip_mask =.*#mms.net.rpc.ip_mask = 127.0.0.1/24#g' ../conf/mms.conf");
    (void)system("sed -i 's/mms.net.rpc.listen_port =.*/mms.net.rpc.listen_port = 7502/g' ../conf/mms.conf");
    (void)system("sed -i 's/mms.net.rpc.protocol =.*/mms.net.rpc.protocol = tcp/g' ../conf/mms.conf");
    (void)system("sed -i 's/mms.net.rpc.busy_polling_mode =.*/mms.net.rpc.busy_polling_mode = false/g' "
                 "../conf/mms.conf");
    (void)system("sed -i 's/mms.net.rpc.worker.groups =.*/mms.net.rpc.worker.groups = 1/g' ../conf/mms.conf");
    (void)system("sed -i 's/mms.net.rpc.worker.groups.cpuset =.*/mms.net.rpc.worker.groups.cpuset = 0-0/g' "
                 "../conf/mms.conf");
    (void)system("sed -i 's/mms.net.ipc.worker.groups =.*/mms.net.ipc.worker.groups = 1/g' ../conf/mms.conf");
    (void)system("sed -i 's/mms.net.ipc.worker.groups.cpuset =.*/mms.net.ipc.worker.groups.cpuset = 0-0/g' "
                 "../conf/mms.conf");
    (void)system("sed -i 's/mms.net.request.executor.thread.num =.*/mms.net.request.executor.thread.num = 8/g' "
                 "../conf/mms.conf");
    (void)system("sed -i 's/mms.net.publisher.worker.cpuset =.*/mms.net.publisher.worker.cpuset = 0-0/g' "
                 "../conf/mms.conf");
    (void)system("sed -i 's/mms.net.subscriber.worker.cpuset =.*/mms.net.subscriber.worker.cpuset = 0-0/g' "
                 "../conf/mms.conf");
    // Cluster Manager
    (void)system("sed -i 's#mms.cm.node.num = .*#mms.cm.node.num = 1#g' ../conf/mms.conf");
    (void)system("sed -i 's/mms.cm.node.id =.*/#mms.cm.node.id =/g' ../conf/mms.conf");
    (void)system("sed -i 's/mms.cm.zk_host =.*/mms.cm.zk_host = 127.0.0.1:2181/g' ../conf/mms.conf");
    (void)system("sed -i 's/mms.net.tls.enable = true.*/mms.net.tls.enable = false/g' ../conf/mms.conf");

    std::cout << "Start mms tester begin..." << std::endl;
    MmsOptions options;
    auto ret = MmsConv::Initialize(options, ServiceStateFunc);
    if (ret != RET_MMS_OK) {
        std::cout << "mms initialize failed, result:" << ret << "." << std::endl;
        return -1;
    }

    ock::tracemark::TraceMark::Init();

    std::cout << "Start mms tester success." << std::endl;

    ::testing::InitGoogleTest(&argc, argv);
    int runRet = RUN_ALL_TESTS();

    (void)system("rm -rf conf");
    sleep(NO_60);

    MmsConv::Exit();
    std::cout << "Exit mms tester success." << std::endl;
    return runRet;
}
