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

#include "bio_sdk_config_instance.h"
#include "bio_log.h"
#include "bio_ip_util.h"

namespace ock {
namespace bio {
void BioSdkConfig::LoadDefaultConf()
{
    /* load log config */
    AddIntConf(SDK_LOG_TYPE, VIntRange::Create(SDK_LOG_TYPE.first, 0, NO_2));
    AddStrConf(SDK_LOG_FILE_PATH);
    /* load net config for security */
    AddStrConf(SDK_NET_TLS_ENABLE_SWITCH, VStrBoolRange::Create(SDK_NET_TLS_ENABLE_SWITCH.first));
    AddStrConf(SDK_NET_TLS_CA_CERT_PATH);
    AddStrConf(SDK_NET_TLS_CA_CRL_PATH);
    AddStrConf(SDK_NET_TLS_CLIENT_CERT_PATH);
    AddStrConf(SDK_NET_TLS_CLIENT_KEY_PATH);
    AddStrConf(SDK_NET_TLS_CLIENT_KEY_PASS_PATH);
    AddStrConf(SDK_NET_TLS_CLIENT_DECRYPTER_LIB_PATH);
}

BResult BioSdkConfig::AutoConfAfterLoadFromFile(const ConfigurationPtr &conf)
{
    auto ret = AutoConfigLog(conf);
    ChkTrueNot(ret == BIO_OK, ret);

    ret = AutoConfigNet(conf);
    ChkTrueNot(ret == BIO_OK, ret);
    return ret;
}

BResult BioSdkConfig::AutoConfigNet(const ConfigurationPtr &conf)
{
    mNetConfig.enableTls = conf->GetStr(SDK_NET_TLS_ENABLE_SWITCH.first) == "true";
    mNetConfig.tlsCaCertPath = conf->GetStr(SDK_NET_TLS_CA_CERT_PATH.first);
    mNetConfig.tlsCaCrlPath = conf->GetStr(SDK_NET_TLS_CA_CRL_PATH.first);
    mNetConfig.tlsClientCertPath = conf->GetStr(SDK_NET_TLS_CLIENT_CERT_PATH.first);
    mNetConfig.tlsClientKeyPath = conf->GetStr(SDK_NET_TLS_CLIENT_KEY_PATH.first);
    mNetConfig.tlsClientKeyPassPath = conf->GetStr(SDK_NET_TLS_CLIENT_KEY_PASS_PATH.first);
    mNetConfig.decrypterLibPath = conf->GetStr(SDK_NET_TLS_CLIENT_DECRYPTER_LIB_PATH.first);
    return BIO_OK;
}

BResult BioSdkConfig::AutoConfigLog(const ConfigurationPtr &conf)
{
    mlogType = conf->GetInt(SDK_LOG_TYPE.first);
    mlogFilePath = conf->GetStr(SDK_LOG_FILE_PATH.first);
    return BIO_OK;
}

BResult BioSdkConfig::Initialize(const std::string &homePath)
{
    std::string configurePath = homePath + "bio_sdk_test.conf";
    if (mInited) {
        return BIO_OK;
    }

    ConfigurationPtr conf = Configuration::GetInstance<BioSdkConfig>();
    if (conf.Get() == nullptr) {
        std::cout << "create config object failed" << std::endl;
        return BIO_ERR;
    }

    if (!conf->ReadConf<BioSdkConfig>(configurePath)) {
        std::cout << "read config file " << configurePath << std::endl;
        return BIO_ERR;
    }

    std::ostringstream ossTmp;
    /* validate based on validation */
    auto errors = conf->Validate();
    if (!errors.empty()) {
        for (auto &item : errors) {
            ossTmp << item << "\n";
        }

        std::cout << "Invalid configuration with un-proper items: \n" << ossTmp.str() << "\n" << std::endl;
        return BIO_ERR;
    }

    /* auto config something */
    auto ret = AutoConfAfterLoadFromFile(conf);
    if (ret != BIO_OK) {
        std::cout << "Module load config file failed" << std::endl;
        return BIO_ERR;
    }

    /* validate security setting */
    /* do later */
    std::vector<std::string> moreErrors;
    mInited = true;
    return BIO_OK;
}
}
}