/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef MMS_CONFIG_INSTANCE_H
#define MMS_CONFIG_INSTANCE_H

#include "mms_config.h"
#include "mms_err.h"
#include "mms_types.h"

namespace ock {
namespace mms {
const auto LOG_LEVEL = std::make_pair("mms.log.level", "info");
const auto TRACE_SWITCH = std::make_pair("mms.trace.switch", "false");
const auto CRC_SWITCH = std::make_pair("mms.crc.switch", "false");
const auto SEQUENCE_SWITCH = std::make_pair("mms.sequence.switch", "false");
const auto MULTICAST_SWITCH = std::make_pair("mms.multicast.switch", "true");
const auto DEPLOYMENT_MODE = std::make_pair("mms.deployment.mode", "separate");
const auto CRB_SEND_CPU_START = std::make_pair("mms.crb.send.cpu.start", 68);

const auto NET_RPC_IP_MASK = std::make_pair("mms.net.rpc.ip_mask", "127.0.0.1/24");
const auto NET_RPC_PORT = std::make_pair("mms.net.rpc.listen_port", 7201);
const auto NET_MULTICAST_PORT = std::make_pair("mms.net.multicast.listen_port", 7501);
const auto NET_RPC_PROTOCOL = std::make_pair("mms.net.rpc.protocol", "tcp");
const auto NET_MULTICAST_PROTOCOL = std::make_pair("mms.net.multicast.protocol", "rdma");
const auto NET_RPC_CONNECT_COUNT = std::make_pair("mms.net.rpc.connect.count", 2);
const auto NET_RPC_BUSY_POLL_MODE = std::make_pair("mms.net.rpc.busy_polling_mode", "false");
const auto NET_RPC_WORKER_GROUPS = std::make_pair("mms.net.rpc.worker.groups", "2,2");
const auto NET_RPC_WORKER_GROUPS_CPUSET = std::make_pair("mms.net.rpc.worker.groups.cpuset", "10-12,13-14");
const auto NET_IPC_BUSY_POLL_MODE = std::make_pair("mms.net.ipc.busy_polling_mode", "false");
const auto NET_IPC_WORKER_GROUPS = std::make_pair("mms.net.ipc.worker.groups", "4,4");
const auto NET_IPC_WORKER_GROUPS_CPUSET = std::make_pair("mms.net.ipc.worker.groups.cpuset", "2-5,6-9");
const auto NET_PUBLISHER_WORKER_CPUSET = std::make_pair("mms.net.publisher.worker.cpuset", "10-17");
const auto NET_SUBSCRIBER_WORKER_CPUSET = std::make_pair("mms.net.subscriber.worker.cpuset", "18-18");
const auto NET_RECV_REQUEST_HANDLE_THREAD_NUM = std::make_pair("mms.net.request.executor.thread.num", 8);
const auto NET_RECV_REQUEST_HANDLE_QUEUE_SIZE = std::make_pair("mms.net.request.executor.queue.size", 1024);
const auto NET_MESSAGE_MAX_BUFF_SIZE = std::make_pair("mms.net.message.max_buff_size", 70);

const auto NET_TLS_ENABLE = std::make_pair("mms.net.tls.enable", "true");
const auto NET_TLS_CERTIFICATION_PATH = std::make_pair("mms.net.tls.certification.path", "");
const auto NET_TLS_CA_CERT_PATH = std::make_pair("mms.net.tls.ca.cert.path", "");
const auto NET_TLS_CA_CRL_PATH = std::make_pair("mms.net.tls.ca.crl.path", "");
const auto NET_TLS_PRIVATE_KEY_PATH = std::make_pair("mms.net.tls.private.key.path", "");
const auto NET_TLS_PRIVATE_KEY_PASSWORD_PATH = std::make_pair("mms.net.tls.private.key.password.path", "");
const auto NET_TLS_DECRYPTER_LIB_PATH = std::make_pair("mms.net.tls.decrypter.lib.path", "");
const auto NET_TLS_OPENSSL_LIB_PATH = std::make_pair("mms.net.tls.openssl.lib.path", "");

const auto MEM_NUMA_ID = std::make_pair("mms.mem.numa.id", "0,1");
const auto MEM_NUMA_SIZE = std::make_pair("mms.mem.numa.size", "32,32");
const auto MEM_VALUE_BLOCK_SIZE = std::make_pair("mms.mem.value.unit.size", 2);

const auto CM_NODE_NUM = std::make_pair("mms.cm.node.num", 3);
const auto CM_NODE_ID = std::make_pair("mms.cm.node.id", NO_MAX_VALUE16);
const auto CM_NODE_REGISTER_TIMEOUT = std::make_pair("mms.cm.register_timeout_sec", 30);
const auto CM_ZK_HOST = std::make_pair("mms.cm.zk_host", "127.0.0.1:2181");

class MmsConfig;
using MmsConfigPtr = Ref<MmsConfig>;

class MmsConfig : public Configuration {
public:
    struct NetConfig {
        std::string dataIpMask = "127.0.0.1/24";
        std::string dataIp = "127.0.0.1";
        uint16_t dataPort = 7300;
        uint16_t multicastPort = 7501;
        uint16_t protocol = 1;
        std::string multicastProtocol = "rdma";

