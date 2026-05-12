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

#include <csignal>
#include <sys/resource.h>

#include "mms_server.h"

using namespace ock::mms;

static std::atomic<bool> gDaemonRunning = { false };

static void HandleSigterm(int signum)
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

int main(int argc, char *argv[])
{
    auto mmsServer = MmsServer::Instance();
    auto ret = mmsServer->Start(ServiceStateFunc);
    if (ret != MMS_OK) {
        std::cout << "mms daemon start failed:" << ret << "." << std::endl;
        return -1;
    }
    struct sigaction termSa {};
    termSa.sa_handler = &HandleSigterm;
    sigaction(SIGTERM, &termSa, nullptr);

    gDaemonRunning = true;
    std::cout << "mms daemon start success." << std::endl;
    while (gDaemonRunning) {
        sleep(5U);
    }
    return 0;
}
