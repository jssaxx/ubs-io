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
#include "mms_config_instance.h"
#include "mms_log.h"
#include "mms_ip_util.h"
#include "mms_comm.h"

namespace ock {
namespace mms {
constexpr uint64_t GB_SIZE = 1024 * 1024 * 1024;
constexpr uint64_t MB_SIZE = 1024 * 1024;
static constexpr ValidatorTag COMMON_VALIDATOR_TAG = NO_0;
static constexpr ValidatorTag INTER_NODE_VALIDATOR_TAG = NO_1;

static BResult ParseCpuRange(const std::string &cpuRange, uint32_t maxCpuNo,
                             std::pair<uint32_t, uint32_t> &range)
{
    if (!StrUtil::StrToUint32Pair(cpuRange, range)) {
        LOG_ERROR("Invalid cpu range:" << cpuRange << ".");
        return MMS_INVALID_PARAM;
    }

    if ((range.first > range.second) || (range.second >= maxCpuNo)) {
        LOG_ERROR("Invalid cpu range:" << cpuRange << ", max cpu no:" << maxCpuNo << ".");
        return MMS_INVALID_PARAM;
    }
    return MMS_OK;
}

static BResult ParseCpuRangeList(const std::string &configName, const std::string &configValue, uint32_t maxCpuNo,
                                 std::vector<std::pair<uint32_t, uint32_t>> &ranges)
{
    std::vector<std::string> cpuRanges{};
    StrUtil::Split(configValue, ",", cpuRanges);
    uint32_t cpuCount = NO_0;
    ranges.clear();
    for (auto &item : cpuRanges) {
        std::pair<uint32_t, uint32_t> range{};
        auto ret = ParseCpuRange(item, maxCpuNo, range);
        if (ret != MMS_OK) {
            LOG_ERROR("Invalid cpu set, config name:" << configName << ", config value:" << configValue << ".");
            return ret;
        }

        uint32_t rangeCpuCount = range.second - range.first + NO_1;
        if ((rangeCpuCount > NO_256) || (cpuCount > NO_256 - rangeCpuCount)) {
            LOG_ERROR("Too many cpu items, config name:" << configName << ", config value:" << configValue << ".");
            return MMS_INVALID_PARAM;
        }
        cpuCount += rangeCpuCount;
        ranges.emplace_back(range);
    }

    if (cpuCount == NO_0) {
        LOG_ERROR("Empty cpu set, config name:" << configName << ", config value:" << configValue << ".");
        return MMS_INVALID_PARAM;
    }
    return MMS_OK;
}

static BResult ParseCpuRangeToIds(const std::string &configName, const std::string &configValue, uint32_t maxCpuNo,
                                  std::vector<uint16_t> &cpuIds)
{
    std::pair<uint32_t, uint32_t> cpuRange{};
    auto ret = ParseCpuRange(configValue, maxCpuNo, cpuRange);
    if (ret != MMS_OK) {
        LOG_ERROR("Invalid cpu set, config name:" << configName << ", config value:" << configValue << ".");
        return ret;
    }

    uint32_t cpuCount = cpuRange.second - cpuRange.first + NO_1;
    if (cpuCount > NO_256 || cpuRange.second > static_cast<uint32_t>(NO_MAX_VALUE16)) {
        LOG_ERROR("Invalid cpu count, config name:" << configName << ", config value:" << configValue << ".");
        return MMS_INVALID_PARAM;
    }

    cpuIds.clear();
    for (uint32_t cpuId = cpuRange.first; cpuId <= cpuRange.second; ++cpuId) {
        cpuIds.emplace_back(static_cast<uint16_t>(cpuId));
    }
    return MMS_OK;
}

void MmsConfig::LoadDefaultConf()
{
    LoadDefaultNetConf();

    /* load mem config */
    AddStrConf(MEM_NUMA_ID, VStrNotNull::Create(MEM_NUMA_ID.first));
    AddStrConf(MEM_NUMA_SIZE, VStrNotNull::Create(MEM_NUMA_SIZE.first));
    AddIntConf(MEM_VALUE_BLOCK_SIZE, VIntRange::Create(MEM_VALUE_BLOCK_SIZE.first, NO_1, NO_64));

    LoadDefaultSecurityConf();
    LoadDefaultBasicConf();
    AddIntConf(NOTIFY_SHM_QUEUE_DEPTH,
        VIntRange::Create(NOTIFY_SHM_QUEUE_DEPTH.first, NO_1024, NO_1048576));
    AddIntConf(NOTIFY_SHM_WORKER_NUM,
        VIntRange::Create(NOTIFY_SHM_WORKER_NUM.first, NO_1, NOTIFY_SHM_MAX_WORKERS));
    AddStrConf(NOTIFY_SHM_WORKER_CPUSET, VStrNotNull::Create(NOTIFY_SHM_WORKER_CPUSET.first));
    AddStrConf(NOTIFY_SHM_BUSY_POLLING, VStrBoolRange::Create(NOTIFY_SHM_BUSY_POLLING.first));
    AddStrConf(CRB_SEND_CPUSET, VStrNotNull::Create(CRB_SEND_CPUSET.first), INTER_NODE_VALIDATOR_TAG);
    LoadDefaultClusterConf();
}

void MmsConfig::LoadDefaultNetConf()
{
    AddStrConf(NET_RPC_IP_MASK, VIpv4MaskValidator::Create(NET_RPC_IP_MASK.first, false), INTER_NODE_VALIDATOR_TAG);
    AddIntConf(NET_RPC_PORT, VIntRange::Create(NET_RPC_PORT.first, NO_7201, NO_7800), INTER_NODE_VALIDATOR_TAG);
    AddIntConf(NET_MULTICAST_PORT, VIntRange::Create(NET_MULTICAST_PORT.first, NO_7201, NO_7800),
               INTER_NODE_VALIDATOR_TAG);

    AddStrConf(NET_RPC_PROTOCOL, VStrEnum::Create(NET_RPC_PROTOCOL.first, "tcp||rdma"), INTER_NODE_VALIDATOR_TAG);
    AddStrConf(NET_MULTICAST_PROTOCOL, VStrEnum::Create(NET_MULTICAST_PROTOCOL.first, "tcp||rdma"),
               INTER_NODE_VALIDATOR_TAG);
    AddIntConf(NET_RPC_CONNECT_COUNT, VIntRange::Create(NET_RPC_CONNECT_COUNT.first, NO_1, NO_16),
               INTER_NODE_VALIDATOR_TAG);
    AddStrConf(NET_RPC_BUSY_POLL_MODE, VStrBoolRange::Create(NET_RPC_BUSY_POLL_MODE.first),
               INTER_NODE_VALIDATOR_TAG);
    AddStrConf(NET_RPC_WORKER_GROUPS, VStrNotNull::Create(NET_RPC_WORKER_GROUPS.first), INTER_NODE_VALIDATOR_TAG);
    AddStrConf(NET_RPC_WORKER_GROUPS_CPUSET, VStrNotNull::Create(NET_RPC_WORKER_GROUPS_CPUSET.first),
               INTER_NODE_VALIDATOR_TAG);
    AddStrConf(NET_PUBLISHER_WORKER_CPUSET, VStrNotNull::Create(NET_PUBLISHER_WORKER_CPUSET.first),
               INTER_NODE_VALIDATOR_TAG);
    AddStrConf(NET_SUBSCRIBER_WORKER_CPUSET, VStrNotNull::Create(NET_SUBSCRIBER_WORKER_CPUSET.first),
               INTER_NODE_VALIDATOR_TAG);
    AddIntConf(NET_SUBSCRIBER_CONNECT_COUNT, VIntRange::Create(NET_SUBSCRIBER_CONNECT_COUNT.first, NO_1, NO_16),
               INTER_NODE_VALIDATOR_TAG);
    AddIntConf(NET_MESSAGE_MAX_BUFF_SIZE, VIntRange::Create(NET_MESSAGE_MAX_BUFF_SIZE.first, NO_1, NO_4096));

    AddStrConf(NET_IPC_BUSY_POLL_MODE, VStrBoolRange::Create(NET_IPC_BUSY_POLL_MODE.first));
    AddStrConf(NET_IPC_WORKER_GROUPS, VStrNotNull::Create(NET_IPC_WORKER_GROUPS.first));
    AddStrConf(NET_IPC_WORKER_GROUPS_CPUSET, VStrNotNull::Create(NET_IPC_WORKER_GROUPS_CPUSET.first));

    AddIntConf(NET_RECV_REQUEST_HANDLE_THREAD_NUM,
        VIntRange::Create(NET_RECV_REQUEST_HANDLE_THREAD_NUM.first, NO_8, NO_256), INTER_NODE_VALIDATOR_TAG);
    AddIntConf(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE,
        VIntRange::Create(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE.first, NO_1024, NO_65535), INTER_NODE_VALIDATOR_TAG);
}

void MmsConfig::LoadDefaultSecurityConf()
{
    AddStrConf(NET_TLS_ENABLE, VStrBoolRange::Create(NET_TLS_ENABLE.first));
    AddStrConf(NET_TLS_CERTIFICATION_PATH);
    AddStrConf(NET_TLS_CA_CERT_PATH);
    AddStrConf(NET_TLS_CA_CRL_PATH);
    AddStrConf(NET_TLS_PRIVATE_KEY_PATH);
    AddStrConf(NET_TLS_PRIVATE_KEY_PASSWORD_PATH);
    AddStrConf(NET_TLS_DECRYPTER_LIB_PATH);
    AddStrConf(NET_TLS_OPENSSL_LIB_PATH);
}

void MmsConfig::LoadDefaultBasicConf()
{
    AddStrConf(LOG_LEVEL, VStrEnum::Create(LOG_LEVEL.first, "error||warn||info||debug||trace"));
    AddStrConf(TRACE_SWITCH, VStrBoolRange::Create(TRACE_SWITCH.first));
    AddStrConf(CRC_SWITCH, VStrBoolRange::Create(CRC_SWITCH.first));
    AddStrConf(SEQUENCE_SWITCH, VStrBoolRange::Create(SEQUENCE_SWITCH.first));
    AddStrConf(MULTICAST_SWITCH, VStrBoolRange::Create(MULTICAST_SWITCH.first));
    AddStrConf(DATA_CHANGE_CALLBACK_SWITCH, VStrBoolRange::Create(DATA_CHANGE_CALLBACK_SWITCH.first));
    AddStrConf(ART_QUERY_SWITCH, VStrBoolRange::Create(ART_QUERY_SWITCH.first));
    AddStrConf(DEPLOYMENT_MODE, VStrEnum::Create(DEPLOYMENT_MODE.first, "separate||converge"));
}

void MmsConfig::LoadDefaultClusterConf()
{
    AddIntConf(CM_NODE_NUM, VIntRange::Create(CM_NODE_NUM.first, MIN_NODES_NUM, MAX_NODES_NUM));
    AddIntConf(CM_NODE_ID, VIntRange::Create(CM_NODE_ID.first, NO_0, NO_MAX_VALUE16));
    AddIntConf(CM_NODE_REGISTER_TIMEOUT, VIntRange::Create(CM_NODE_REGISTER_TIMEOUT.first, NO_10, NO_60));
    AddStrConf(CM_ZK_HOST, VStrNotNull::Create(CM_ZK_HOST.first));
}

BResult MmsConfig::AutoConfAfterLoadFromFile(const ConfigurationPtr &conf)
{
    auto ret = AutoConfigMem(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    ret = AutoConfigBasic(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    ret = AutoConfigCm(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    ret = AutoConfigNet(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    ret = AutoConfigNotifyShm(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    if (!IsSingleNode()) {
        ret = AutoConfigCrb(conf);
        ChkTrueNot(ret == MMS_OK, ret);
    }

    return ret;
}

BResult MmsConfig::AutoConfigNotifyShm(const ConfigurationPtr &conf)
{
    uint32_t queueDepth = static_cast<uint32_t>(conf->GetInt(NOTIFY_SHM_QUEUE_DEPTH.first));
    if (queueDepth < NO_1024 || (queueDepth & (queueDepth - NO_1)) != 0) {
        LOG_ERROR("Notify shm queue depth must be power of two, depth:" << queueDepth << ".");
        return MMS_INVALID_PARAM;
    }

    uint16_t workerNum = static_cast<uint16_t>(conf->GetInt(NOTIFY_SHM_WORKER_NUM.first));
    if (workerNum == 0 || workerNum > NOTIFY_SHM_MAX_WORKERS || queueDepth / workerNum < NO_1024) {
        LOG_ERROR("Invalid notify shm worker num:" << workerNum << ", queue depth:" << queueDepth << ".");
        return MMS_INVALID_PARAM;
    }
    std::pair<uint32_t, uint32_t> cpuRange{};
    auto ret = ParseCpuRange(conf->GetStr(NOTIFY_SHM_WORKER_CPUSET.first), GetDeviceCpuNum(), cpuRange);
    if (ret != MMS_OK || cpuRange.second - cpuRange.first + NO_1 < workerNum) {
        LOG_ERROR("Notify shm worker cpuset does not cover worker num:" << workerNum << ".");
        return MMS_INVALID_PARAM;
    }

    mNotifyShmConfig.queueDepth = queueDepth;
    mNotifyShmConfig.workerNum = workerNum;
    mNotifyShmConfig.workerCpuIds.clear();
    for (uint32_t cpuId = cpuRange.first; cpuId <= cpuRange.second && mNotifyShmConfig.workerCpuIds.size() < workerNum;
        ++cpuId) {
        mNotifyShmConfig.workerCpuIds.emplace_back(static_cast<uint16_t>(cpuId));
    }
    mNotifyShmConfig.busyPolling = conf->GetStr(NOTIFY_SHM_BUSY_POLLING.first) == "true";
    return MMS_OK;
}

BResult MmsConfig::AutoConfigCrb(const ConfigurationPtr &conf)
{
    std::vector<uint16_t> sendCpuIds{};
    auto ret = ParseCpuRangeToIds(
        CRB_SEND_CPUSET.first, conf->GetStr(CRB_SEND_CPUSET.first), GetDeviceCpuNum(), sendCpuIds);
    if (ret != MMS_OK) {
        return ret;
    }

    mCrbConfig.sendCpuIds.swap(sendCpuIds);
    return MMS_OK;
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
    uint32_t maxCpuNo = GetDeviceCpuNum();
    std::string publisherWorkerCpuStr = conf->GetStr(NET_PUBLISHER_WORKER_CPUSET.first);
    auto ret = ParseCpuRangeList(NET_PUBLISHER_WORKER_CPUSET.first, publisherWorkerCpuStr, maxCpuNo,
                                 mNetConfig.publisherWorkerCpuSets);
    if (ret != MMS_OK) {
        return ret;
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

    long subscriberCpuStart = mNetConfig.subscriberWorkerCpuSet.first;
    long subscriberCpuEnd = mNetConfig.subscriberWorkerCpuSet.second;
    long maxCpuNoLong = static_cast<long>(maxCpuNo);

    if (UNLIKELY((subscriberCpuStart > subscriberCpuEnd) || (subscriberCpuStart < NO_0) ||
                 (subscriberCpuEnd < NO_0) || (subscriberCpuEnd >= maxCpuNoLong))) {
        LOG_ERROR("Invalid cpu range:" << subscriberWorkerCpuStr << ".");
        return MMS_INVALID_PARAM;
    }

    mNetConfig.subscriberConnectCount = static_cast<uint16_t>(conf->GetInt(NET_SUBSCRIBER_CONNECT_COUNT.first));
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

BResult MmsConfig::AutoConfigNetAddress(const ConfigurationPtr &conf)
{
    mNetConfig.dataIpMask = conf->GetStr(NET_RPC_IP_MASK.first);
    mNetConfig.dataPort = conf->GetInt(NET_RPC_PORT.first);
    mNetConfig.multicastPort = conf->GetInt(NET_MULTICAST_PORT.first);

    std::vector<std::string> goodIps;
    if (!IpUtil::FilterIpByMask(mNetConfig.dataIpMask, goodIps) || goodIps.empty()) {
        LOG_ERROR("Failed to find ip with ip mask " << mNetConfig.dataIpMask);
        return MMS_ERR;
    }
    mNetConfig.dataIp = std::move(goodIps[0]);
    return MMS_OK;
}

BResult MmsConfig::AutoConfigRpcGroup(const ConfigurationPtr &conf)
{
    mNetConfig.rpcConnCount = conf->GetInt(NET_RPC_CONNECT_COUNT.first);
    mNetConfig.isRpcBusyPolling = conf->GetStr(NET_RPC_BUSY_POLL_MODE.first) == "true";
    mNetConfig.rpcWorkerGroups = conf->GetStr(NET_RPC_WORKER_GROUPS.first);
    mNetConfig.rpcWorkerGroupsCpuSet = conf->GetStr(NET_RPC_WORKER_GROUPS_CPUSET.first);

    std::vector<std::string> groupsVec;
    StrUtil::Split(mNetConfig.rpcWorkerGroups, ",", groupsVec);
    mNetConfig.rpcWorkerGroupsNum = static_cast<uint16_t>(groupsVec.size());
    if (mNetConfig.rpcWorkerGroupsNum <= MAX_GROUPS_NUM) {
        return MMS_OK;
    }

    LOG_ERROR("Invalid configuration with rpc groups num items: " << mNetConfig.rpcWorkerGroupsNum);
    return MMS_ERR;
}

BResult MmsConfig::AutoConfigIpcGroup(const ConfigurationPtr &conf)
{
    mNetConfig.isIpcBusyPolling = conf->GetStr(NET_IPC_BUSY_POLL_MODE.first) == "true";
    mNetConfig.ipcWorkerGroups = conf->GetStr(NET_IPC_WORKER_GROUPS.first);
    mNetConfig.ipcWorkerGroupsCpuSet = conf->GetStr(NET_IPC_WORKER_GROUPS_CPUSET.first);

    std::vector<std::string> groupsVec;
    StrUtil::Split(mNetConfig.ipcWorkerGroups, ",", groupsVec);
    mNetConfig.ipcWorkerGroupsNum = static_cast<uint16_t>(groupsVec.size());
    if (mNetConfig.ipcWorkerGroupsNum <= MAX_GROUPS_NUM) {
        return MMS_OK;
    }

    LOG_ERROR("Invalid configuration with ipc groups num items: " << mNetConfig.ipcWorkerGroupsNum);
    return MMS_ERR;
}

BResult MmsConfig::AutoConfigNet(const ConfigurationPtr &conf)
{
    auto ret = AutoConfigIpcGroup(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    mNetConfig.msgMaxBuffSize = static_cast<uint32_t>(conf->GetInt(NET_MESSAGE_MAX_BUFF_SIZE.first)) * KB_UNIT;
    ret = AutoConfigNetTls(conf);
    ChkTrueNot(ret == MMS_OK, ret);
    if (IsSingleNode()) {
        return MMS_OK;
    }

    ret = AutoConfigNetAddress(conf);
    ChkTrueNot(ret == MMS_OK, ret);
    ret = AutoConfigRpcGroup(conf);
    ChkTrueNot(ret == MMS_OK, ret);

    mNetConfig.multicastProtocol = conf->GetStr(NET_MULTICAST_PROTOCOL.first);
    mNetConfig.protocol = conf->GetStr(NET_RPC_PROTOCOL.first) == "rdma" ? NO_0 : NO_1;
    mNetConfig.handleRequestThreadNum = conf->GetInt(NET_RECV_REQUEST_HANDLE_THREAD_NUM.first);
    mNetConfig.handleRequestQueueSize = conf->GetInt(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE.first);
    if (UNLIKELY(AutoConfigNetMulticast(conf) != MMS_OK)) {
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
        mBasicConfig.logLevel = SPDLOG_LEVEL_TRACE;
    } else if (logLevel == "debug") {
        mBasicConfig.logLevel = SPDLOG_LEVEL_DEBUG;
    } else if (logLevel == "info") {
        mBasicConfig.logLevel = SPDLOG_LEVEL_INFO;
    } else if (logLevel == "warn") {
        mBasicConfig.logLevel = SPDLOG_LEVEL_WARN;
    } else if (logLevel == "error") {
        mBasicConfig.logLevel = SPDLOG_LEVEL_ERROR;
    } else {
        LOG_ERROR("Failed to load daemon log level config, invalid level " << logLevel);
        return MMS_ERR;
    }

    mBasicConfig.traceSwitch = conf->GetStr(TRACE_SWITCH.first) == "true";
    mBasicConfig.crcSwitch = conf->GetStr(CRC_SWITCH.first) == "true";
    mBasicConfig.sequenceSwitch = conf->GetStr(SEQUENCE_SWITCH.first) == "true";
    mBasicConfig.multicastSwitch = conf->GetStr(MULTICAST_SWITCH.first) == "true";
    mBasicConfig.dataChangeCallbackSwitch = conf->GetStr(DATA_CHANGE_CALLBACK_SWITCH.first) == "true";
    mBasicConfig.artQuerySwitch = conf->GetStr(ART_QUERY_SWITCH.first) == "true";

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
    auto errors = conf->Validate(COMMON_VALIDATOR_TAG);
    if (conf->GetInt(CM_NODE_NUM.first) != NO_1) {
        auto interNodeErrors = conf->Validate(INTER_NODE_VALIDATOR_TAG);
        errors.insert(errors.end(), interNodeErrors.begin(), interNodeErrors.end());
    }
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
