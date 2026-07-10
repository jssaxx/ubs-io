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
const auto SDK_NET_TLS_CLIENT_KEY_PASS_PATH = std::make_pair("bio.net.tls.client.key.pass.path", "");
const auto SDK_NET_TLS_CLIENT_DECRYPTER_LIB_PATH = std::make_pair("bio.net.tls.client.decrypter.lib.path", "");

class BioSdkConfig;
using BioSdkConfigPtr = Ref<BioSdkConfig>;

class BioSdkConfig : public Configuration {
public:
    struct NetConfig {
        bool enableTls = true;
        std::string tlsCaCertPath = "/path/CA/cacert.pem";
        std::string tlsCaCrlPath;
        std::string tlsClientCertPath = "/path/client/servercert.pem";
        std::string tlsClientKeyPath = "/path/client/serverkey.pem";
        std::string tlsClientKeyPassPath = "";
        std::string decrypterLibPath;
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
#endif // BOOSTIO_BIO_SDK_CONFIG_INSTANCE_H