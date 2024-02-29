/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_CONFIG_INSTANCE_H
#define BOOSTIO_BIO_CONFIG_INSTANCE_H

#include "bio_config.h"
#include "bio_err.h"

namespace ock {
namespace bio {
const auto LOG_LEVEL = std::make_pair("bio.log.level", "info");

const auto NET_DATA_PROTOCOL = std::make_pair("bio.net.data.protocol", "tcp");
const auto NET_DATA_BUSY_POLL_MODE = std::make_pair("bio.net.data.busy_polling_mode", false);
const auto NET_DATA_WORKERS_COUNT = std::make_pair("bio.net.data.workers_count", 4);
const auto NET_DATA_IP_MASK = std::make_pair("bio.net.data.ip_mask", "127.0.0.1/24");
const auto NET_DATA_PORT = std::make_pair("bio.net.data.listen_port", 9898);
const auto NET_RECV_REQUEST_HANDLE_THREAD_NUM = std::make_pair("bio.net.request.executor.thread.num", 8);
const auto NET_RECV_REQUEST_HANDLE_QUEUE_SIZE = std::make_pair("bio.net.request.executor.queue.size", 1024);

const auto CM_INITIAL_NODE_NUM = std::make_pair("bio.cm.initial.nodes_count", 2);
const auto CM_NODE_NUM = std::make_pair("bio.cm.nodes_count", 2);
const auto CM_PT_NUM = std::make_pair("bio.cm.pts_count", 16);
const auto CM_GROUP_ID = std::make_pair("bio.cm.group.id", 1);
const auto CM_ZK_HOST = std::make_pair("bio.cm.zk_host", "127.0.0.1:2181");
const auto CM_NODE_REGISTER_TIMEOUT = std::make_pair("bio.cm.register_timeout_sec", 30);
const auto CM_NODE_REGISTER_PERM_TIMEOUT = std::make_pair("bio.cm.register_perm_timeout_sec", 60);

const auto CLIENT_LOCAL_MR_MB = std::make_pair("bio.client.buffer.size_in_mb", 512);

const auto SECURITY_ENABLED = std::make_pair("bio.security.enabled", false);
const auto SECURITY_CONF_PATH = std::make_pair("bio.security.conf", "bio_security.conf");

const auto SEGMENT_SIZE_MB = std::make_pair("bio.segment.size_in_mb", 2);

const auto MEM_CAPACITY_SIZE_GB = std::make_pair("bio.mem.size_in_gb", 8);

const auto DISK_CAPACITY_SIZE_GB = std::make_pair("bio.disk.size_in_gb", 8);

const auto DISK_CONF_PATH = std::make_pair("bio.disk.path", "xxx:xxx:xxx");

class BioConfig;
using BioConfigPtr = Ref<BioConfig>;

class BioConfig : public Configuration {
public:
    struct NetConfig {
        std::string dataIpMask = "127.0.0.1/24";
        std::string dataIp = "127.0.0.1";
        uint16_t dataPort = 9998;
        uint16_t protocol = 1;
        bool isBusyLoop = false;
        uint16_t dataWorkersCnt = 4;
        uint16_t handleRequestThreadNum = 8;
        uint16_t handleRequestQueueSize = 1024;
    };

    struct CmConfig {
        int32_t deployType = 1; // 1-Converged deployment, 0-Separated deployment
        int32_t initialNodeNum = 2;
        int32_t nodeNum = 2;
        int32_t ptNum = 2;
        int32_t registeredTimeoutSec = 30;
        int32_t registeredPermTimeoutSec = 60;
        int32_t groupId = 0;
        std::string zkHost = "127.0.0.1:2181";
    };

    struct DaemonConfig {
        int32_t logLevel = 0;
        uint32_t segment = 2097152; // 2MB
        uint64_t memCap = 8589934592; // 8GB
        uint64_t diskCap = 8589934592; // 8GB
        std::vector<std::string> diskList;
    };

    struct ClientConfig {
        uint64_t localMrSize = 0;
    };

    enum UnderFsType{
        UNDER_FS_LOCAL_FILE,
        UNDER_FS_CEPH
    };

    struct UnderFsConfig {
        UnderFsType underFsType;
    };

public:
    static const BioConfigPtr &Instance() {
        static auto instance = MakeRef<BioConfig>();
        return instance;
    }

    BResult Initialize(const std::string &homePath);

    void LoadDefaultConf() override;

    const NetConfig &GetNetConfig() const noexcept
    {
        return mNetConfig;
    }

    const CmConfig &GetCmConfig() const noexcept
    {
        return mCmConfig;
    }

    const DaemonConfig &GetDaemonConfig() const noexcept
    {
        return mDaemonConfig;
    }

    const ClientConfig &GetClientConfig() const noexcept
    {
        return mClientConfig;
    }

private:
    void DumpToLog();

    BResult AutoConfAfterLoadFromFile(const ConfigurationPtr &conf);

    BResult AutoConfigNet(const ConfigurationPtr &conf);

    BResult AutoConfigCm(const ConfigurationPtr &conf);

    BResult AutoConfigDaemon(const ConfigurationPtr &conf);

    BResult AutoConfigClient(const ConfigurationPtr &conf);

private:
    NetConfig mNetConfig;
    CmConfig mCmConfig;
    DaemonConfig mDaemonConfig;
    ClientConfig mClientConfig;
    bool mInited { false };
};
}
}
#endif // BOOSTIO_BIO_CONFIG_INSTANCE_H