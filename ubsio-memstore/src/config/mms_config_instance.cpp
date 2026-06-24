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
#include <limits>

#include "mms_config_instance.h"
#include "mms_log.h"
#include "mms_ip_util.h"
#include "mms_comm.h"

namespace ock {
namespace mms {
constexpr uint64_t GB_SIZE = 1024 * 1024 * 1024;
constexpr uint64_t MB_SIZE = 1024 * 1024;
constexpr long MAX_MEM_NUMA_SIZE_GB = 4096;
constexpr int32_t MIN_CRB_SEND_CPU_START = -1;
constexpr int32_t MAX_CRB_SEND_CPU_START = std::numeric_limits<int16_t>::max();
void MmsConfig::LoadDefaultConf()
{
    /* don't allow empty */
    AddStrConf(NET_RPC_IP_MASK, VIpv4MaskValidator::Create(NET_RPC_IP_MASK.first, false));
    AddIntConf(NET_RPC_PORT, VIntRange::Create(NET_RPC_PORT.first, NO_7201, NO_7800));
    AddIntConf(NET_MULTICAST_PORT, VIntRange::Create(NET_MULTICAST_PORT.first, NO_7201, NO_7800));

    /* load mem config */
    AddStrConf(MEM_NUMA_ID, VStrNotNull::Create(MEM_NUMA_ID.first));
    AddStrConf(MEM_NUMA_SIZE, VStrNotNull::Create(MEM_NUMA_SIZE.first));
    AddIntConf(MEM_VALUE_BLOCK_SIZE, VIntRange::Create(MEM_VALUE_BLOCK_SIZE.first, NO_1, NO_64));

    /* load net config for rpc */
    AddStrConf(NET_RPC_PROTOCOL, VStrEnum::Create(NET_RPC_PROTOCOL.first, "tcp||rdma"));
    AddStrConf(NET_MULTICAST_PROTOCOL, VStrEnum::Create(NET_MULTICAST_PROTOCOL.first, "tcp||rdma"));
    AddIntConf(NET_RPC_CONNECT_COUNT, VIntRange::Create(NET_RPC_CONNECT_COUNT.first, NO_1, NO_16));
    AddStrConf(NET_RPC_BUSY_POLL_MODE, VStrBoolRange::Create(NET_RPC_BUSY_POLL_MODE.first));
    AddStrConf(NET_RPC_WORKER_GROUPS, VStrNotNull::Create(NET_RPC_WORKER_GROUPS.first));
    AddStrConf(NET_RPC_WORKER_GROUPS_CPUSET, VStrNotNull::Create(NET_RPC_WORKER_GROUPS_CPUSET.first));
    AddStrConf(NET_PUBLISHER_WORKER_CPUSET, VStrNotNull::Create(NET_PUBLISHER_WORKER_CPUSET.first));
    AddStrConf(NET_SUBSCRIBER_WORKER_CPUSET, VStrNotNull::Create(NET_SUBSCRIBER_WORKER_CPUSET.first));
    AddIntConf(NET_MESSAGE_MAX_BUFF_SIZE, VIntRange::Create(NET_MESSAGE_MAX_BUFF_SIZE.first, NO_1, NO_4096));

    AddStrConf(NET_TLS_ENABLE, VStrBoolRange::Create(NET_TLS_ENABLE.first));
    AddStrConf(NET_TLS_CERTIFICATION_PATH);
    AddStrConf(NET_TLS_CA_CERT_PATH);
    AddStrConf(NET_TLS_CA_CRL_PATH);
    AddStrConf(NET_TLS_PRIVATE_KEY_PATH);
    AddStrConf(NET_TLS_PRIVATE_KEY_PASSWORD_PATH);
    AddStrConf(NET_TLS_DECRYPTER_LIB_PATH);
    AddStrConf(NET_TLS_OPENSSL_LIB_PATH);

    /* load net config for ipc */
    AddStrConf(NET_IPC_BUSY_POLL_MODE, VStrBoolRange::Create(NET_IPC_BUSY_POLL_MODE.first));
    AddStrConf(NET_IPC_WORKER_GROUPS, VStrNotNull::Create(NET_IPC_WORKER_GROUPS.first));
    AddStrConf(NET_IPC_WORKER_GROUPS_CPUSET, VStrNotNull::Create(NET_IPC_WORKER_GROUPS_CPUSET.first));

    /* load net config for message shedule */
    AddIntConf(NET_RECV_REQUEST_HANDLE_THREAD_NUM,
        VIntRange::Create(NET_RECV_REQUEST_HANDLE_THREAD_NUM.first, NO_8, NO_256));
    AddIntConf(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE,
        VIntRange::Create(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE.first, NO_1024, NO_65535));

    /* load log info */
    AddStrConf(LOG_LEVEL, VStrEnum::Create(LOG_LEVEL.first, "error||warn||info||debug||trace"));

    /* htrace switch */
    AddStrConf(TRACE_SWITCH, VStrBoolRange::Create(TRACE_SWITCH.first));

    /* crc switch */
    AddStrConf(CRC_SWITCH, VStrBoolRange::Create(CRC_SWITCH.first));

    /* sequence switch */
    AddStrConf(SEQUENCE_SWITCH, VStrBoolRange::Create(SEQUENCE_SWITCH.first));

    /* multicast switch */
    AddStrConf(MULTICAST_SWITCH, VStrBoolRange::Create(MULTICAST_SWITCH.first));

    /* deployment mode */
    AddStrConf(DEPLOYMENT_MODE, VStrEnum::Create(DEPLOYMENT_MODE.first, "separate||converge"));

    /* crb scheduler */
    AddIntConf(CRB_SEND_CPU_START,
               VIntRange::Create(CRB_SEND_CPU_START.first, MIN_CRB_SEND_CPU_START, MAX_CRB_SEND_CPU_START));

    /* load cluster manager config */
    AddIntConf(CM_NODE_NUM, VIntRange::Create(CM_NODE_NUM.first, MIN_NODES_NUM, MAX_NODES_NUM));
    AddIntConf(CM_NODE_ID, VIntRange::Create(CM_NODE_ID.first, NO_0, NO_MAX_VALUE16));
    AddIntConf(CM_NODE_REGISTER_TIMEOUT, VIntRange::Create(CM_NODE_REGISTER_TIMEOUT.first, NO_10, NO_60));
    AddStrConf(CM_ZK_HOST, VStrNotNull::Create(CM_ZK_HOST.first));
}

BResult MmsConfig::AutoConfAfterLoadFromFile(const ConfigurationPtr &conf)
{
    auto ret = AutoConfigMem(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    ret = AutoConfigNet(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    ret = AutoConfigBasic(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    ret = AutoConfigCm(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    return ret;
}

BResult MmsConfig::AutoConfigValueBlock(const ConfigurationPtr &conf)
{
    mMemConfig.valueBlockSize = conf->GetInt(MEM_VALUE_BLOCK_SIZE.first) * KB_UNIT;
    return MMS_OK;
}

BResult MmsConfig::AutoConfigMem(const ConfigurationPtr &conf)
{
    std::string numaIds = conf->GetStr(MEM_NUMA_ID.first);
    std::vector<std::string> numaIdsVec;
    StrUtil::Split(numaIds, ",", numaIdsVec);

    std::string numaSizes = conf->GetStr(MEM_NUMA_SIZE.first);
    std::vector<std::string> numaSizesVec;
    StrUtil::Split(numaSizes, ",", numaSizesVec);

    if (numaIdsVec.size() != numaSizesVec.size() || numaIdsVec.size() > MAX_NUMAS_NUM) {
        LOG_ERROR("Invalid configuration with numa items: " << numaIdsVec.size());
        return MMS_ERR;
    }

    long value;
    uint32_t index;
    for (index = 0; index < numaIdsVec.size(); index++) {
        if (UNLIKELY(!StrUtil::StrToLong(numaIdsVec[index], value))) {
            return MMS_INVALID_PARAM;
        }
        mMemConfig.numaId[index] = static_cast<uint16_t>(value);
    }

    for (index = 0; index < numaSizesVec.size(); index++) {
        if (UNLIKELY(!StrUtil::StrToLong(numaSizesVec[index], value))) {
            return MMS_INVALID_PARAM;
        }
        if (UNLIKELY(value < static_cast<long>(NO_0) || value > MAX_MEM_NUMA_SIZE_GB)) {
            LOG_ERROR("Invalid numa size:" << value << ", max size:" << MAX_MEM_NUMA_SIZE_GB << "GB.");
            return MMS_INVALID_PARAM;
        }
        mMemConfig.numaSize[index] = static_cast<uint64_t>(value) * IO_SIZE_1G; // GB换算成字节
    }

    mMemConfig.numaNum = static_cast<uint16_t>(numaIdsVec.size());

    auto ret = CheckNumaInfo(mMemConfig.numaNum, mMemConfig.numaId);
    if (ret != MMS_OK) {
        return ret;
    }

    if (UNLIKELY(AutoConfigValueBlock(conf) != MMS_OK)) {
        return MMS_INVALID_PARAM;
    }

    return MMS_OK;
}

BResult MmsConfig::AutoConfigNetMulticast(const ConfigurationPtr &conf)
{
    std::string publisherWorkerCpuStr = conf->GetStr(NET_PUBLISHER_WORKER_CPUSET.first);
    std::vector<std::string> cpuSet{};
    StrUtil::Split(publisherWorkerCpuStr, "-", cpuSet);
    if (cpuSet.size() != NO_2) {
        LOG_ERROR("Invalid publisher cpu set:" << publisherWorkerCpuStr << ".");
        return MMS_INVALID_PARAM;
    }

    if (UNLIKELY(!StrUtil::StrToLong(cpuSet[NO_0], mNetConfig.publisherWorkerCpuSet.first) ||
                 !StrUtil::StrToLong(cpuSet[NO_1], mNetConfig.publisherWorkerCpuSet.second))) {
        LOG_ERROR("Invalid cpu range:" << publisherWorkerCpuStr << ".");
        return MMS_INVALID_PARAM;
    }

    uint16_t maxCpuNo = GetDeviceCpuNum();
    long cpuStart = mNetConfig.publisherWorkerCpuSet.first;
    long cpuEnd = mNetConfig.publisherWorkerCpuSet.second;

    if (UNLIKELY((cpuStart > cpuEnd) || (cpuStart < NO_0) || (cpuEnd < NO_0) || (cpuEnd >= maxCpuNo))) {
        LOG_ERROR("Invalid cpu range:" << publisherWorkerCpuStr << ".");
        return MMS_INVALID_PARAM;
    }

    std::string subscriberWorkerCpuStr = conf->GetStr(NET_SUBSCRIBER_WORKER_CPUSET.first);
    std::vector<std::string> cpuRange{};
    StrUtil::Split(subscriberWorkerCpuStr, "-", cpuRange);
    if (cpuRange.size() != NO_2) {
        LOG_ERROR("Invalid subscriber cpu set:" << subscriberWorkerCpuStr << ".");
        return MMS_INVALID_PARAM;
    }

    if (UNLIKELY(!StrUtil::StrToLong(cpuRange[NO_0], mNetConfig.subscriberWorkerCpuSet.first) ||
                 !StrUtil::StrToLong(cpuRange[NO_1], mNetConfig.subscriberWorkerCpuSet.second))) {
        LOG_ERROR("Invalid cpu range:" << subscriberWorkerCpuStr << ".");
        return MMS_INVALID_PARAM;
    }

    long SubscriberCpuStart = mNetConfig.subscriberWorkerCpuSet.first;
    long SubscriberCpuEnd = mNetConfig.subscriberWorkerCpuSet.second;

    if (UNLIKELY((SubscriberCpuStart > SubscriberCpuEnd) || (SubscriberCpuStart < NO_0) || (SubscriberCpuEnd < NO_0) ||
                 (SubscriberCpuEnd >= maxCpuNo))) {
        LOG_ERROR("Invalid cpu range:" << subscriberWorkerCpuStr << ".");
        return MMS_INVALID_PARAM;
    }

    return MMS_OK;
}

BResult MmsConfig::AutoConfigNetTls(const ConfigurationPtr &conf)
{
    mNetConfig.tlsEnable = conf->GetStr(NET_TLS_ENABLE.first) == "true";
    mNetConfig.certificationPath = conf->GetStr(NET_TLS_CERTIFICATION_PATH.first);
    mNetConfig.caCerPath = conf->GetStr(NET_TLS_CA_CERT_PATH.first);
    mNetConfig.caCrlPath = conf->GetStr(NET_TLS_CA_CRL_PATH.first);
    mNetConfig.privateKeyPath = conf->GetStr(NET_TLS_PRIVATE_KEY_PATH.first);
    mNetConfig.privateKeyPasswordPath = conf->GetStr(NET_TLS_PRIVATE_KEY_PASSWORD_PATH.first);
    mNetConfig.decrypterLibPath = conf->GetStr(NET_TLS_DECRYPTER_LIB_PATH.first);
    mNetConfig.opensslLibDir = conf->GetStr(NET_TLS_OPENSSL_LIB_PATH.first);

    if (!mNetConfig.tlsEnable) {
        return MMS_OK;
    }

    bool checkCaPath = FileUtil::CanonicalPath(mNetConfig.certificationPath) &&
                       FileUtil::CanonicalPath(mNetConfig.caCerPath) &&
                       FileUtil::CanonicalPath(mNetConfig.privateKeyPath) &&
                       FileUtil::CanonicalPath(mNetConfig.privateKeyPasswordPath) &&
                       FileUtil::CanonicalPath(mNetConfig.decrypterLibPath);
    if (!checkCaPath) {
        LOG_ERROR("Check path failed.");
        return MMS_INVALID_PARAM;
    }

    return MMS_OK;
}

BResult MmsConfig::AutoConfigNet(const ConfigurationPtr &conf)
{
    /* auto config cm port and ip mask */
    mNetConfig.dataIpMask = conf->GetStr(NET_RPC_IP_MASK.first);
    mNetConfig.dataPort = conf->GetInt(NET_RPC_PORT.first);
    mNetConfig.multicastPort = conf->GetInt(NET_MULTICAST_PORT.first);

    /* fetch ip from ip mask, example:x.x.x.x/24 to x.x.x.x */
    std::vector<std::string> goodIps;
    if (!IpUtil::FilterIpByMask(mNetConfig.dataIpMask, goodIps) || goodIps.empty()) {
        LOG_ERROR("Failed to find ip with ip mask " << mNetConfig.dataIpMask);
        return MMS_ERR;
    }
    mNetConfig.dataIp = std::move(goodIps[0]);

    mNetConfig.multicastProtocol = conf->GetStr(NET_MULTICAST_PROTOCOL.first);
    std::string protocol = conf->GetStr(NET_RPC_PROTOCOL.first);
    if (protocol == "rdma") {
        mNetConfig.protocol = 0;
    } else {
        mNetConfig.protocol = 1;
    }

    mNetConfig.msgMaxBuffSize = static_cast<uint32_t>(conf->GetInt(NET_MESSAGE_MAX_BUFF_SIZE.first)) * KB_UNIT;
    mNetConfig.rpcConnCount = conf->GetInt(NET_RPC_CONNECT_COUNT.first);
    mNetConfig.isRpcBusyPolling = conf->GetStr(NET_RPC_BUSY_POLL_MODE.first) == "true";
    mNetConfig.rpcWorkerGroups = conf->GetStr(NET_RPC_WORKER_GROUPS.first);
    mNetConfig.rpcWorkerGroupsCpuSet = conf->GetStr(NET_RPC_WORKER_GROUPS_CPUSET.first);
    std::vector<std::string> groupsVec;
    StrUtil::Split(mNetConfig.rpcWorkerGroups, ",", groupsVec);
    mNetConfig.rpcWorkerGroupsNum = static_cast<uint16_t>(groupsVec.size());
    if (mNetConfig.rpcWorkerGroupsNum > MAX_GROUPS_NUM) {
        LOG_ERROR("Invalid configuration with rpc groups num items: " << mNetConfig.rpcWorkerGroupsNum);
        return MMS_ERR;
    }
    groupsVec.clear();
    mNetConfig.isIpcBusyPolling = conf->GetStr(NET_IPC_BUSY_POLL_MODE.first) == "true";
    mNetConfig.ipcWorkerGroups = conf->GetStr(NET_IPC_WORKER_GROUPS.first);
    mNetConfig.ipcWorkerGroupsCpuSet = conf->GetStr(NET_IPC_WORKER_GROUPS_CPUSET.first);
    StrUtil::Split(mNetConfig.ipcWorkerGroups, ",", groupsVec);
    mNetConfig.ipcWorkerGroupsNum = static_cast<uint16_t>(groupsVec.size());
    if (mNetConfig.ipcWorkerGroupsNum > MAX_GROUPS_NUM) {
        LOG_ERROR("Invalid configuration with ipc groups num items: " << mNetConfig.ipcWorkerGroupsNum);
        return MMS_ERR;
    }

    mNetConfig.handleRequestThreadNum = conf->GetInt(NET_RECV_REQUEST_HANDLE_THREAD_NUM.first);
    mNetConfig.handleRequestQueueSize = conf->GetInt(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE.first);

    if (AutoConfigNetMulticast(conf) != MMS_OK) {
        return MMS_INVALID_PARAM;
    }

    if (AutoConfigNetTls(conf) != MMS_OK) {
        return MMS_INVALID_PARAM;
    }

    return MMS_OK;
}

BResult MmsConfig::AutoConfigCm(const ConfigurationPtr &conf)
{
    mCmConfig.nodeNum = conf->GetInt(CM_NODE_NUM.first);
    mCmConfig.nodeId = conf->GetInt(CM_NODE_ID.first);
    mCmConfig.registeredTimeoutSec = conf->GetInt(CM_NODE_REGISTER_TIMEOUT.first);
    mCmConfig.zkHost = conf->GetStr(CM_ZK_HOST.first);
    return MMS_OK;
}

BResult MmsConfig::AutoConfigBasic(const ConfigurationPtr &conf)
{
    auto logLevel = conf->GetStr(LOG_LEVEL.first);
    if (logLevel == "trace") {
        mBasicConfig.logLevel = MMSLOG_LEVEL_TRACE;
    } else if (logLevel == "debug") {
        mBasicConfig.logLevel = MMSLOG_LEVEL_DEBUG;
    } else if (logLevel == "info") {
        mBasicConfig.logLevel = MMSLOG_LEVEL_INFO;
    } else if (logLevel == "warn") {
        mBasicConfig.logLevel = MMSLOG_LEVEL_WARN;
    } else if (logLevel == "error") {
        mBasicConfig.logLevel = MMSLOG_LEVEL_ERROR;
    } else {
        LOG_ERROR("Failed to load daemon log level config, invalid level " << logLevel);
        return MMS_ERR;
    }

    mBasicConfig.traceSwitch = conf->GetStr(TRACE_SWITCH.first) == "true";
    mBasicConfig.crcSwitch = conf->GetStr(CRC_SWITCH.first) == "true";
    mBasicConfig.sequenceSwitch = conf->GetStr(SEQUENCE_SWITCH.first) == "true";
    mBasicConfig.multicastSwitch = conf->GetStr(MULTICAST_SWITCH.first) == "true";
    mBasicConfig.crbSendCpuStart = static_cast<int16_t>(conf->GetInt(CRB_SEND_CPU_START.first));

    auto deployment = conf->GetStr(DEPLOYMENT_MODE.first);
    if (deployment == "separate") {
        mBasicConfig.isSeparateMode = true;
    } else if (deployment == "converge") {
        mBasicConfig.isSeparateMode = false;
    } else {
        LOG_ERROR("Failed to load deployment mode config, invalid mode " << deployment);
        return MMS_ERR;
    }

    return MMS_OK;
}

BResult MmsConfig::CheckNumaInfo(uint16_t numaNum, uint16_t numaId[])
{
    if (NumaAvailable() == -1) {
        if (numaNum == NO_1 && numaId[0] == 0) {
            return MMS_OK;
        }
        LOG_ERROR("NUMA is not available on this system.");
        return MMS_ERR;
    }
    int maxNum = GetNumaNodeNum();
    uint16_t index;
    for (index = 0; index < numaNum; index++) {
        if (numaId[index] >= static_cast<uint16_t>(maxNum)) {
            LOG_ERROR("This system has NUMA nodes:" << maxNum);
            return MMS_ERR;
        }
    }
    return MMS_OK;
}

BResult MmsConfig::Initialize(const std::string &homePath)
{
    std::string configurePath = homePath + "/mms.conf";
    LOG_INFO("start to read config file : " << configurePath);

    if (mInited) {
        return MMS_OK;
    }

    ConfigurationPtr conf = Configuration::GetInstance<MmsConfig>();
    if (conf.Get() == nullptr) {
        LOG_INFO("create config object failed");
        return MMS_ERR;
    }

    if (!conf->ReadConf<MmsConfig>(configurePath)) {
        LOG_ERROR("read config file " << configurePath << " failed");
        return MMS_ERR;
    }

    DumpToLog();

    std::ostringstream ossTmp;
    /* validate based on validation */
    auto errors = conf->Validate();
    if (!errors.empty()) {
        for (auto &item : errors) {
            ossTmp << item << "\n";
        }

        LOG_ERROR("Invalid configuration with un-proper items: \n" << ossTmp.str() << "\n");
        return MMS_ERR;
    }

    /* auto config something */
    auto ret = AutoConfAfterLoadFromFile(conf);
    if (ret != MMS_OK) {
        LOG_ERROR("Module load config file failed");
        return MMS_ERR;
    }

    /* validate security setting */
    /* do later */
    std::vector<std::string> moreErrors;
    mInited = true;
    return MMS_OK;
}

void MmsConfig::DumpToLog()
{
    ConfigurationPtr conf = Configuration::GetInstance<MmsConfig>();
    if (conf.Get() == nullptr) {
        LOG_INFO("Load config object failed");
        return;
    }

    KVReader reader;
    conf->Dump(reader);

    std::lock_guard<std::mutex> guard(mMutex);
    LOG_INFO("Configuration Dump:");
    for (uint32_t i = 0; i < reader.Size(); i++) {
        std::string key;
        std::string value;
        reader.GetI(i, key, value);
        if (key.find("tls") == std::string::npos) {
            LOG_INFO("" << key << " = " << value);
        }
    }
    return;
}
}
}
