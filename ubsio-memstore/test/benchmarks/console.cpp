/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <iostream>
#include <memory>
#include <csignal>
#include <climits>
#include <sys/resource.h>
#include "mms_client.h"

static std::atomic<bool> gDaemonRunning = { false };

static void ConsoleHandleSigterm(int signum)
{
    (void)signum;
    if (!gDaemonRunning) {
        std::cout << "Already exited!" << std::endl;
        return;
    }

    struct rlimit coreLimiter = {
        .rlim_cur = 0,
        .rlim_max = 0
    };
    int result = setrlimit(RLIMIT_CORE, &coreLimiter);
    if (UNLIKELY(result != 0)) {
        std::cout << "Failed to disable core dump, errno " << errno << std::endl;
    }
    gDaemonRunning = false;
}

static void ServiceStateFunc(bool serviceable)
{
    std::string isNormal = serviceable ? "normal" : "fault";
    std::cout << "mms service state is: " << isNormal.c_str() << "." << std::endl;
}

int main(int argc, char **argv)
{
    if (argc != 4U) {
        std::cout << "Usage: mms_console [busypolling:0/1] [conn count:2] [worker groups:4]" << std::endl;
        return -1;
    }

    MmsOptions options;
    options.netIsBusyPolling = static_cast<bool>(std::stoul(argv[1]));
    options.netConnectCnt = static_cast<uint16_t>(std::stoul(argv[2]));
    options.netGroupNum = static_cast<uint16_t>(std::stoul(argv[3]));
    options.tlsEnable = 0;

    auto ret = MmsInitialize(options, ServiceStateFunc);
    if (ret != RET_MMS_OK) {
        std::cout << "mms console start failed:" << ret << std::endl;
        return -1;
    }

    gDaemonRunning = true;
    std::cout << "mms console start success." << std::endl;
    while (gDaemonRunning) {
        sleep(5);
    }
    return 0;
}
