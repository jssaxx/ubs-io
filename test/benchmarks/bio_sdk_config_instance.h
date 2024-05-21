/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_SDK_CONFIG_INSTANCE_H
#define BOOSTIO_BIO_SDK_CONFIG_INSTANCE_H

#include "bio_config.h"
#include "bio_err.h"

namespace ock {
namespace bio {
const auto SDK_LOG_LEVEL = std::make_pair("bio.log.level", "info");

const auto SDK_LOG_TYPE = std::make_pair("bio.log.type", 0);
const auto SDK_LOG_FILE_PATH = std::make_pair("bio.sdk.log.path", "");

const auto SDK_NET_TLS_ENABLE_SWITCH = std::make_pair("bio.net.tls.enable.switch", "true");
const auto SDK_NET_TLS_CA_CERT_PATH = std::make_pair("bio.net.tls.ca.cert.path",
    "/path/CA/cacert.pem");
const auto SDK_NET_TLS_CA_CRL_PATH = std::make_pair("bio.net.tls.ca.crl.path", "");
const auto SDK_NET_TLS_CLIENT_CERT_PATH = std::make_pair("bio.net.tls.client.cert.path",
    "/path/client/clientcert.pem");
const auto SDK_NET_TLS_CLIENT_KEY_PATH = std::make_pair("bio.net.tls.client.key.path",
    "/path/client/clientkey.pem");
const auto SDK_NET_TLS_CLIENT_KEY_PASS_PATH = std::make_pair("bio.net.tls.client.key.pass.path",
    "/path/client/client.keypass");
const auto NET_HESC_CLIENT_KFS_MASTER_PATH = std::make_pair("bio.net.hesc.client.tls.kfs.master.path",
    "/path/client/master/kfsa");
const auto NET_HESC_CLIENT_KFS_STANDBY_PATH = std::make_pair("bio.net.hesc.client.tls.kfs.pass.standby.path",
    "/path/client/standby/kfsb");

class BioSdkConfig;
using BioSdkConfigPtr = Ref<BioSdkConfig>;

class BioSdkConfig : public Configuration {
public:
    struct NetConfig {
        bool enableTls = true;
        std::string tlsCaCertPath = "/path/CA/cacert.pem"; /* CA根证书 */
        std::string tlsCaCrlPath = "";                     /* 吊销列表文件，可选，如果无吊销证书可以不设置 */
        std::string tlsClientCertPath = "/path/client/servercert.pem"; /* server工作证书 */
        std::string tlsClientKeyPath = "/path/client/serverkey.pem"; /* server公钥 */
        std::string tlsClientKeyPassPath = "/path/client/server.keypass"; /* server端私钥密文文件 */
        std::string hseKfsMasterPath = "/path/client/master/kfsa"; /* hseceasy kfs master path */
        std::string hseKfsStandbyPath = "/path/client/standby/kfsb"; /* hseceasy kfs standby path  */
    };

public:
    static const BioSdkConfigPtr &Instance()
    {
        static auto instance = MakeRef<BioSdkConfig>();
        return instance;
    }

    BResult Initialize(const std::string &homePath);

    void LoadDefaultConf() override;

    const NetConfig &GetNetConfig() const noexcept
    {
        return mNetConfig;
    }

    const uint8_t &GetLogTypeConfig() const noexcept
    {
        return mlogType;
    }

    const std::string &GetLogFilePathConfig() const noexcept
    {
        return mlogFilePath;
    }

private:
    void DumpToLog();

    BResult AutoConfAfterLoadFromFile(const ConfigurationPtr &conf);

    BResult AutoConfigNet(const ConfigurationPtr &conf);

    BResult AutoConfigLog(const ConfigurationPtr &conf);

private:
    NetConfig mNetConfig;
    uint8_t mlogType;
    std::string mlogFilePath = "";
    bool mInited{ false };
};
}
}
#endif // BOOSTIO_BIO_CONFIG_INSTANCE_H