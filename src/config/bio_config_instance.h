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
const auto NET_RPC_DATA_BUSY_POLL_MODE = std::make_pair("bio.net.rpc.data.busy_polling_mode", "false");
const auto NET_RPC_DATA_WORKERS_COUNT = std::make_pair("bio.net.rpc.data.workers_count", 4);
const auto NET_IPC_DATA_BUSY_POLL_MODE = std::make_pair("bio.net.ipc.data.busy_polling_mode", "false");
const auto NET_IPC_DATA_WORKERS_COUNT = std::make_pair("bio.net.ipc.data.workers_count", 4);
const auto NET_DATA_IP_MASK = std::make_pair("bio.net.data.ip_mask", "127.0.0.1/24");
const auto NET_DATA_PORT = std::make_pair("bio.net.data.listen_port", 7201);
const auto NET_RECV_REQUEST_HANDLE_THREAD_NUM = std::make_pair("bio.net.request.executor.thread.num", 8);
const auto NET_RECV_REQUEST_HANDLE_QUEUE_SIZE = std::make_pair("bio.net.request.executor.queue.size", 1024);
const auto NET_TLS_ENABLE_SWITCH = std::make_pair("bio.net.tls.enable.switch", "true");
const auto NET_TLS_CA_CERT_PATH = std::make_pair("bio.net.tls.ca.cert.path", "/path/CA/cacert.pem");
const auto NET_TLS_CA_CRL_PATH = std::make_pair("bio.net.tls.ca.crl.path", "");
const auto NET_TLS_SERVER_CERT_PATH = std::make_pair("bio.net.tls.server.cert.path", "/path/server/servercert.pem");
const auto NET_TLS_SERVER_KEY_PATH = std::make_pair("bio.net.tls.server.key.path", "/path/server/serverkey.pem");
const auto NET_TLS_SERVER_KEY_PASS_PATH = std::make_pair("bio.net.tls.server.key.pass.path",
    "/path/server/server.keypass");
const auto NET_HESC_SERVER_KFS_MASTER_PATH = std::make_pair("bio.net.hesc.server.tls.kfs.master.path",
    "/path/server/master/kfsa");
const auto NET_HESC_SERVER_KFS_STANDBY_PATH = std::make_pair("bio.net.hesc.server.tls.kfs.pass.standby.path",
    "/path/server/standby/kfsb");

const auto CM_INITIAL_NODE_NUM = std::make_pair("bio.cm.initial.nodes_count", 2);
const auto CM_PT_NUM = std::make_pair("bio.cm.pts_count", 16);
const auto CM_ZK_HOST = std::make_pair("bio.cm.zk_host", "127.0.0.1:2181");
const auto CM_NODE_REGISTER_TIMEOUT = std::make_pair("bio.cm.register_timeout_sec", 30);
const auto CM_NODE_REGISTER_PERM_TIMEOUT = std::make_pair("bio.cm.register_perm_timeout_sec", 60);

const auto SEGMENT_SIZE_MB = std::make_pair("bio.segment.size_in_mb", 4);

const auto MEM_CAPACITY_SIZE_GB = std::make_pair("bio.mem.size_in_gb", 50);

const auto DISK_CONF_PATH = std::make_pair("bio.disk.path", "xxx:xxx:xxx");

const auto WCACHE_EVICT_WATER_LEVEL = std::make_pair("bio.wcache.evict_water_level", 0);

const auto RCACHE_EVICT_WATER_LEVEL = std::make_pair("bio.rcache.evict_water_level", 90);

const auto MEM_READ_WRITE_RATIO = std::make_pair("bio.cache.mem_read_write_ratio", "5:5");

const auto DISK_READ_WRITE_RATIO = std::make_pair("bio.cache.disk_read_write_ratio", "5:5");

const auto WORK_SCENE = std::make_pair("bio.work.scene", "none");
const auto WORK_IO_ALIGNSIZE = std::make_pair("bio.work.io.alignsize", 1);
const auto WORK_IO_TIMEOUT = std::make_pair("bio.work.io.timeout", 60);
const auto WORK_NET_TIMEOUT = std::make_pair("bio.work.net.timeout", 20);

