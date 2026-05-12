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

#include "expire_checker.h"
#include "mms_trace.h"
#include "mms_mem_mgr.h"
#include "mms_mem_allocator.h"
#include "mms_cache.h"
#include "mms_message.h"
#include "mms_client.h"

#ifdef USE_CLI_TOOLS
#include <dlfcn.h>
#endif

namespace ock {
namespace mms {
static void Log(int level, const char *msg)
{
    if (MmsClientLog::Instance() != nullptr) {
        MmsClientLog::Instance()->Log(level, msg);
    }
}

static bool IsStartCatchUpRetryable(BResult ret)
{
    return ret == MMS_ALLOC_FAIL || ret == MMS_INNER_RETRY || ret == MMS_NET_RETRY || ret == MMS_CHECK_PT_FAIL;
}

BResult MmsClient::Initialize(const MmsOptions &options, ServiceCallback service)
{
    if (mStarted) {
        return MMS_OK;
    }

    if (UNLIKELY(service == nullptr)) {
        return MMS_INVALID_PARAM;
    }

    mServiceCallback = service;
    auto ret = ClientGlobVarInit();
    if (ret != MMS_OK) {
        return ret;
    }
    ret = ClientLoggerInit();
    if (ret != MMS_OK) {
        return ret;
    }
#ifdef USE_CLI_TOOLS
    ret = ClientDiagnoseInit();
    if (ret != MMS_OK) {
        return ret;
    }
#endif
    ret = ClientNetInit(options);
    if (ret != MMS_OK) {
        return ret;
    }
    ret = ClientBasicInit();
    if (ret != MMS_OK) {
        return ret;
    }
    ret = ClientMemInit();
    if (ret != MMS_OK) {
        return ret;
    }
    ret = ClientCacheInit();
    if (ret != MMS_OK) {
        return ret;
    }
    ret = ClientKvInit();
    if (ret != MMS_OK) {
        return ret;
    }
    ret = CheckServiceState(mServiceable);
    if (ret != MMS_OK) {
        return ret;
    }
    if (options.tlsEnable) {
        auto expireChecker = ExpireChecker::Instance();
        if (expireChecker == nullptr) {
            LOG_INFO("expire checker alloc fail.");
            return MMS_ALLOC_FAIL;
        }
        ret = expireChecker->ExpireCheckerInit(options.caCerPath, options.certificationPath,
                                               options.opensslLibDir);
        if (ret != MMS_OK) {
            return ret;
        }
    }
    mServiceCallback(mServiceable);
    mStarted = true;
    return MMS_OK;
}

void MmsClient::Exit(void)
{
}

BResult MmsClient::ClientGlobVarInit(void)
{
    mKvClient = MakeRef<MmsKvClient>();
    if (mKvClient == nullptr) {
        CLIENT_LOG_ERROR("Get kv client failed.");
        return MMS_ALLOC_FAIL;
    }

    mCache = Cache::Instance();
    if (mCache == nullptr) {
        CLIENT_LOG_ERROR("Get cache failed.");
        return MMS_ALLOC_FAIL;
    }

    mNetEngine = MakeRef<NetEngine>();
    if (mNetEngine == nullptr) {
        CLIENT_LOG_ERROR("Get net engine failed.");
        return MMS_ALLOC_FAIL;
    }

    mMemMgr = MmsMemMgr::Instance();
    if (mMemMgr == nullptr) {
        CLIENT_LOG_ERROR("Get mem mgr failed.");
        return MMS_ALLOC_FAIL;
    }

    mMemAllocator = MmsMemAllocator::Instance(MMAP_AREA_IOCTX);
    if (mMemAllocator == nullptr) {
        CLIENT_LOG_ERROR("Get mem allocator failed.");
        return MMS_ALLOC_FAIL;
    }

    return MMS_OK;
}

BResult MmsClient::ClientLoggerInit(void)
{
    auto level = static_cast<int32_t>(MmsClientLog::Level::LOG_LEVEL_INFO);
    uint8_t logType = static_cast<uint8_t>(NO_2);
    std::string logFilePath = "/var/log/mms/";
    auto clientLog = MmsClientLog::Instance();
    if (clientLog == nullptr) {
        return MMS_ALLOC_FAIL;
    }
    return clientLog->Initialize(level, logType, logFilePath);
}

void MmsClient::ClientLoggerExit(void)
{
    MmsClientLog::Instance()->Exit();
}

BResult FillNetOptions(const MmsOptions &options, NetOptions &netOptions)
{
    netOptions.isBusyPolling = options.netIsBusyPolling != 0;
    netOptions.connCount = options.netConnectCnt;
    netOptions.workerGroupsNum = options.netGroupNum;
    netOptions.role = NET_CLIENT;
    netOptions.protocol = ServiceProtocol::SHM;

    // tls
    netOptions.tlsEnable = options.tlsEnable != 0;
    netOptions.certificationPath = options.certificationPath;
    netOptions.caCerPath = options.caCerPath;
    netOptions.caCrlPath = options.caCrlPath;
    netOptions.privateKeyPath = options.privateKeyPath;
    netOptions.privateKeyPasswordPath = options.privateKeyPasswordPath;
    netOptions.decrypterLibPath = options.decrypterLibPath;
    netOptions.opensslLibDir = options.opensslLibDir;

    if (!netOptions.tlsEnable) {
        return MMS_OK;
    }

    bool checkCaPath = FileUtil::CanonicalPath(netOptions.certificationPath) &&
                       FileUtil::CanonicalPath(netOptions.caCerPath) &&
                       FileUtil::CanonicalPath(netOptions.privateKeyPath) &&
                       FileUtil::CanonicalPath(netOptions.privateKeyPasswordPath) &&
                       FileUtil::CanonicalPath(netOptions.decrypterLibPath);
    if (!checkCaPath) {
        CLIENT_LOG_ERROR("Check path failed.");
        return MMS_INVALID_PARAM;
    }

    return MMS_OK;
}

BResult MmsClient::ClientNetInit(const MmsOptions &options)
{
    int16_t timeoutSec = NO_16;
    NetMemList memList;
    auto ret = mNetEngine->Initialize(timeoutSec, 0, NO_1024, Log, memList);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Net engine initialize failed, result:" << ret << ".");
        return ret;
    }

