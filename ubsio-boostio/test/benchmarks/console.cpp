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

static int ConsoleGetSdkConfig(BioSdkConfigPtr sdkConf, ClientOptionsConfig *optConf)
{
    optConf->logType = (LogType)sdkConf->GetLogTypeConfig();
    strncpy_s(optConf->logFilePath, PATH_MAX, sdkConf->GetLogFilePathConfig().c_str(),
        sdkConf->GetLogFilePathConfig().size());                    /* log file path */
    optConf->enable = sdkConf->GetNetConfig().enableTls;            /* tls switch */
    strncpy_s(optConf->certificationPath, PATH_MAX, sdkConf->GetNetConfig().tlsClientCertPath.c_str(),
        sdkConf->GetNetConfig().tlsClientCertPath.size());          /* certification path */
    strncpy_s(optConf->caCerPath, PATH_MAX, sdkConf->GetNetConfig().tlsCaCertPath.c_str(),
        sdkConf->GetNetConfig().tlsCaCertPath.size());              /* caCer path */
    strncpy_s(optConf->caCrlPath, PATH_MAX, sdkConf->GetNetConfig().tlsCaCrlPath.c_str(),
        sdkConf->GetNetConfig().tlsCaCrlPath.size());               /* caCrl path */
    strncpy_s(optConf->privateKeyPath, PATH_MAX, sdkConf->GetNetConfig().tlsClientKeyPath.c_str(),
        sdkConf->GetNetConfig().tlsClientKeyPath.size());           /* private key path */
    strncpy_s(optConf->privateKeyPassword, PATH_MAX, sdkConf->GetNetConfig().tlsClientKeyPassPath.c_str(),
        sdkConf->GetNetConfig().tlsClientKeyPassPath.size());       /* private key password */
    strncpy_s(optConf->decrypterLibPath, PATH_MAX, sdkConf->GetNetConfig().decrypterLibPath.c_str(),
        sdkConf->GetNetConfig().decrypterLibPath.size());           /* decrypter lib path */

    return BIO_OK;
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

    ClientOptionsConfig *optConf = new (std::nothrow) ClientOptionsConfig();
    if (optConf == nullptr) {
        return BIO_ERR;
    }
    WorkerMode mode = static_cast<WorkerMode>(std::stoul(argv[1]));
    if (mode == SEPARATES) {
        BioSdkConfigPtr mConfig = BioSdkConfig::Instance();
        if (mConfig == nullptr) {
            std::cout << "Create bio sdk configuration instance failed." << std::endl;
            delete optConf;
            return BIO_ERR;
        }
        auto result = mConfig->Initialize("/etc/boostio/");
        if (result != BIO_OK) {
            std::cout << "Failed to initialize sdk configuration, result: " << result << "." << std::endl;
            delete optConf;
            return BIO_ERR;
        }
        result = ConsoleGetSdkConfig(mConfig, optConf);
        if (result != BIO_OK) {
            std::cout << "Failed to get sdk configuration." << std::endl;
            delete optConf;
            return BIO_ERR;
        }
    }

    auto ret = BioService::Initialize(mode, *optConf);
    if (ret != RET_CACHE_OK) {
        std::cout << "Initialize bio service failed, ret " << ret << std::endl;
        delete optConf;
        return -1;
    }

    gDaemonRunning = true;
    std::cout << "BoostIO Console Start Success." << std::endl;
    while (gDaemonRunning) {
        sleep(NO_5);
    }
    delete optConf;
    return 0;
}