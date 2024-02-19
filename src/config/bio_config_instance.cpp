/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include "bio_config_instance.h"
#include "bio_log.h"
#include "bio_ip_util.h"

namespace ock {
namespace bio {
constexpr uint64_t GB_SIZE = 1024 * 1024 * 1024;
constexpr uint64_t MB_SIZE = 1024 * 1024;
void BioConfig::LoadDefaultConf()
{
    LOG_INFO("Load default conf");

    /* load net config for fs */
    AddStrConf(NET_DATA_PROTOCOL, VStrEnum::Create(NET_DATA_PROTOCOL.first, "tcp||rdma"));
    AddBoolConf(NET_DATA_BUSY_POLL_MODE);
    AddIntConf(NET_DATA_WORKERS_COUNT, VIntRange::Create(NET_DATA_WORKERS_COUNT.first, NO_1, NO_16));
    /* don't allow empty */
    AddStrConf(NET_DATA_IP_MASK, VIpv4MaskValidator::Create(NET_DATA_IP_MASK.first, false));
    AddIntConf(NET_DATA_PORT, VIntRange::Create(NET_DATA_PORT.first, NO_2048, NO_65535));

    /* load net config for cm */
    /* don't allow empty */
    AddIntConf(NET_RECV_REQUEST_HANDLE_THREAD_NUM,
        VIntRange::Create(NET_RECV_REQUEST_HANDLE_THREAD_NUM.first, NO_8, NO_256));
    AddIntConf(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE,
        VIntRange::Create(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE.first, NO_1024, NO_65535));

    AddIntConf(CLIENT_LOCAL_MR_MB, VIntRange::Create(CLIENT_LOCAL_MR_MB.first, NO_256, NO_8192));

    /* load log info */
    AddStrConf(LOG_LEVEL, VStrEnum::Create(LOG_LEVEL.first, "error||warn||info||debug"));
    AddIntConf(SEGMENT_SIZE_MB, VIntRange::Create(SEGMENT_SIZE_MB.first, NO_1, NO_16));
    AddIntConf(MEM_CAPACITY_SIZE_GB, VIntRange::Create(MEM_CAPACITY_SIZE_GB.first, NO_1, NO_512));
    AddIntConf(DISK_CAPACITY_SIZE_GB, VIntRange::Create(DISK_CAPACITY_SIZE_GB.first, NO_1, NO_8192));
    AddStrConf(DISK_CONF_PATH, VStrNotNull::Create(DISK_CONF_PATH.first));

    /* load security related config */
    AddBoolConf(SECURITY_ENABLED);
    AddStrConf(SECURITY_CONF_PATH, VStrNotNull::Create(SECURITY_CONF_PATH.first));

    /* load cluster manager config */
    AddIntConf(CM_INITIAL_NODE_NUM, VIntRange::Create(CM_INITIAL_NODE_NUM.first, 2, NO_256));
    AddIntConf(CM_NODE_NUM, VIntRange::Create(CM_NODE_NUM.first, 2, NO_256));
    AddIntConf(CM_PT_NUM, VIntRange::Create(CM_PT_NUM.first, 2, NO_8192));
    AddIntConf(CM_NODE_REGISTER_TIMEOUT, VIntRange::Create(CM_NODE_REGISTER_TIMEOUT.first, NO_16, NO_60));
    AddIntConf(CM_NODE_REGISTER_PERM_TIMEOUT, VIntRange::Create(CM_NODE_REGISTER_PERM_TIMEOUT.first, NO_60, NO_1024));
    AddIntConf(CM_GROUP_ID, VIntRange::Create(CM_GROUP_ID.first, 0, NO_255));
    AddStrConf(CM_ZK_HOST, VStrNotNull::Create(CM_ZK_HOST.first));
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

    return ret;
}

BResult BioConfig::AutoConfigNet(const ConfigurationPtr &conf)
{
    /* auto config cm port and ip mask */
    mNetConfig.dataIpMask = conf->GetStr(NET_DATA_IP_MASK.first);
    mNetConfig.dataPort = conf->GetInt(NET_DATA_PORT.first);

    /* fetch ip from ip mask, example:x.x.x.x/24 to x.x.x.x */
    std::vector<std::string> goodIps;
    if (!IpUtil::FilterIpByMask(mNetConfig.dataIpMask, goodIps) || goodIps.empty()) {
        LOG_ERROR("Failed to find ip with ip mask " << mNetConfig.dataIpMask);
        return BIO_ERR;
    }
    mNetConfig.dataIp = std::move(goodIps[0]);

    mNetConfig.isBusyLoop = conf->GetBool(NET_DATA_BUSY_POLL_MODE.first);
    mNetConfig.dataWorkersCnt = conf->GetInt(NET_DATA_WORKERS_COUNT.first);

    std::string protocol = conf->GetStr(NET_DATA_PROTOCOL.first);
    if (protocol == "rdma") {
        mNetConfig.protocol = 0;
    } else if (protocol == "tcp") {
        mNetConfig.protocol = 1;
    } else {
        LOG_ERROR("Invalid configuration with protocol items: " << protocol);
        mNetConfig.protocol = 255;
    }

    mNetConfig.handleRequestThreadNum = conf->GetInt(NET_RECV_REQUEST_HANDLE_THREAD_NUM.first);
    mNetConfig.handleRequestQueueSize = conf->GetInt(NET_RECV_REQUEST_HANDLE_QUEUE_SIZE.first);

    return BIO_OK;
}

BResult BioConfig::AutoConfigCm(const ConfigurationPtr &conf)
{
    mCmConfig.initialNodeNum = conf->GetInt(CM_INITIAL_NODE_NUM.first);
    mCmConfig.nodeNum = conf->GetInt(CM_NODE_NUM.first);
    mCmConfig.ptNum = conf->GetInt(CM_PT_NUM.first);
    mCmConfig.registeredTimeoutSec = conf->GetInt(CM_NODE_REGISTER_TIMEOUT.first);
    mCmConfig.registeredPermTimeoutSec = conf->GetInt(CM_NODE_REGISTER_PERM_TIMEOUT.first);
    mCmConfig.groupId = conf->GetInt(CM_GROUP_ID.first);
    mCmConfig.zkHost = conf->GetStr(CM_ZK_HOST.first);

    return BIO_OK;
}

BResult BioConfig::AutoConfigDaemon(const ConfigurationPtr &conf)
{
    auto logLevel = conf->GetStr(LOG_LEVEL.first);
    if (logLevel == "debug") {
        mDaemonConfig.logLevel = SPDLOG_LEVEL_DEBUG;
    } else if (logLevel == "info") {
        mDaemonConfig.logLevel = SPDLOG_LEVEL_INFO;
    } else if (logLevel == "warn") {
        mDaemonConfig.logLevel = SPDLOG_LEVEL_WARN;
    } else if (logLevel == "error") {
        mDaemonConfig.logLevel = SPDLOG_LEVEL_ERROR;
    } else {
        LOG_ERROR("Failed to load daemon log level config, invalid level " << logLevel);
        return BIO_ERR;
    }

    mDaemonConfig.segment = conf->GetInt(SEGMENT_SIZE_MB.first) * MB_SIZE;
    mDaemonConfig.memCap = conf->GetInt(MEM_CAPACITY_SIZE_GB.first) * GB_SIZE;
    mDaemonConfig.diskCap = conf->GetInt(DISK_CAPACITY_SIZE_GB.first) * GB_SIZE;

    std::string diskMask = conf->GetStr(DISK_CONF_PATH.first);
    StrUtil::Split(diskMask, ":", mDaemonConfig.diskList);
    if (mDaemonConfig.diskList.size() == 0 || mDaemonConfig.diskList.size() > NO_4) {
        LOG_ERROR("Failed to spilt disk path, " << diskMask);
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult BioConfig::AutoConfigClient(const ConfigurationPtr &conf)
{
    auto reserveSize = conf->GetInt(CLIENT_LOCAL_MR_MB.first);
    mClientConfig.localMrSize = reserveSize * MB_SIZE;
    return BIO_OK;
}

BResult BioConfig::Initialize(const std::string &homePath)
{
    std::string configurePath = homePath + "/conf/bio.conf";
    LOG_INFO("start to read config file : " << configurePath);

    if (mInited) {
        return BIO_OK;
    }

    ConfigurationPtr conf = Configuration::GetInstance<BioConfig>();
    if (conf.Get() == nullptr) {
        LOG_INFO("create config object failed");
        return BIO_ERR;
    }

    if (!conf->ReadConf<BioConfig>(configurePath)) {
        LOG_ERROR("read config file " << configurePath << " failed");
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
    std::ostringstream ossTmp;
    ossTmp << "Configuration Dump:" << std::endl;

    for (uint32_t i = 0; i < reader.Size(); i++) {
        std::string key;
        std::string value;
        reader.GetI(i, key, value);
        ossTmp << " " << key << " = " << value << std::endl;
    }

    LOG_INFO(ossTmp.str());
}
}
}
