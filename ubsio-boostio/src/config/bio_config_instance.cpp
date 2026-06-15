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

#include <algorithm>
#include <cstdio>
#include <map>
#include <sstream>
#include "bio_log.h"
#include "bio_ip_util.h"
#include "bio_file_util.h"
#include "bio_config_instance.h"

namespace ock {
namespace bio {
constexpr uint64_t GB_SIZE = 1024 * 1024 * 1024;
constexpr uint64_t MB_SIZE = 1024 * 1024;
constexpr uint32_t DISK_PATH_CONFIG_MAX_NUM = DEVICE_SIZE * NO_4;

namespace {
struct StandaloneDiskInfo {
    uint32_t index;
    std::string path;
    std::string diskKey;
};

using StandaloneDiskGroup = std::vector<uint32_t>;

std::string GetPhysicalDiskKeyOrPath(const std::string &diskPath)
{
    std::string diskKey;
    return FileUtil::GetPhysicalDiskKey(diskPath, diskKey) ? diskKey : diskPath;
}

std::vector<StandaloneDiskInfo> BuildStandaloneDiskInfos(const std::vector<std::string> &diskList)
{
    std::vector<StandaloneDiskInfo> diskInfos;
    diskInfos.reserve(diskList.size());
    for (uint32_t index = 0; index < diskList.size(); ++index) {
        diskInfos.push_back({ index, diskList[index], GetPhysicalDiskKeyOrPath(diskList[index]) });
    }
    return diskInfos;
}

std::vector<StandaloneDiskGroup> GroupStandaloneDisksByPhysicalDisk(const std::vector<StandaloneDiskInfo> &diskInfos)
{
    std::map<std::string, uint32_t> groupIndexByDiskKey;
    std::vector<StandaloneDiskGroup> groups;
    for (const auto &diskInfo : diskInfos) {
        auto iter = groupIndexByDiskKey.find(diskInfo.diskKey);
        if (iter == groupIndexByDiskKey.end()) {
            auto groupIndex = static_cast<uint32_t>(groups.size());
            iter = groupIndexByDiskKey.emplace(diskInfo.diskKey, groupIndex).first;
            groups.emplace_back();
        }
        groups[iter->second].emplace_back(diskInfo.index);
    }
    return groups;
}

std::vector<uint32_t> SelectStandaloneDiskIndexes(const std::vector<StandaloneDiskGroup> &groups, uint32_t deviceId,
    uint32_t deviceCount)
{
    if (deviceCount == 0 || deviceId >= deviceCount) {
        return {};
    }

    uint32_t maxGroupSize = 0;
    for (const auto &group : groups) {
        maxGroupSize = std::max(maxGroupSize, static_cast<uint32_t>(group.size()));
    }

    std::vector<std::vector<uint32_t>> assignedDiskIndexes(deviceCount);
    std::vector<std::vector<bool>> deviceHasGroup(deviceCount, std::vector<bool>(groups.size(), false));
    for (uint32_t diskIndexInGroup = 0; diskIndexInGroup < maxGroupSize; ++diskIndexInGroup) {
        for (uint32_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            const auto &group = groups[groupIndex];
            if (diskIndexInGroup >= group.size()) {
                continue;
            }

            uint32_t bestDevice = deviceCount;
            for (uint32_t offset = 0; offset < deviceCount; ++offset) {
                uint32_t candidate = (groupIndex + diskIndexInGroup + offset) % deviceCount;
                if (bestDevice == deviceCount ||
                    assignedDiskIndexes[candidate].size() < assignedDiskIndexes[bestDevice].size() ||
                    (assignedDiskIndexes[candidate].size() == assignedDiskIndexes[bestDevice].size() &&
                        deviceHasGroup[bestDevice][groupIndex] && !deviceHasGroup[candidate][groupIndex])) {
                    bestDevice = candidate;
                }
            }
            assignedDiskIndexes[bestDevice].emplace_back(group[diskIndexInGroup]);
            deviceHasGroup[bestDevice][groupIndex] = true;
        }
    }

    auto diskIndexes = assignedDiskIndexes[deviceId];
    std::sort(diskIndexes.begin(), diskIndexes.end());
    return diskIndexes;
}

std::string FormatStandaloneDiskSelection(const std::vector<StandaloneDiskInfo> &diskInfos,
    const std::vector<uint32_t> &diskIndexes)
{
    std::ostringstream oss;
    for (uint32_t i = 0; i < diskIndexes.size(); ++i) {
        const auto &diskInfo = diskInfos[diskIndexes[i]];
        oss << "{index:" << diskInfo.index << ", path:" << diskInfo.path << ", physicalDisk:" << diskInfo.diskKey <<
            "}";
        if (i + 1 < diskIndexes.size()) {
            oss << ", ";
        }
    }
    return oss.str();
}
}

void BioConfig::LoadDefaultConf()
{
    /* load net config for fs */
    AddStrConf(NET_DATA_PROTOCOL, VStrEnum::Create(NET_DATA_PROTOCOL.first, "tcp||rdma"));
    AddStrConf(NET_RPC_DATA_BUSY_POLL_MODE, VStrBoolRange::Create(NET_RPC_DATA_BUSY_POLL_MODE.first));
    AddIntConf(NET_RPC_DATA_WORKERS_COUNT, VIntRange::Create(NET_RPC_DATA_WORKERS_COUNT.first, NO_1, NO_16));
    AddStrConf(NET_IPC_DATA_BUSY_POLL_MODE, VStrBoolRange::Create(NET_IPC_DATA_BUSY_POLL_MODE.first));
    AddIntConf(NET_IPC_DATA_WORKERS_COUNT, VIntRange::Create(NET_IPC_DATA_WORKERS_COUNT.first, NO_1, NO_128));
    /* don't allow empty */
    AddStrConf(NET_DATA_IP_MASK, VIpv4MaskValidator::Create(NET_DATA_IP_MASK.first, false));
    AddIntConf(NET_DATA_PORT, VIntRange::Create(NET_DATA_PORT.first, NO_7201, NO_7800));
    AddIntConf(NET_SEGMENT_SIZE);
    /* load net config for cm */
    /* don't allow empty */
    AddIntConf(NET_RECV_REQUEST_HANDLE_THREAD_NUM,
        VIntRange::Create(NET_RECV_REQUEST_HANDLE_THREAD_NUM.first, NO_8, NO_512));
    AddIntConf(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE,
        VIntRange::Create(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE.first, NO_1024, NO_65535));

    /* load log info */
    AddStrConf(LOG_LEVEL, VStrEnum::Create(LOG_LEVEL.first, "error||warn||info||debug||trace"));
    AddIntConf(SEGMENT_SIZE_MB, VIntRange::Create(SEGMENT_SIZE_MB.first, NO_1, NO_16));
    AddIntConf(MEM_CAPACITY_SIZE_GB, VIntRange::Create(MEM_CAPACITY_SIZE_GB.first, NO_U64_0, NO_512));
    AddIntConf(SDK_MEM_CAPACITY_SIZE_MB, VIntRange::Create(SDK_MEM_CAPACITY_SIZE_MB.first, NO_U64_0, NO_4194304));
    AddStrConf(DISK_CONF_PATH);
    AddIntConf(STANDALONE_DEVICE_COUNT, VIntRange::Create(STANDALONE_DEVICE_COUNT.first, 0, DEVICE_SIZE));
    AddIntConf(SDK_MEM_CAPACITY_SIZE_MB);

    AddIntConf(BIO_WCACHE_NEGOTIATE_DELAY, VIntRange::Create(BIO_WCACHE_NEGOTIATE_DELAY.first, NO_50, NO_1000));
    AddStrConf(DATA_CRC_ENABLE, VStrBoolRange::Create(DATA_CRC_ENABLE.first));
    AddStrConf(BIO_TRACE_ENABLE, VStrBoolRange::Create(BIO_TRACE_ENABLE.first));
    AddStrConf(BIO_CACHE_QOS_ENABLE, VStrBoolRange::Create(BIO_CACHE_QOS_ENABLE.first));
    AddIntConf(WCACHE_EVICT_WATER_LEVEL, VIntRange::Create(WCACHE_EVICT_WATER_LEVEL.first, 0, NO_100));
    AddIntConf(WCACHE_DISK_EVICT_WATER_LEVEL, VIntRange::Create(WCACHE_DISK_EVICT_WATER_LEVEL.first, 0, NO_100));
    AddIntConf(RCACHE_EVICT_WATER_LEVEL, VIntRange::Create(RCACHE_EVICT_WATER_LEVEL.first, 0, NO_100));
    AddStrConf(MEM_READ_WRITE_RATIO, VStrRatio::Create(MEM_READ_WRITE_RATIO.first));
    AddStrConf(DISK_READ_WRITE_RATIO, VStrRatio::Create(DISK_READ_WRITE_RATIO.first));
    AddStrConf(BIO_CLI_TOOLS_ENABLE, VStrBoolRange::Create(BIO_CLI_TOOLS_ENABLE.first));

    AddStrConf(WORK_SCENE, VStrEnum::Create(WORK_SCENE.first, "none||bigdata"));
    AddIntConf(WORK_IO_ALIGNSIZE, VIntRange::Create(WORK_IO_ALIGNSIZE.first, NO_1, NO_4194304));
    AddIntConf(WORK_IO_TIMEOUT, VIntRange::Create(WORK_IO_TIMEOUT.first, NO_60, NO_300));
    AddIntConf(WORK_NET_TIMEOUT, VIntRange::Create(WORK_NET_TIMEOUT.first, NO_16, NO_128));
    AddIntConf(BATCH_GET_THREAD_NUM, VIntRange::Create(BATCH_GET_THREAD_NUM.first, NO_8, NO_512));

    /* load cluster manager config */
    AddIntConf(CM_INITIAL_NODE_NUM, VIntRange::Create(CM_INITIAL_NODE_NUM.first, NO_1, NO_256));
    AddIntConf(CM_COPY_NUM, VStrEnum::Create(CM_COPY_NUM.first, "1||2"));
    AddIntConf(CM_PT_NUM, VIntRange::Create(CM_PT_NUM.first, NO_1, NO_8192));
    AddIntConf(CM_NODE_REGISTER_TIMEOUT, VIntRange::Create(CM_NODE_REGISTER_TIMEOUT.first, NO_10, NO_60));
    AddIntConf(CM_NODE_REGISTER_PERM_TIMEOUT, VIntRange::Create(CM_NODE_REGISTER_PERM_TIMEOUT.first, NO_60, NO_600));
    AddStrConf(CM_ZK_HOST, VIpv4PortListValidator::Create(CM_ZK_HOST.first));

    /* load underfs config */
    AddStrConf(UNDERFS_FILE_SYSTEM_TYPE, VStrEnum::Create(UNDERFS_FILE_SYSTEM_TYPE.first, "ceph||hdfs||none"));
    AddStrConf(UNDERFS_CEPH_CFG_PATH);
    AddStrConf(UNDERFS_CEPH_CLUSTER, VStrNotNull::Create(UNDERFS_CEPH_CLUSTER.first));
    AddStrConf(UNDERFS_CEPH_USER, VStrNotNull::Create(UNDERFS_CEPH_USER.first));
    AddStrConf(UNDERFS_CEPH_POOL, VStrCephPool::Create(UNDERFS_CEPH_POOL.first));
    AddStrConf(UNDERFS_HDFS_NAMENODE);
    AddStrConf(UNDERFS_HDFS_WORKING_PATH);

    /* load net config for security */
    AddStrConf(NET_TLS_ENABLE_SWITCH, VStrBoolRange::Create(NET_TLS_ENABLE_SWITCH.first));
    AddStrConf(NET_TLS_CA_CERT_PATH);
    AddStrConf(NET_TLS_CA_CRL_PATH);
    AddStrConf(NET_TLS_SERVER_CERT_PATH);
    AddStrConf(NET_TLS_SERVER_KEY_PATH);
    AddStrConf(NET_TLS_SERVER_KEY_PASS_PATH);
    AddStrConf(NET_TLS_SERVER_DECRYPTER_PATH);
    AddStrConf(NET_TLS_SERVER_SSL_LIB_DIR);

    /* load prometheus config */
    AddStrConf(PROMETHEUS_ENABLE, VStrBoolRange::Create(PROMETHEUS_ENABLE.first));
    AddStrConf(PROMETHEUS_LISTEN_ADDRESS, VIpv4PortValidator::Create(PROMETHEUS_LISTEN_ADDRESS.first));
    AddIntConf(PROMETHEUS_SCRAPE_INTERVAL_SEC, VIntRange::Create(PROMETHEUS_SCRAPE_INTERVAL_SEC.first, NO_2, NO_8192));
}

BResult BioConfig::AutoConfAfterLoadFromFile(const ConfigurationPtr &conf)
{
    auto ret = AutoConfigNet(conf);
    ChkTrueNot(ret == BIO_OK, ret);

    ret = AutoConfigDaemon(conf);
    ChkTrueNot(ret == BIO_OK, ret);

    ret = AutoConfigCm(conf);
    ChkTrueNot(ret == BIO_OK, ret);

    ret = AutoConfigClient(conf);
    ChkTrueNot(ret == BIO_OK, ret);

    ret = AutoConfigUnderFs(conf);
    ChkTrueNot(ret == BIO_OK, ret);

    return ret;
}

BResult BioConfig::AutoConfigNet(const ConfigurationPtr &conf)
{
    /* auto config cm port and ip mask */
    mNetConfig.netSegmentSize = conf->GetInt(NET_SEGMENT_SIZE.first);
    mNetConfig.dataIpMask = conf->GetStr(NET_DATA_IP_MASK.first);
    mNetConfig.dataPort = conf->GetInt(NET_DATA_PORT.first);

    /* fetch ip from ip mask, example:x.x.x.x/24 to x.x.x.x */
    std::vector<std::string> goodIps;
    if (!IpUtil::FilterIpByMask(mNetConfig.dataIpMask, goodIps) || goodIps.empty()) {
        LOG_ERROR("Failed to find ip with ip mask " << mNetConfig.dataIpMask);
        return BIO_ERR;
    }
    mNetConfig.dataIp = std::move(goodIps[0]);

    mNetConfig.isRpcBusyLoop = conf->GetStr(NET_RPC_DATA_BUSY_POLL_MODE.first) == "true";
    mNetConfig.rpcDataWorkersCnt = conf->GetInt(NET_RPC_DATA_WORKERS_COUNT.first);

    mNetConfig.isIpcBusyLoop = conf->GetStr(NET_IPC_DATA_BUSY_POLL_MODE.first) == "true";
    mNetConfig.ipcDataWorkersCnt = conf->GetInt(NET_IPC_DATA_WORKERS_COUNT.first);

    mNetConfig.enableTls = conf->GetStr(NET_TLS_ENABLE_SWITCH.first) == "true";
    mNetConfig.tlsCaCertPath = conf->GetStr(NET_TLS_CA_CERT_PATH.first);
    mNetConfig.tlsCaCrlPath = conf->GetStr(NET_TLS_CA_CRL_PATH.first);
    mNetConfig.tlsServerCertPath = conf->GetStr(NET_TLS_SERVER_CERT_PATH.first);
    mNetConfig.tlsServerKeyPath = conf->GetStr(NET_TLS_SERVER_KEY_PATH.first);
    mNetConfig.tlsServerKeyPassPath = conf->GetStr(NET_TLS_SERVER_KEY_PASS_PATH.first);
    mNetConfig.decrypterLibPath = conf->GetStr(NET_TLS_SERVER_DECRYPTER_PATH.first);
    mNetConfig.opensslLibDir = conf->GetStr(NET_TLS_SERVER_SSL_LIB_DIR.first);
    if (mNetConfig.enableTls) {
        bool checkCaPath = FileUtil::CanonicalPath(mNetConfig.tlsCaCertPath)
                           && FileUtil::CanonicalPath(mNetConfig.tlsServerCertPath)
                           && FileUtil::CanonicalPath(mNetConfig.tlsServerKeyPath);
        if (!checkCaPath) {
            LOG_ERROR("Invalid ca path.");
            return BIO_ERR;
        }

        if (!mNetConfig.tlsCaCrlPath.empty()) {
            if (!FileUtil::CanonicalPath(mNetConfig.tlsCaCrlPath)) {
                LOG_ERROR("Invalid crl path.");
                return BIO_ERR;
            }
        }

        if (!mNetConfig.tlsServerKeyPassPath.empty()) {
            if (!FileUtil::CanonicalPath(mNetConfig.tlsServerKeyPassPath)) {
                LOG_ERROR("Invalid key password path.");
                return BIO_ERR;
            }
        }

        if (!mNetConfig.decrypterLibPath.empty()) {
            if (!FileUtil::CanonicalPath(mNetConfig.decrypterLibPath)) {
                LOG_ERROR("Invalid decrypter Lib Path.");
                return BIO_ERR;
            }
        }
    }

    std::string protocol = conf->GetStr(NET_DATA_PROTOCOL.first);
    if (protocol == "rdma") {
        mNetConfig.protocol = 0;
    } else if (protocol == "tcp") {
        mNetConfig.protocol = 1;
    } else {
        LOG_ERROR("Invalid configuration with protocol items: " << protocol);
        mNetConfig.protocol = NO_255;
    }

    mNetConfig.handleRequestThreadNum = conf->GetInt(NET_RECV_REQUEST_HANDLE_THREAD_NUM.first);
    mNetConfig.handleRequestQueueSize = conf->GetInt(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE.first);

    return BIO_OK;
}

BResult BioConfig::AutoConfigCm(const ConfigurationPtr &conf)
{
    mCmConfig.initialNodeNum = conf->GetInt(CM_INITIAL_NODE_NUM.first);
    mCmConfig.copyNum = conf->GetInt(CM_COPY_NUM.first);
    mCmConfig.nodeNum = NO_256;
    mCmConfig.ptNum = conf->GetInt(CM_PT_NUM.first);
    mCmConfig.registeredTimeoutSec = conf->GetInt(CM_NODE_REGISTER_TIMEOUT.first);
    mCmConfig.registeredPermTimeoutSec = conf->GetInt(CM_NODE_REGISTER_PERM_TIMEOUT.first);
    mCmConfig.groupId = NO_U64_0;
    mCmConfig.zkHost = conf->GetStr(CM_ZK_HOST.first);
    return BIO_OK;
}

BResult BioConfig::AutoConfigDaemon(const ConfigurationPtr &conf)
{
    auto ret = AutoConfigDaemonLogAndOther(conf);
    ChkTrueNot(ret == BIO_OK, ret);

    ret = AutoConfigDaemonCache(conf);
    ChkTrueNot(ret == BIO_OK, ret);

    ret = AutoConfigDaemonDisk(conf);
    ChkTrueNot(ret == BIO_OK, ret);

    return ret;
}

BResult BioConfig::AutoConfigDaemonLogAndOther(const ConfigurationPtr &conf)
{
    auto logLevel = conf->GetStr(LOG_LEVEL.first);
    if (logLevel == "trace") {
        mDaemonConfig.logLevel = BIOLOG_LEVEL_TRACE;
    } else if (logLevel == "debug") {
        mDaemonConfig.logLevel = BIOLOG_LEVEL_DEBUG;
    } else if (logLevel == "info") {
        mDaemonConfig.logLevel = BIOLOG_LEVEL_INFO;
    } else if (logLevel == "warn") {
        mDaemonConfig.logLevel = BIOLOG_LEVEL_WARN;
    } else if (logLevel == "error") {
        mDaemonConfig.logLevel = BIOLOG_LEVEL_ERROR;
    } else {
        LOG_ERROR("Failed to load daemon log level config, invalid level " << logLevel);
        return BIO_ERR;
    }

    mDaemonConfig.enableCrc = conf->GetStr(DATA_CRC_ENABLE.first) == "true";
    mDaemonConfig.enableTrace = conf->GetStr(BIO_TRACE_ENABLE.first) == "true";
    mDaemonConfig.enableQos = conf->GetStr(BIO_CACHE_QOS_ENABLE.first) == "true";
    mDaemonConfig.enableCli = conf->GetStr(BIO_CLI_TOOLS_ENABLE.first) == "true";
    mDaemonConfig.enablePrometheus = conf->GetStr(PROMETHEUS_ENABLE.first) == "true";
    mDaemonConfig.listenAddress = conf->GetStr(PROMETHEUS_LISTEN_ADDRESS.first);
    mDaemonConfig.scrapeIntervalSec = static_cast<uint32_t>(conf->GetInt(PROMETHEUS_SCRAPE_INTERVAL_SEC.first));

    std::string scene = conf->GetStr(WORK_SCENE.first);
    if (scene == "none") {
        mDaemonConfig.workScene = 0;
    } else if (scene == "bigdata") {
        mDaemonConfig.workScene = NO_1;
    } else {
        LOG_ERROR("Invalid configuration with scene items: " << scene);
        return BIO_INVALID_PARAM;
    }

    return BIO_OK;
}

BResult BioConfig::AutoConfigDaemonCache(const ConfigurationPtr &conf)
{
    // The value range of related parameters is verified during configuration parsing.
    mDaemonConfig.workIoAlignSize = static_cast<uint32_t>(conf->GetInt(WORK_IO_ALIGNSIZE.first));
    mDaemonConfig.workIoTimeOut = static_cast<uint32_t>(conf->GetInt(WORK_IO_TIMEOUT.first));
    mDaemonConfig.workNetTimeOut = static_cast<uint32_t>(conf->GetInt(WORK_NET_TIMEOUT.first));
    mDaemonConfig.batchGetThreadNum = static_cast<uint32_t>(conf->GetInt(BATCH_GET_THREAD_NUM.first));

    mDaemonConfig.segment = static_cast<uint32_t>(conf->GetInt(SEGMENT_SIZE_MB.first) * MB_SIZE);
    mDaemonConfig.sdkPoolSize = static_cast<uint64_t>(conf->GetInt(SDK_MEM_CAPACITY_SIZE_MB.first) * MB_SIZE);
    mDaemonConfig.standaloneDeviceCount = static_cast<uint32_t>(conf->GetInt(STANDALONE_DEVICE_COUNT.first));
    mDaemonConfig.negotiateDelay = static_cast<uint32_t>(conf->GetInt(BIO_WCACHE_NEGOTIATE_DELAY.first) * NO_1000);
    mDaemonConfig.memCap = static_cast<uint64_t>(conf->GetInt(MEM_CAPACITY_SIZE_GB.first) * GB_SIZE);
    uint64_t sysFreeMemCap = GetSysFreeMemCap();
    if (mDaemonConfig.memCap > sysFreeMemCap) {
        LOG_ERROR("Failed to set mem cap " << mDaemonConfig.memCap << ", over system free mem cap " << sysFreeMemCap);
        return BIO_ERR;
    }
    mDaemonConfig.wcacheMemEvictLevel = static_cast<uint64_t>(conf->GetInt(WCACHE_EVICT_WATER_LEVEL.first));
    mDaemonConfig.wcacheDiskEvictLevel = static_cast<uint64_t>(conf->GetInt(WCACHE_DISK_EVICT_WATER_LEVEL.first));
    mDaemonConfig.rcacheMemEvictLevel = static_cast<uint64_t>(conf->GetInt(RCACHE_EVICT_WATER_LEVEL.first));
    mDaemonConfig.rcacheDiskEvictLevel = static_cast<uint64_t>(conf->GetInt(RCACHE_EVICT_WATER_LEVEL.first));
    std::vector<std::string> ratios;
    mDaemonConfig.memReadWriteRatio = conf->GetStr(MEM_READ_WRITE_RATIO.first);
    StrUtil::Split(mDaemonConfig.memReadWriteRatio, ":", ratios);
    StrUtil::StrToLong(ratios[0], mDaemonConfig.memReadRatio);
    StrUtil::StrToLong(ratios[NO_1], mDaemonConfig.memWriteRatio);

    ratios.clear();
    mDaemonConfig.diskReadWriteRatio = conf->GetStr(DISK_READ_WRITE_RATIO.first);
    StrUtil::Split(mDaemonConfig.diskReadWriteRatio, ":", ratios);
    StrUtil::StrToLong(ratios[0], mDaemonConfig.diskReadRatio);
    StrUtil::StrToLong(ratios[NO_1], mDaemonConfig.diskWriteRatio);
    if (mDaemonConfig.memCap == 0) {
        LOG_INFO("This server config  memory is 0, disk config is invalid , server can not join pt.");
    }

    return BIO_OK;
}

BResult BioConfig::AutoConfigDaemonDisk(const ConfigurationPtr &conf)
{
    std::string diskMask = conf->GetStr(DISK_CONF_PATH.first);
    StrUtil::Split(diskMask, ":", mDaemonConfig.diskList);
    if (mDaemonConfig.diskList.size() > DISK_PATH_CONFIG_MAX_NUM) {
        LOG_ERROR("Failed to spilt disk path, number of paths cannot exceed " << DISK_PATH_CONFIG_MAX_NUM << ". " <<
            diskMask);
        return BIO_ERR;
    }

    for (std::string &diskPath : mDaemonConfig.diskList) {
        if (!FileUtil::CanonicalPath(diskPath)) {
            LOG_ERROR("Disk path not exist, value " << diskPath);
            return BIO_ERR;
        }
        mDaemonConfig.diskCaps.emplace_back(FileUtil::GetDiskCapacity(diskPath));
    }

    if (mDaemonConfig.diskCaps.size() == 0) {
        mDaemonConfig.memCap = 0;
        LOG_INFO("This server config disk is null, memory reset to 0, server can not join pt.");
        return BIO_OK;
    }

    if (mDaemonConfig.diskCaps.size() > DISK_PATH_CONFIG_MAX_NUM) {
        LOG_ERROR("Disk path num limit:" << DISK_PATH_CONFIG_MAX_NUM << ", input:" << mDaemonConfig.diskCaps.size());
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult BioConfig::AutoConfigClient(const ConfigurationPtr &conf)
{
    mClientConfig.localMrSize = NO_512 * MB_SIZE;
    return BIO_OK;
}

BResult BioConfig::AutoConfigUnderFs(const ConfigurationPtr &conf)
{
    mUnderFsConfig.underFsType = conf->GetStr(UNDERFS_FILE_SYSTEM_TYPE.first);
    mUnderFsConfig.cephConfig.cfgPath = conf->GetStr(UNDERFS_CEPH_CFG_PATH.first);
    if (mUnderFsConfig.underFsType == "ceph") {
        if (!FileUtil::CanonicalPath(mUnderFsConfig.cephConfig.cfgPath)) {
            LOG_ERROR("Ceph config path not exist, value:" << mUnderFsConfig.cephConfig.cfgPath);
            return BIO_ERR;
        }
    }
    mUnderFsConfig.cephConfig.cluster = conf->GetStr(UNDERFS_CEPH_CLUSTER.first);
    mUnderFsConfig.cephConfig.user = conf->GetStr(UNDERFS_CEPH_USER.first);
    mUnderFsConfig.hdfsConfig.nameNode = conf->GetStr(UNDERFS_HDFS_NAMENODE.first);
    mUnderFsConfig.hdfsConfig.workingPath = conf->GetStr(UNDERFS_HDFS_WORKING_PATH.first);

    std::vector<std::string> idWithPoolNames;
    StrUtil::Split(conf->GetStr(UNDERFS_CEPH_POOL.first), ",", idWithPoolNames);
    for (const auto &idWithPoolName : idWithPoolNames) {
        std::vector<std::string> idAndPoolName;
        StrUtil::Split(idWithPoolName, ":", idAndPoolName);
        long poolId = 0;
        if (StrUtil::StrToLong(idAndPoolName[0], poolId)) {
            mUnderFsConfig.cephConfig.pools.emplace(poolId, idAndPoolName[1]);
        }
    }

    return BIO_OK;
}

void BioConfig::BakFileProcess(const std::string &homePath)
{
    std::string initConfPath = homePath + CONF_INIT_BAK_SUFFIX;
    std::string bakConfPath = homePath + CONF_BAK_SUFFIX;
    std::string configPath = homePath + CONF_SUFFIX;
#ifdef DEBUG_UT
    initConfPath = "./" + CONF_INIT_BAK_SUFFIX;
    bakConfPath = "./" + CONF_BAK_SUFFIX;
    configPath = "./" + CONF_SUFFIX;
#endif
    // 1、查看是否存在.bak.init文件，存在则将其重命名为.bak文件,不存在不做处理
    if (FileUtil::Exist(initConfPath)) {
        if (std::rename(initConfPath.c_str(), bakConfPath.c_str()) != 0) {
            LOG_WARN("Replace bak init path to bak path failed!");
        }
    }

    // 2、判断是否存在.bak文件，不存在不做处理，
    if (!FileUtil::Exist(bakConfPath)) {
        return;
    }

    // 3、存在则删除老conf文件（存在，不存在则忽略），并将.bak重命名问bio.conf
    if (FileUtil::Exist(configPath)) {
        if (std::remove(configPath.c_str()) != 0) {
            LOG_ERROR("Remove configPath error.");
            return;
        }
    }
    if (std::rename(bakConfPath.c_str(), configPath.c_str()) != 0) {
        LOG_ERROR("Replace bak path to conf path failed!");
    }
}

BResult BioConfig::Initialize(const std::string &homePath)
{
    BakFileProcess(homePath);
    std::string configurePath = homePath + CONF_SUFFIX;
    LOG_INFO("Start to read config file.");

    if (mInited) {
        return BIO_OK;
    }

    ConfigurationPtr conf = Configuration::GetInstance<BioConfig>();
    if (conf.Get() == nullptr) {
        LOG_INFO("Create config object failed.");
        return BIO_ERR;
    }

    if (!conf->ReadConf<BioConfig>(configurePath)) {
        LOG_ERROR("Read config file failed.");
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

        LOG_ERROR("Invalid configuration with un-proper items: \n" << ossTmp.str() << "\n");
        return BIO_ERR;
    }

    /* auto config something */
    auto ret = AutoConfAfterLoadFromFile(conf);
    if (ret != BIO_OK) {
        LOG_ERROR("Module load config file failed");
        return BIO_ERR;
    }

    /* validate security setting */
    /* do later */
    std::vector<std::string> moreErrors;
    mInited = true;
    return BIO_OK;
}

void BioConfig::SetStandaloneDeviceInfo(uint32_t deviceId)
{
    mStandaloneDeviceInfo.configured = true;
    mStandaloneDeviceInfo.deviceId = deviceId;
    LOG_INFO("Set standalone device info, deviceId:" << deviceId << ".");
}

BResult BioConfig::SelectStandaloneDiskByDeviceInfo()
{
    uint16_t diskNum = static_cast<uint16_t>(mDaemonConfig.diskList.size());
    if (diskNum == 0 || !mStandaloneDeviceInfo.configured) {
        LOG_ERROR("Invalid standalone config, diskNum:" << diskNum <<
            ", deviceConfigured:" << mStandaloneDeviceInfo.configured <<
            ". Standalone mode requires cache disks and BioSetStandaloneDevice before BioInitialize(STANDALONE).");
        return BIO_INVALID_PARAM;
    }
    if (mDaemonConfig.diskCaps.size() != mDaemonConfig.diskList.size()) {
        LOG_ERROR("Standalone disk config is inconsistent, disk path num:" << mDaemonConfig.diskList.size() <<
            ", disk cap num:" << mDaemonConfig.diskCaps.size() << ".");
        return BIO_ERR;
    }

    if (mDaemonConfig.standaloneDeviceCount != 0) {
        return SelectStandaloneDisksByDeviceCount(diskNum);
    }

    return SelectStandaloneDiskLegacy(diskNum);
}

BResult BioConfig::SelectStandaloneDiskLegacy(uint16_t diskNum)
{
    if (mStandaloneDeviceInfo.deviceId >= diskNum) {
        LOG_ERROR("Invalid standalone device info, deviceId:" << mStandaloneDeviceInfo.deviceId <<
            ", diskPathNum:" << diskNum << ". The device id must match the index in bio.disk.path.");
        return BIO_INVALID_PARAM;
    }
    auto diskIndexes = std::vector<uint32_t>{ mStandaloneDeviceInfo.deviceId };
    auto diskInfos = BuildStandaloneDiskInfos(mDaemonConfig.diskList);
    auto selectionLog = FormatStandaloneDiskSelection(diskInfos, diskIndexes);
    return ApplyStandaloneDiskSelection(diskIndexes, diskNum, selectionLog);
}

BResult BioConfig::SelectStandaloneDisksByDeviceCount(uint16_t diskNum)
{
    uint32_t deviceCount = mDaemonConfig.standaloneDeviceCount;
    if (mStandaloneDeviceInfo.deviceId >= deviceCount || diskNum < deviceCount) {
        LOG_ERROR("Invalid standalone disk selection input, deviceId:" << mStandaloneDeviceInfo.deviceId <<
            ", deviceCount:" << deviceCount << ", diskPathNum:" << diskNum <<
            ". The device id must be less than deviceCount and diskPathNum must be no less than deviceCount.");
        return BIO_INVALID_PARAM;
    }

    auto diskInfos = BuildStandaloneDiskInfos(mDaemonConfig.diskList);
    auto diskGroups = GroupStandaloneDisksByPhysicalDisk(diskInfos);
    auto diskIndexes = SelectStandaloneDiskIndexes(diskGroups, mStandaloneDeviceInfo.deviceId, deviceCount);
    if (diskIndexes.empty()) {
        LOG_ERROR("Standalone device has no selected cache disk, deviceId:" << mStandaloneDeviceInfo.deviceId <<
            ", deviceCount:" << deviceCount << ", configuredDiskNum:" << diskNum << ".");
        return BIO_INVALID_PARAM;
    }

    auto selectionLog = FormatStandaloneDiskSelection(diskInfos, diskIndexes);
    return ApplyStandaloneDiskSelection(diskIndexes, diskNum, selectionLog);
}

BResult BioConfig::ApplyStandaloneDiskSelection(const std::vector<uint32_t> &diskIndexes, uint16_t configuredDiskNum,
    const std::string &selectionLog)
{
    std::vector<std::string> selectedDiskList;
    std::vector<int64_t> selectedDiskCaps;
    selectedDiskList.reserve(diskIndexes.size());
    selectedDiskCaps.reserve(diskIndexes.size());
    for (uint32_t diskIndex : diskIndexes) {
        if (diskIndex >= mDaemonConfig.diskCaps.size() || mDaemonConfig.diskCaps[diskIndex] <= 0) {
            LOG_ERROR("Invalid standalone disk capacity, diskIndex:" << diskIndex << ", cap:" <<
                (diskIndex < mDaemonConfig.diskCaps.size() ? mDaemonConfig.diskCaps[diskIndex] : 0) << ".");
            return BIO_INVALID_PARAM;
        }
        selectedDiskList.emplace_back(mDaemonConfig.diskList[diskIndex]);
        selectedDiskCaps.emplace_back(mDaemonConfig.diskCaps[diskIndex]);
    }

    mDaemonConfig.diskList.clear();
    mDaemonConfig.diskCaps.clear();
    mDaemonConfig.diskList.swap(selectedDiskList);
    mDaemonConfig.diskCaps.swap(selectedDiskCaps);
    mStandaloneDiskIndex = diskIndexes.front();
    LOG_INFO("Standalone selects cache disks by device info, deviceId:" << mStandaloneDeviceInfo.deviceId <<
        ", deviceCount:" << mDaemonConfig.standaloneDeviceCount << ", configuredDiskNum:" << configuredDiskNum <<
        ", selectedDiskNum:" << mDaemonConfig.diskList.size() << ", firstSelectedDiskIndex:" << mStandaloneDiskIndex <<
        ", selectedDisks:" << selectionLog << ".");
    return BIO_OK;
}

BResult BioConfig::CreateDiskConfBak(const std::string &diskPath)
{
    LOG_DEBUG("Start to backup disk config.");
    auto ret = FileUtil::BackUpFile(CONFIG_PATH, CONFIG_PATH_BAK_INIT);
    if (UNLIKELY(!ret)) {
        LOG_ERROR("Backup config file failed.");
        return BIO_INNER_ERR;
    }

    BResult res = AddDiskPath(diskPath, CONFIG_PATH_BAK_INIT);
    if (UNLIKELY(res != BIO_OK)) {
        LOG_ERROR("Add disk path to config file failed.");
        FileUtil::RemoveFile(CONFIG_PATH_BAK_INIT);
        return BIO_INNER_ERR;
    }

    ret = FileUtil::RenameFile(CONFIG_PATH_BAK_INIT, CONFIG_PATH_BAK);
    if (UNLIKELY(!ret)) {
        LOG_ERROR("Rename backup config file failed.");
        FileUtil::RemoveFile(CONFIG_PATH_BAK_INIT);
        return BIO_INNER_ERR;
    }

    LOG_DEBUG("Finish to backup disk config.");
    return BIO_OK;
}

BResult BioConfig::AddDiskPath(const std::string &diskPath, const std::string &configPath)
{
    LOG_DEBUG("Start to add disk path to config file.");
    std::vector<std::string> lines;
    auto ret = FileUtil::ReadFile(configPath, lines);
    if (UNLIKELY(!ret || lines.empty())) {
        LOG_ERROR("Read config file failed, filePath: " << configPath << " .");
        return BIO_INNER_ERR;
    }

    std::string configKey = "bio.disk.path";
    std::string newDiskConfig = ":" + diskPath;
    ret = FileUtil::AppendConfigToLine(lines, configKey, newDiskConfig);
    if (UNLIKELY(!ret)) {
        LOG_ERROR("Append config to line failed, filePath: " << newDiskConfig << ", diskPath: " << diskPath << ".");
        return BIO_INNER_ERR;
    }

    ret = FileUtil::WriteFile(configPath, lines);
    if (UNLIKELY(!ret)) {
        LOG_ERROR("Write config to file failed, filePath: " << newDiskConfig << ", diskPath: " << diskPath << ".");
        return BIO_INNER_ERR;
    }

    LOG_DEBUG("Finish to add disk path to config file.");
    return BIO_OK;
}

BResult BioConfig::ReplaceFile(const std::string &oldFile, const std::string &newFile)
{
    LOG_DEBUG("Start to replace file.");
    if (std::remove(oldFile.c_str()) != 0) {
        LOG_ERROR("Old file is not exist, oldFile: " << oldFile << ".");
        return BIO_INNER_ERR;
    }

    if (std::rename(newFile.c_str(), oldFile.c_str()) != 0) {
        LOG_ERROR("Rename file failed, oldFile: " << oldFile << " , newFile: " << newFile << ".");
        return BIO_INNER_ERR;
    }

    LOG_DEBUG("Finish to replace file.");
    return BIO_OK;
}

bool BioConfig::CheckDiskIsExist(std::string &newDiskPath, uint32_t &diskId)
{
    bool isExist = false;
    for (size_t i = 0; i < mDaemonConfig.diskList.size(); ++i) {
        if (mDaemonConfig.diskList[i] == newDiskPath) {
            isExist = true;
            diskId = i;
            return isExist;
        }
    }
    return isExist;
}

void BioConfig::ResizeDaemonConfigDisks(std::string &newDiskPath)
{
    mDaemonConfig.diskList.emplace_back(newDiskPath);
    mDaemonConfig.diskCaps.emplace_back(FileUtil::GetDiskCapacity(newDiskPath));
}

void BioConfig::DumpToLog()
{
    ConfigurationPtr conf = Configuration::GetInstance<BioConfig>();
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
        if (key.find("tls") == std::string::npos && key.find("cfg") == std::string::npos) {
            LOG_INFO("" << key << " = " << value);
        }
    }
    return;
}

uint64_t BioConfig::ModifyConfigEvictWaterLevel(uint8_t tier, uint64_t level)
{
    uint64_t ori;
    if (tier == 0) {
        ori = mDaemonConfig.wcacheMemEvictLevel;
        mDaemonConfig.wcacheMemEvictLevel = level;
    } else {
        ori = mDaemonConfig.wcacheDiskEvictLevel;
        mDaemonConfig.wcacheDiskEvictLevel = level;
    }

    LOG_INFO("config changed tier:" << tier << ", wcacheMemEvictLevel, " << ori << " => " << level);
    return ori;
}

std::string BioConfig::ModifyConfigMemReadWriteRatio(const std::string &ratios)
{
    auto ori = mDaemonConfig.memReadWriteRatio;
    mDaemonConfig.memReadWriteRatio = ratios;
    LOG_INFO("config changed:mDaemonConfig.memReadWriteRatio(GB), " << ori << " => " << ratios);
    return ori;
}

std::string BioConfig::ModifyConfigDiskReadWriteRatio(const std::string &ratios)
{
    auto ori = mDaemonConfig.diskReadWriteRatio;
    mDaemonConfig.diskReadWriteRatio = ratios;
    LOG_INFO("config changed:mDaemonConfig.diskReadWriteRatio(GB), " << ori << " => " << ratios);
    return ori;
}
}
}