const auto UNDERFS_CEPH_CFG_PATH = std::make_pair("bio.underfs.ceph.cfg.path", "/etc/ceph/ceph.conf");
const auto UNDERFS_CEPH_CLUSTER = std::make_pair("bio.underfs.ceph.cluster", "ceph");
const auto UNDERFS_CEPH_USER = std::make_pair("bio.underfs.ceph.user", "client.admin");
const auto UNDERFS_CEPH_POOL = std::make_pair("bio.underfs.ceph.pool", "0:jfspool1,1:jfspool2");

class BioConfig;
using BioConfigPtr = Ref<BioConfig>;

class BioConfig : public Configuration {
public:
    struct NetConfig {
        std::string dataIpMask = "127.0.0.1/24";
        std::string dataIp = "127.0.0.1";
        uint16_t dataPort = 9998;
        uint16_t protocol = 1;
        bool isRpcBusyLoop = false;
        uint16_t rpcDataWorkersCnt = 4;
        bool isIpcBusyLoop = false;
        uint16_t ipcDataWorkersCnt = 4;
        uint16_t handleRequestThreadNum = 8;
        uint16_t handleRequestQueueSize = 1024;
        bool enableTls = true;
        std::string tlsCaCertPath = "/path/CA/cacert.pem"; /* CA根证书 */
        std::string tlsCaCrlPath = "";                     /* 吊销列表文件，可选，如果无吊销证书可以不设置 */
        std::string tlsServerCertPath = "/path/server/servercert.pem"; /* server工作证书 */
        std::string tlsServerKeyPath = "/path/server/serverkey.pem"; /* server公钥 */
        std::string tlsServerKeyPassPath = "/path/server/server.keypass"; /* server端私钥密文文件 */
        std::string hseKfsMasterPath = "/path/client/master/kfsa"; /* hseceasy kfs master path */
        std::string hseKfsStandbyPath = "/path/client/standby/kfsb"; /* hseceasy kfs standby path  */
    };

    struct CmConfig {
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
        uint32_t segment = 4194304;    // 4MB
        uint64_t memCap = 53687091200; // 50GB
        uint64_t wcacheMemEvictLevel = 0;
        uint64_t wcacheDiskEvictLevel = 0;
        uint64_t rcacheMemEvictLevel = 90;
        uint64_t rcacheDiskEvictLevel = 90;
        std::string memReadWriteRatio = "5:5";
        long memReadRatio = 5;
        long memWriteRatio = 5;
        std::string diskReadWriteRatio = "5:5";
        long diskReadRatio = 5;
        long diskWriteRatio = 5;
        std::vector<std::string> diskList;
        std::vector<int64_t> diskCaps;
        uint32_t workScene = 0;
        uint32_t workIoAlignSize = 1;
        uint32_t workIoTimeOut = 60;
        uint32_t workNetTimeOut = 20;
    };

    struct ClientConfig {
        uint64_t localMrSize = 0;
    };

    enum UnderFsType {
        UNDER_FS_LOCAL_FILE,
        UNDER_FS_CEPH
    };

    struct UnderFsConfig {
        UnderFsType underFsType;
        std::string cfgPath;
        std::string cluster;
        std::string user;
        std::unordered_map<uint64_t, std::string> pools;
    };

public:
    static const BioConfigPtr &Instance()
    {
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

    const UnderFsConfig &GetUnderFsConfig() const noexcept
    {
        return mUnderFsConfig;
    }

    uint64_t ModifyConfigEvictWaterLevel(uint8_t tier, uint64_t level);

    std::string ModifyConfigMemReadWriteRatio(const std::string &ratios);

    std::string ModifyConfigDiskReadWriteRatio(const std::string &ratios);

private:
    void DumpToLog();

    BResult AutoConfAfterLoadFromFile(const ConfigurationPtr &conf);

    BResult AutoConfigNet(const ConfigurationPtr &conf);

    BResult AutoConfigCm(const ConfigurationPtr &conf);

    BResult AutoConfigDaemon(const ConfigurationPtr &conf);

    BResult AutoConfigClient(const ConfigurationPtr &conf);

    BResult AutoConfigUnderFs(const ConfigurationPtr &conf);

private:
    NetConfig mNetConfig;
    CmConfig mCmConfig;
    DaemonConfig mDaemonConfig;
    ClientConfig mClientConfig;
    UnderFsConfig mUnderFsConfig;
    bool mInited{ false };
};
}
}
#endif // BOOSTIO_BIO_CONFIG_INSTANCE_H