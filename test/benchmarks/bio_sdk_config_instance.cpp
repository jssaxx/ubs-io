/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include "bio_sdk_config_instance.h"
#include "bio_log.h"
#include "bio_ip_util.h"

namespace ock {
namespace bio {
void BioSdkConfig::LoadDefaultConf()
{
    LOG_INFO("Load default conf");
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
    AddStrConf(NET_HESC_CLIENT_KFS_MASTER_PATH);
    AddStrConf(NET_HESC_CLIENT_KFS_STANDBY_PATH);
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
    mNetConfig.hseKfsMasterPath = conf->GetStr(NET_HESC_CLIENT_KFS_MASTER_PATH.first);
    mNetConfig.hseKfsStandbyPath = conf->GetStr(NET_HESC_CLIENT_KFS_STANDBY_PATH.first);
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
    std::string configurePath = homePath + "/conf/bio_sdk_test.conf";
    std::cout << "start to read config file : " << configurePath << std::endl;

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

    DumpToLog();

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

void BioSdkConfig::DumpToLog()
{
    ConfigurationPtr conf = Configuration::GetInstance<BioSdkConfig>();
    if (conf.Get() == nullptr) {
        std::cout << "Load config object failed" << std::endl;
        return;
    }

    KVReader reader;
    conf->Dump(reader);

    std::lock_guard<std::mutex> guard(mMutex);
    std::ostringstream ossTmp;

    for (uint32_t i = 0; i < reader.Size(); i++) {
        std::string key;
        std::string value;
        reader.GetI(i, key, value);
        if ((key.find("tls") == std::string::npos) && (key.find("log.path") == std::string::npos)) {
            ossTmp << " " << key << " = " << value << std::endl;
        }
    }
    std::cout << ossTmp.str() << std::endl;
}
}
}