    mOptions = options;
    if (options.netGroupNum > MAX_GROUPS_NUM) {
        CLIENT_LOG_ERROR("Invalid net groups num:" << options.netGroupNum << ".");
        return MMS_INVALID_PARAM;
    }

    NetOptions netOptions;
    ret = FillNetOptions(options, netOptions);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Check path failed, invalid options.");
        return ret;
    }

    ret = mNetEngine->Start(netOptions);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Net engine start failed, result:" << ret << ".");
        return ret;
    }

    auto channelBroken = [this](uint32_t nodeId, uint32_t pid) -> void {
        std::thread t([this, nodeId]() {
            auto ret = mStartService->Execute([this]() { BuildThreadTask(); });
            if (ret != MMS_OK) {
                CLIENT_LOG_ERROR("Execute build services failed.");
                return;
            }
        });
        t.detach();
    };
    ret = mNetEngine->RegisterChannelBrokenHandler(channelBroken);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Register channel broken handler failed, ret:" << ret << ".");
        return ret;
    }

    mStartService = ExecutorService::Create(NO_1, NO_1024);
    if (mStartService == nullptr) {
        CLIENT_LOG_ERROR("Failed to create executor service.");
        return MMS_ALLOC_FAIL;
    }

    mStartService->SetThreadName("client-services");
    auto result = mStartService->Start();
    if (!result) {
        CLIENT_LOG_ERROR("Failed to start executor service.");
        return MMS_ALLOC_FAIL;
    }

    ConnectInfo info(INVALID_NID, static_cast<uint32_t>(getpid()), INVALID_NID, true);
    ret = mNetEngine->SyncConnect(info);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Connect to local failed, ret:" << ret << ".");
        return ret;
    }

    mServerOnline.store(true);

    return MMS_OK;
}

void MmsClient::ClientNetExit(void)
{
    mNetEngine->Stop();
}

