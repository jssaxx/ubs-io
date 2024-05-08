/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include <iostream>
#include <memory>
#include <csignal>
#include <climits>
#include <sys/resource.h>
#include "htracer.h"
#include "bio_client.h"
#include "bio_sdk_config_instance.h"

using namespace ock::bio;

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

int main(int argc, char **argv)
{
    if (argc != 2U) {
        std::cout << "Invalid parameter, please input deployment mode." << std::endl;
        std::cout << "Usage: bio_console [0/1], 0-mean:converged deployment, 1-mean:separated deployment" << std::endl;
        return -1;
    }

    struct sigaction termSa {};
    termSa.sa_handler = &ConsoleHandleSigterm;
    sigaction(SIGTERM, &termSa, nullptr);

    TlsOptionsConfig optConf;
    WorkerMode mode = static_cast<WorkerMode>(std::stoul(argv[1]));
    if (mode == SEPARATES) {
        BioSdkConfigPtr mConfig = BioSdkConfig::Instance();
        if (mConfig == nullptr) {
            std::cout << "Create bio sdk configuration instance failed." << std::endl;
            return BIO_ERR;
        }
        auto result = mConfig->Initialize(".");
        if (result != BIO_OK) {
            std::cout << "Failed to initialize sdk configuration, result: " << result << "."<< std::endl;
            return BIO_ERR;
        }
        optConf.enableTls = mConfig->GetNetConfig().enableTls;      /* tls switch */
        strncpy_s(optConf.certificationPath, PATH_MAX + 1, mConfig->GetNetConfig().tlsClientCertPath.c_str(),
            mConfig->GetNetConfig().tlsClientCertPath.size());          /* certification path */
        strncpy_s(optConf.caCerPath, PATH_MAX + 1, mConfig->GetNetConfig().tlsCaCertPath.c_str(),
            mConfig->GetNetConfig().tlsCaCertPath.size());              /* caCer path */
        strncpy_s(optConf.caCrlPath, PATH_MAX + 1, mConfig->GetNetConfig().tlsCaCrlPath.c_str(),
            mConfig->GetNetConfig().tlsCaCrlPath.size());               /* caCrl path */
        strncpy_s(optConf.privateKeyPath, PATH_MAX + 1, mConfig->GetNetConfig().tlsClientKeyPath.c_str(),
            mConfig->GetNetConfig().tlsClientKeyPath.size());           /* private key path */
        strncpy_s(optConf.privateKeyPassword, PATH_MAX + 1, mConfig->GetNetConfig().tlsClientKeyPassPath.c_str(),
            mConfig->GetNetConfig().tlsClientKeyPassPath.size());       /* private key password */
        strncpy_s(optConf.hseKfsMasterPath, PATH_MAX + 1, mConfig->GetNetConfig().hseKfsMasterPath.c_str(),
            mConfig->GetNetConfig().hseKfsMasterPath.size());           /* hseceasy kfs master path */
        strncpy_s(optConf.hseKfsMasterPath, PATH_MAX + 1, mConfig->GetNetConfig().hseKfsStandbyPath.c_str(),
            mConfig->GetNetConfig().hseKfsStandbyPath.size());          /* hseceasy kfs standby path */
    }

    auto ret = BioService::Initialize(mode, optConf);
    if (ret != RET_CACHE_OK) {
        std::cout << "Initialize bio service failed, ret " << ret << std::endl;
        return -1;
    }

    gDaemonRunning = true;
    std::cout << "BoostIO Console Start Success." << std::endl;
    while (gDaemonRunning) {
        sleep(NO_5);
    }
    return 0;
}