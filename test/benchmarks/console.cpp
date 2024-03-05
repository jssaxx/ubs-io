/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <csignal>
#include <sys/resource.h>
#include "htracer.h"
#include "bio_client.h"

using namespace ock::bio;

std::atomic<bool> gDaemonRunning = { false };

void HandleSigterm(int signum)
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

int main(int argc, char **argv)
{
    if (argc != 2U) {
        std::cout << "Invalid parameter, please input deployment mode." << std::endl;
        std::cout << "Usage: bio_console [0/1], 0-mean:converged deployment, 1-mean:separated deployment" << std::endl;
        return -1;
    }

    struct sigaction termSa {};
    termSa.sa_handler = &HandleSigterm;
    sigaction(SIGTERM, &termSa, nullptr);

    WorkerMode mode = static_cast<WorkerMode>(std::stoul(argv[1]));
    auto ret = BioService::Initialize(mode);
    if (ret != RET_CACHE_OK) {
        std::cout << "Initialize bio service failed, ret " << ret << std::endl;
        return -1;
    }

    gDaemonRunning = true;
    std::cout << "BoostIO Console Start Success." << std::endl;
    while (gDaemonRunning) {
        sleep(5);
    }
    return 0;
}