        uint16_t rpcConnCount = 2;
        bool isRpcBusyPolling = false;
        std::string rpcWorkerGroups;
        std::string rpcWorkerGroupsCpuSet;
        uint16_t rpcWorkerGroupsNum;

        bool isIpcBusyPolling = false;
        std::string ipcWorkerGroups;
        std::string ipcWorkerGroupsCpuSet;
        uint16_t ipcWorkerGroupsNum;

        std::pair<long, long> publisherWorkerCpuSet;
        std::pair<long, long> subscriberWorkerCpuSet;

        uint16_t handleRequestThreadNum = 8;
        uint16_t handleRequestQueueSize = 1024;

        uint32_t msgMaxBuffSize = 70 * KB_UNIT;

        bool tlsEnable = true;
        std::string certificationPath{};
        std::string caCerPath{};
        std::string caCrlPath{};
        std::string privateKeyPath{};
        std::string privateKeyPasswordPath{};
        std::string decrypterLibPath{};
        std::string opensslLibDir{};
    };

    struct CmConfig {
        int32_t nodeNum = 3;
        int32_t nodeId = 0;
        int32_t registeredTimeoutSec = 30;
        std::string zkHost = "127.0.0.1:2181";
    };

    struct MemConfig {
        uint16_t numaNum;
        uint16_t numaId[MAX_NUMAS_NUM];
        uint64_t numaSize[MAX_NUMAS_NUM];
        uint32_t valueBlockSize;
    };

    struct BasicConfig {
        int32_t logLevel = 0;
        bool traceSwitch = false;
        bool crcSwitch = false;
        bool sequenceSwitch = false;
        bool multicastSwitch = false;
        bool isSeparateMode = true;
        int16_t crbSendCpuStart = 68;
    };

public:
    static const MmsConfigPtr &Instance()
    {
        static auto instance = MakeRef<MmsConfig>();
        return instance;
    }

    BResult Initialize(const std::string &homePath);

    void LoadDefaultConf() override;

    const MemConfig &GetMemConfig() const noexcept
    {
        return mMemConfig;
    }

    const NetConfig &GetNetConfig() const noexcept
    {
        return mNetConfig;
    }

    const CmConfig &GetCmConfig() const noexcept
    {
        return mCmConfig;
    }

    const BasicConfig &GetBasicConfig() const noexcept
    {
        return mBasicConfig;
    }

private:
    void DumpToLog();

    BResult AutoConfAfterLoadFromFile(const ConfigurationPtr &conf);

    BResult AutoConfigValueBlock(const ConfigurationPtr &conf);

    BResult AutoConfigMem(const ConfigurationPtr &conf);

    BResult AutoConfigNetMulticast(const ConfigurationPtr &conf);

    BResult AutoConfigNetTls(const ConfigurationPtr &conf);

    BResult AutoConfigNet(const ConfigurationPtr &conf);

    BResult AutoConfigCm(const ConfigurationPtr &conf);

    BResult AutoConfigBasic(const ConfigurationPtr &conf);

    BResult CheckNumaInfo(uint16_t numaNum, uint16_t numaId[]);

private:
    MemConfig mMemConfig;
    NetConfig mNetConfig;
    CmConfig mCmConfig;
    BasicConfig mBasicConfig;
    bool mInited{ false };
};
}
}
#endif // MMS_CONFIG_INSTANCE_H
