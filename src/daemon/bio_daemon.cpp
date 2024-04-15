/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <csignal>
#include <sys/resource.h>

#include "bio_server.h"

using namespace ock::bio;

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

int main(int argc, char *argv[])
{
    auto bioServer = BioServer::Instance();
    auto ret = bioServer->Start();
    if (ret != BIO_OK) {
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