BResult MmsClient::ClientBasicInit(void)
{
    BasicRequest req = { { 0, MMS_OP_C_BASIC, 0, 0 } };
    BasicResponse rsp;
    BResult ret = mNetEngine->SyncCall<BasicRequest, BasicResponse>(INVALID_NID, 0, MMS_OP_C_BASIC, req, rsp);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Send basic info request failed, ret:" << ret << ".");
        return ret;
    }

    ret = mNetEngine->ReceiveFds(INVALID_NID, mAreaFd, MMAP_AREA_BUTT);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Receive file mem fd failed, ret:" << ret << ".");
        return ret;
    }

    if (rsp.logLevel > SPDLOG_LEVEL_ERROR || rsp.logLevel < SPDLOG_LEVEL_TRACE || rsp.memNum > MAX_NUMAS_NUM ||
        rsp.netTimeOut < NO_10 || rsp.netTimeOut > NO_60) {
        CLIENT_LOG_ERROR("Invalid response, log level:" << rsp.logLevel << ", numa num:" << rsp.memNum
                                                        << ", net timeout:" << rsp.netTimeOut << ".");
        return MMS_INVALID_PARAM;
    }

    mNetEngine->UpdateTimeOut(static_cast<int16_t>(rsp.netTimeOut)); // 更新消息请求发送超时参数.
    ret = mNetEngine->UpdateChannelTimeOut(INVALID_NID); // 更新链路超时参数.
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Update channel timeout failed, ret:" << ret << ".");
        return ret;
    }

    MmsClientLog::Instance()->ResetLogLevel(rsp.logLevel);

    for (uint16_t idx = 0; idx < rsp.memNum; idx++) {
        mNumaId[idx] = rsp.memNumaId[idx];
        mNumaSize[idx] = rsp.memSize[idx];
    }
    mNumaNum = rsp.memNum;
    mIoTimeOut = rsp.ioTimeOut;
    mLogLevel = rsp.logLevel;
    mEnableCrc = rsp.enableCrc;
    mMaxMsgBuffSize = rsp.maxMsgBuffSize;
    mBlockInfo = {rsp.minBlockSize, rsp.maxBlockSize, rsp.minBlockSizeRate};
    UpdateCrcSwitch(rsp.enableCrc);
    tracemark::TraceMark::Init();
    tracemark::TraceMark::SetEnable(rsp.traceSwitch);

    NumaGroupIndex::Instance()->SetNumaInfo(mNumaId, mNumaNum);
    NumaGroupIndex::Instance()->SetGroupInfo(mOptions.netGroupNum);

    CLIENT_LOG_INFO("Client basic init, SERVER PID:" << rsp.serverPid << ".");
    return MMS_OK;
}

void MmsClient::ClientBasicExit(void)
{
}

BResult MmsClient::InitMemMgr()
{
    uint32_t index;
    MemMgrOptions mgrOptions;
    for (index = 0; index < mNumaNum; index++) {
        mgrOptions.numaId[index] = mNumaId[index];
        mgrOptions.numaSize[index] = mNumaSize[index];
        mgrOptions.areaSize[MMAP_AREA_IOCTX][index] = META_SHM_IOCTX_SIZE;
        mgrOptions.areaSize[MMAP_AREA_BUCKET][index] = mgrOptions.numaSize[index] / IO_SIZE_1G * IO_SIZE_32M;
        ValueIndexMemCfg cfg{};
        cfg.totalMemSize =
            mgrOptions.numaSize[index] - META_SHM_IOCTX_SIZE - mgrOptions.areaSize[MMAP_AREA_BUCKET][index];
        cfg.indexNodeSize = INDEX_VALUE_SIZE + sizeof(BlockHeader);
        cfg.minBlockSize = mBlockInfo.minBlockSize + sizeof(BlockHeader) + sizeof(DataHeader);
        cfg.maxBlockSize = mBlockInfo.maxBlockSize + sizeof(BlockHeader) + sizeof(DataHeader);
        cfg.minBlockMemRatio = static_cast<double>(mBlockInfo.minBlockSizeRate) / NO_10;
        cfg.maxBlockMemRatio = static_cast<double>(NO_10 - mBlockInfo.minBlockSizeRate) / NO_10;
        cfg.Calculate();
        mgrOptions.areaSize[MMAP_AREA_INDEX][index] = cfg.indexMemSize;
        mgrOptions.areaSize[MMAP_AREA_VALUE][index] = cfg.valueMemSize;
    }

    mgrOptions.numaNum = mNumaNum;
    mgrOptions.areaFd[MMAP_AREA_IOCTX] = mAreaFd[MMAP_AREA_IOCTX];
    mgrOptions.areaFd[MMAP_AREA_BUCKET] = mAreaFd[MMAP_AREA_BUCKET];
    mgrOptions.areaFd[MMAP_AREA_VALUE] = mAreaFd[MMAP_AREA_VALUE];
    mgrOptions.areaFd[MMAP_AREA_INDEX] = mAreaFd[MMAP_AREA_INDEX];

    mgrOptions.areaMode[MMAP_AREA_IOCTX] = MMAP_MODE_RW;
    mgrOptions.areaMode[MMAP_AREA_BUCKET] = MMAP_MODE_RW;
    mgrOptions.areaMode[MMAP_AREA_VALUE] = MMAP_MODE_READ;
    mgrOptions.areaMode[MMAP_AREA_INDEX] = MMAP_MODE_RW;
    auto ret = mMemMgr->Initialize(mgrOptions, Log, false);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Mem mgr initialize failed, result:" << ret << ".");
        return ret;
    }
    mMemMgr->ResetLogLevel(mLogLevel);

    return MMS_OK;
}

