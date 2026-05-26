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

#include <sys/resource.h>
#include <csignal>

#include "bio_server.h"

using namespace ock::bio;

static std::atomic<bool> gDaemonRunning = {false};

static void HandleSigterm(int signum)
{
    (void)signum;
    if (!gDaemonRunning) {
        std::cout << "Already exited!" << std::endl;
        return;
    }

    struct rlimit coreLimiter = {.rlim_cur = 0, .rlim_max = 0};
    int result = setrlimit(RLIMIT_CORE, &coreLimiter);
    if (UNLIKELY(result != 0)) {
        std::cout << "Failed to disable core dump, errno " << errno << std::endl;
    }
    gDaemonRunning = false;
}

int main(int argc, char *argv[])
{
    auto bioServer = BioServer::Instance();
    if (bioServer == nullptr) {
        std::cout << "BioServer init failed!" << std::endl;
        return -1;
    }
    auto ret = bioServer->Start();
    if (ret != BIO_OK) {
        std::cout << "BoostIO Daemon Start Fail." << std::endl;
        return -1;
    }

    struct sigaction termSa {};
    termSa.sa_handler = &HandleSigterm;
    sigaction(SIGTERM, &termSa, nullptr);

    gDaemonRunning = true;
    std::cout << "BoostIO Daemon Start Success." << std::endl;
    while (gDaemonRunning) {
        sleep(5U);
    }
    return 0;
}