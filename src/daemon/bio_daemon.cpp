/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <csignal>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "bio_server.h"

using namespace ock::bio;

static std::atomic<bool> gDaemonRunning = { false };

static bool IsRunning()
{
    std::string processName = "bio_daemon";
    std::string filePath = "/opt/boostio/run/";
    std::string fileName = filePath + processName + ".lock";
    int ret = mkdir(filePath.c_str(), 640);
    if (ret < 0 && errno != EEXIST) {
        std::cout << "Mkdir failed, file path " << filePath.c_str() << " errno " << errno << "." << std::endl;
        return true;
    }

    int fd = open(fileName.c_str(), O_WRONLY | O_CREAT, 0600);
    if (fd < 0) {
        std::cout << "Open failed, file " << fileName.c_str() << "errno " << errno << "." << std::endl;
        return true;
    }

    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_whence = SEEK_SET;
    lock.l_len = 0;
    ret = fcntl(fd, F_SETLK, &lock);
    if (ret < 0) {
        std::cout << "Process lock file is locked." << std::endl;
        close(fd);
        return true;
    }

    return false;
}

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
    if (IsRunning()) {
        std::cout << "bio_daemon process is running." << std::endl;
        return -1;
    }

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