BResult MmsClient::ClientMemInit(void)
{
    BResult ret = InitMemMgr();
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Init memory manager failed, ret:" << ret << ".");
        return ret;
    }

    MemAllocOptions allocOptions;
    ret = mMemMgr->GetAreaMemDesc(MMAP_AREA_IOCTX, allocOptions.numaId, allocOptions.numaSize, allocOptions.numaAddress,
                                  allocOptions.numaNum);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Mem mgr get data mem failed, result:" << ret << ".");
        return ret;
    }

    allocOptions.blockNum = NO_1;
    allocOptions.blockRate[NO_0] = NO_10;
    allocOptions.blockSize[NO_0] = mMaxMsgBuffSize;

    ret = mMemAllocator->Initialize(allocOptions);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Mem alloctor initialize failed, result:" << ret << ".");
        return ret;
    }

    return MMS_OK;
}

void MmsClient::ClientMemExit(void)
{
}

BResult MmsClient::ClientCacheInit(void)
{
    uint64_t addr;
    uint64_t size;
    auto ret = mMemMgr->GetAreaMemDesc(MMAP_AREA_BUCKET, addr, size);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Mem mgr get k/v mem failed, result:" << ret << ".");
        return ret;
    }

    ret = mCache->Init(addr, size, Log, false, {0, 0});
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Failed to init cache instance, ret:" << ret << ".");
        return ret;
    }

    mCache->ResetLogLevel(mLogLevel);
    return MMS_OK;
}

void MmsClient::ClientCacheExit(void)
{
}

BResult MmsClient::ClientKvInit(void)
{
    KvClientPara para = {mCache, mNetEngine, mMemMgr, mMemAllocator, mIoTimeOut, mMaxMsgBuffSize};
    auto ret = mKvClient->Initialize(para);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Failed to initialize mirror client, ret:" << ret << ".");
    }
    return ret;
}

void MmsClient::ClientKvExit(void)
{
    return;
}

BResult MmsClient::ResetResource()
{
    for (uint16_t &numaId: mNumaId) {
        numaId = -1;
    }

    for (auto &fd: mAreaFd) {
        fd = -1;
    }

    BResult ret = MMS_OK;
    ret = mMemMgr->Reset();
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("mMemMgr reset failed, ret:" << ret << ".");
        return ret;
    }

    ret = mMemAllocator->Reset();
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("mMemAllocator reset failed, ret:" << ret << ".");
        return ret;
    }

    ret = mCache->Reset();
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("mCache reset failed, ret:" << ret << ".");
        return ret;
    }
    return MMS_OK;
}

BResult MmsClient::BuildThreadTask(void)
{
    mStarted = false;
    if (mServiceable) {
        mServiceable = false;
        mServiceCallback(mServiceable);
    }
    mServerOnline.store(false);

    constexpr uint16_t interval = 2;

    uint32_t maxCnt = mIoTimeOut / interval;
    uint32_t retryCnt = 0;
    do {
        sleep(interval);
        CLIENT_LOG_WARN("Wait onfly io, retry cnt:" << retryCnt++ << ".");
    } while (retryCnt < maxCnt);

    BResult ret = MMS_OK;
    ret = ResetResource();
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("reset resource failed!");
        return MMS_ERR;
    }

    retryCnt = 0;
    do {
        ConnectInfo info(INVALID_NID, static_cast<uint32_t>(getpid()), INVALID_NID, true);
        ret = mNetEngine->SyncConnect(info);
        if (ret != MMS_OK) {
            sleep(interval);
            CLIENT_LOG_WARN("Connect to local server failed, retry cnt:" << retryCnt++ << ".");
            continue;
        }
    } while (ret != MMS_OK);

    mServerOnline.store(true);

    retryCnt = 0;
    do {
        if (mNetEngine->CheckConnect(INVALID_NID) != MMS_OK) {
            CLIENT_LOG_WARN("server disconnected, return build services. retryCnt:" << retryCnt);
            return MMS_OK;
        }
        ret = BuildServices();
        if (ret != MMS_OK) {
            sleep(interval);
            CLIENT_LOG_WARN("Build services fail, ret:" << ret << ". retryCnt:" << retryCnt);
            retryCnt++;
            continue;
        }
    } while (ret != MMS_OK);

    mStarted = true;

    return MMS_OK;
}

BResult MmsClient::BuildServices(void)
{
    auto ret = ClientBasicInit();
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Build basic failed, ret:" << ret << ".");
        return ret;
    }

    ret = ClientMemInit();
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("ClientMemInit  failed, ret:" << ret << ".");
        return ret;
    }

    ret = ClientCacheInit();
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("ClientCacheInit  failed, ret:" << ret << ".");
        return ret;
    }

    return MMS_OK;
}

BResult MmsClient::CheckServiceState(std::atomic<bool> &serviceable)
{
    ServiceRequest req = { { 0, MMS_OP_C_SERVICEABLE, 0, 0 } };
    ServiceResponse rsp;
    BResult ret = mNetEngine->SyncCall<ServiceRequest, ServiceResponse>(INVALID_NID, 0, MMS_OP_C_SERVICEABLE, req, rsp);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Send serviceable request failed, ret:" << ret << ".");
        return ret;
    }

    serviceable = rsp.serviceable;
    return MMS_OK;
}

BResult MmsClient::MmsStartCatchUpTask(void)
{
    if (mServiceable) {
        CLIENT_LOG_WARN("server can provide service, do not start catch up task!");
        return MMS_OK;
    }

    BasicRequest req = {{0, MMS_OP_C_CRB_START_CATCH_UP, 0, 0, 0}};
    BResult rsp = MMS_OK;
    BResult ret = MMS_OK;
    for (uint16_t i = 0; i < RETRY_COUNT; i++) {
        ret = mNetEngine->SyncCall<BasicRequest, BResult>(INVALID_NID, 0, MMS_OP_C_CRB_START_CATCH_UP, req, rsp);
        if (UNLIKELY(ret != MMS_OK)) {
            CLIENT_LOG_ERROR("Send node recover request failed, ret:" << ret << ".");
            if (!IsStartCatchUpRetryable(ret) || i + NO_1 == RETRY_COUNT) {
                break;
            }
            sleep(RETRY_SLEEP);
            continue;
        }

        break;
    }

    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Start catch up failed, ret:" << ret << ".");
        return ret;
    }

    bool expected = false;
    if (mServiceCheckStarted.compare_exchange_strong(expected, true)) {
        std::thread t([this]() {
            BackCheckStateTask();
        });
        t.detach();
    }

    return MMS_OK;
}

void MmsClient::BackCheckStateTask()
{
    uint64_t retryCnt = 0;
    BResult ret = MMS_OK;
    constexpr uint16_t interval = 2;
    do {
        ret = CheckServiceState(mServiceable);
        if (ret != MMS_OK || !mServiceable) {
            sleep(interval);
            CLIENT_LOG_WARN("Check services state fail, retry count:" << retryCnt++ << ".");
            continue;
        }
    } while (!mServiceable);
    mServiceCallback(mServiceable.load());
    mServiceCheckStarted.store(false);
    CLIENT_LOG_INFO("Client is serviceable.");
}

#ifdef USE_CLI_TOOLS
using ClientDiagnose = int (*)();
BResult MmsClient::ClientDiagnoseInit(void)
{
    uint32_t procPid = 600U;
    std::string diagName = "mms_c";
    BResult ret = cli_agent_init(procPid, const_cast<char *>(diagName.c_str()));
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize cli, ret:" << ret << ".");
        return MMS_INNER_ERR;
    }

    std::string soFileName = std::string(PROJECT_PATH_PREFIX) + "/lib/libclient_diagnose.so";
    char *canonicalPath = realpath(soFileName.c_str(), nullptr);
    if (canonicalPath == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library, not exist, " << soFileName << ".");
        return MMS_NOT_EXISTS;
    }

    void *handler = dlopen(canonicalPath, RTLD_NOW);
    free(canonicalPath);
    canonicalPath = nullptr;
    if (handler == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen , error " << dlerror());
        return MMS_INNER_ERR;
    }
    ClientDiagnose clientInitFunc = reinterpret_cast<ClientDiagnose>(dlsym(handler, "ClientDiagnoseInit"));
    ret = clientInitFunc();
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Failed to Initialize client diagnose, ret:" << ret << ".");
        return ret;
    }
    return MMS_OK;
}
#endif
}
}
