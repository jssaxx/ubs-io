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

#include <string>

#ifdef USE_CLI_TOOLS
#include <dlfcn.h>
#endif

namespace ock {
namespace mms {

static thread_local uint16_t g_groupIndex = NumaGroupIndex::Instance()->GetGroupIndex();

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

BResult MmsClient::InitExpireChecker(const MmsOptions &options)
{
    if (options.tlsEnable == 0) {
        return MMS_OK;
    }

    auto expireChecker = ExpireChecker::Instance();
    if (expireChecker == nullptr) {
        CLIENT_LOG_ERROR("Expire checker alloc fail.");
        return MMS_ALLOC_FAIL;
    }
    return expireChecker->ExpireCheckerInit(options.caCerPath, options.certificationPath, options.opensslLibDir);
}

BResult MmsClient::InitClientBase(const MmsOptions &options)
{
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
    return ClientNetInit(options);
}

BResult MmsClient::InitClientDataPath(void)
{
    auto ret = ClientBasicInit();
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
    return ClientKvInit();
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
    auto ret = InitClientBase(options);
    if (ret != MMS_OK) {
        return ret;
    }
    ret = InitClientDataPath();
    if (ret != MMS_OK) {
        return ret;
    }
    ret = CheckServiceState(mServiceable);
    if (ret != MMS_OK) {
        return ret;
    }
    ret = InitExpireChecker(options);
    if (ret != MMS_OK) {
        return ret;
    }
    mServiceCallback(mServiceable);
    mStarted = true;
    return MMS_OK;
}

void MmsClient::Exit(void)
{
    mNotifyCallback.store(nullptr, std::memory_order_release);
    mNotifyChannel = nullptr;
    mNotifyPid = 0;
    if (mNotifyCallbackService != nullptr) {
        mNotifyCallbackService->Stop();
        mNotifyCallbackService = nullptr;
    }
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

BResult MmsClient::RegisterNotifyHandler()
{
    auto ret = mNetEngine->RegisterNewRequestHandler(MMS_OP_NOTIFY_DATA_CHANGE,
        std::bind(&MmsClient::HandleNotifyDataChange, this, std::placeholders::_1));
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Register notify data change handler failed, ret:" << ret << ".");
    }
    return ret;
}

void MmsClient::HandleNotifyChannelBroken()
{
    mNotifyChannel = nullptr;
    auto callback = mNotifyCallback.load(std::memory_order_acquire);
    if (callback == nullptr) {
        return;
    }
    bool ret = mStartService->Execute([this, callback]() {
        auto registerRet = RegisterNotifyCallback(callback);
        if (registerRet != MMS_OK) {
            CLIENT_LOG_ERROR("Re-register notify callback failed, ret:" << registerRet << ".");
        }
    });
    if (!ret) {
        CLIENT_LOG_ERROR("Execute notify reconnect failed.");
    }
}

void MmsClient::HandleClientChannelBroken(uint32_t pid)
{
    if (pid == NOTIFY_PID_FLAG) {
        HandleNotifyChannelBroken();
        return;
    }

    std::thread thread([this]() {
        bool ret = mStartService->Execute([this]() { BuildThreadTask(); });
        if (!ret) {
            CLIENT_LOG_ERROR("Execute build services failed.");
        }
    });
    thread.detach();
}

BResult MmsClient::RegisterClientChannelBrokenHandler()
{
    auto channelBroken = [this](uint32_t, uint32_t pid) -> void {
        HandleClientChannelBroken(pid);
    };
    auto ret = mNetEngine->RegisterChannelBrokenHandler(channelBroken);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Register channel broken handler failed, ret:" << ret << ".");
    }
    return ret;
}

BResult MmsClient::StartClientServiceExecutor()
{
    mStartService = ExecutorService::Create(NO_1, NO_1024);
    if (mStartService == nullptr) {
        CLIENT_LOG_ERROR("Failed to create executor service.");
        return MMS_ALLOC_FAIL;
    }

    mStartService->SetThreadName("client-services");
    if (!mStartService->Start()) {
        CLIENT_LOG_ERROR("Failed to start executor service.");
        return MMS_ALLOC_FAIL;
    }
    return MMS_OK;
}

BResult MmsClient::ConnectLocalServer()
{
    ConnectInfo info(INVALID_NID, static_cast<uint32_t>(getpid()), INVALID_NID, true);
    auto ret = mNetEngine->SyncConnect(info);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Connect to local failed, ret:" << ret << ".");
        return ret;
    }

    mServerOnline.store(true);
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

    ret = RegisterNotifyHandler();
    if (ret != MMS_OK) {
        return ret;
    }

    ret = RegisterClientChannelBrokenHandler();
    if (ret != MMS_OK) {
        return ret;
    }

    ret = StartClientServiceExecutor();
    if (ret != MMS_OK) {
        return ret;
    }

    return ConnectLocalServer();
}

void MmsClient::ClientNetExit(void)
{
    mNetEngine->Stop();
}

BResult MmsClient::StartNotifyCallbackService()
{
    std::lock_guard<std::mutex> lock(mNotifyCallbackServiceLock);
    if (mNotifyCallbackService != nullptr) {
        return MMS_OK;
    }

    mNotifyCallbackService = ExecutorService::Create(NO_1, NO_8192);
    if (mNotifyCallbackService == nullptr) {
        CLIENT_LOG_ERROR("Failed to create notify callback executor.");
        return MMS_ALLOC_FAIL;
    }
    mNotifyCallbackService->SetThreadName("client-data-notify");
    if (!mNotifyCallbackService->Start()) {
        CLIENT_LOG_ERROR("Failed to start notify callback executor.");
        mNotifyCallbackService = nullptr;
        return MMS_INNER_ERR;
    }
    return MMS_OK;
}

BResult MmsClient::StartNotifyChannel()
{
    if (mNotifyChannel != nullptr) {
        return MMS_OK;
    }

    mNotifyPid = static_cast<uint32_t>(getpid()) | NOTIFY_PID_FLAG;
    ConnectInfo info(INVALID_NID, mNotifyPid, INVALID_NID, false);
    auto ret = mNetEngine->ConnectToPeer(CONNECT_IPC, info, mNotifyGroupIndex, mNotifyChannel);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Connect notify channel failed, ret:" << ret << ".");
        mNotifyChannel = nullptr;
        mNotifyPid = 0;
        mNotifyGroupIndex = 0;
        return ret;
    }
    NetChannelUpCtx ctx;
    ctx.peerId = INVALID_NID;
    ctx.procId = NOTIFY_PID_FLAG;
    ctx.groupIndex = mNotifyGroupIndex;
    ctx.isAccepted = 0;
    mNotifyChannel->SetUpCtx(ctx.whole);
    return MMS_OK;
}

BResult MmsClient::RegisterNotifyCallback(NotifyCallback callback)
{
    if (UNLIKELY(!mStarted || mNetEngine == nullptr)) {
        CLIENT_LOG_ERROR("Mms client is not started.");
        return MMS_NOT_READY;
    }

    if (callback != nullptr && !mDataChangeCallbackSwitch) {
        CLIENT_LOG_DEBUG("Data change callback switch is off.");
        return MMS_OK;
    }

    if (callback != nullptr) {
        auto ret = StartNotifyCallbackService();
        if (ret != MMS_OK) {
            return ret;
        }
        ret = StartNotifyChannel();
        if (ret != MMS_OK) {
            return ret;
        }
        mNotifyCallback.store(callback, std::memory_order_release);
    } else {
        mNotifyCallback.store(nullptr, std::memory_order_release);
    }

    NotifySubscribeReq req = {{0, MMS_OP_C_NOTIFY_SUBSCRIBE, 0, 0, 0}, callback != nullptr, mNotifyGroupIndex,
        mNotifyPid};
    BResult rsp = MMS_OK;
    auto ret = mNetEngine->SyncCall<NotifySubscribeReq, BResult>(INVALID_NID, g_groupIndex, MMS_OP_C_NOTIFY_SUBSCRIBE,
                                                                 req, rsp);
    if (UNLIKELY(ret != MMS_OK)) {
        CLIENT_LOG_ERROR("Notify subscribe request failed, ret:" << ret << ".");
        if (callback != nullptr) {
            mNotifyCallback.store(nullptr, std::memory_order_release);
        }
        return ret;
    }
    return MMS_OK;
}

BResult MmsClient::HandleNotifyDataChange(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive notify message len:" << ctx.MessageDataLen() << " or message data invalid.");
        return MMS_OK;
    }

    if (ctx.MessageDataLen() == sizeof(NotifyDataChangeReq)) {
        auto *req = static_cast<NotifyDataChangeReq *>(ctx.MessageData());
        if (UNLIKELY(req->keyLen == 0 || req->keyLen >= MAX_KEY_SIZE || req->opType >= OP_BUTT)) {
            CLIENT_LOG_ERROR("Invalid notify data, keyLen:" << req->keyLen << ", opType:" << req->opType << ".");
            return MMS_OK;
        }
        NotifyDataChangeItem item{req->keyLen, req->opType, {}};
        auto ret = memcpy_s(item.key, MAX_KEY_SIZE, req->key, req->keyLen + NO_1);
        if (UNLIKELY(ret != EOK)) {
            CLIENT_LOG_ERROR("Copy notify key failed, ret:" << ret << ".");
            return MMS_OK;
        }
        return HandleSingleNotifyDataChange(item);
    }

    if (UNLIKELY(ctx.MessageDataLen() != sizeof(NotifyDataChangeBatchReq))) {
        CLIENT_LOG_ERROR("Receive notify message len:" << ctx.MessageDataLen() << " invalid.");
        return MMS_OK;
    }

    auto *req = static_cast<NotifyDataChangeBatchReq *>(ctx.MessageData());
    if (UNLIKELY(req->itemNum == 0 || req->itemNum > NOTIFY_DATA_CHANGE_BATCH_NUM)) {
        CLIENT_LOG_ERROR("Invalid notify batch item num:" << req->itemNum << ".");
        return MMS_OK;
    }
    for (uint16_t index = 0; index < req->itemNum; ++index) {
        auto ret = HandleSingleNotifyDataChange(req->items[index]);
        if (ret != MMS_OK) {
            return ret;
        }
    }
    return MMS_OK;
}

BResult MmsClient::HandleSingleNotifyDataChange(const NotifyDataChangeItem &item)
{
    if (UNLIKELY(item.keyLen == 0 || item.keyLen >= MAX_KEY_SIZE || item.opType >= OP_BUTT)) {
        CLIENT_LOG_ERROR("Invalid notify data, keyLen:" << item.keyLen << ", opType:" << item.opType << ".");
        return MMS_OK;
    }

    if (mNotifyCallback.load(std::memory_order_acquire) == nullptr) {
        return MMS_OK;
    }

    if (UNLIKELY(mNotifyCallbackService == nullptr)) {
        CLIENT_LOG_ERROR("Notify callback executor is not ready.");
        return MMS_OK;
    }

    std::string key(item.key, item.keyLen);
    auto opType = static_cast<OperateType>(item.opType);
    auto ret = mNotifyCallbackService->Execute([this, key, opType]() {
        auto callback = mNotifyCallback.load(std::memory_order_acquire);
        if (callback != nullptr) {
            callback(key.c_str(), opType);
        }
    });
    if (UNLIKELY(!ret)) {
        CLIENT_LOG_ERROR("Put notify callback task to queue failed.");
        return MMS_OK;
    }

    return MMS_OK;
}

BResult MmsClient::ClientBasicInit(void)
{
    BasicRequest req = { { 0, MMS_OP_C_BASIC, 0, 0 } };
    BasicResponse rsp;
    BResult ret = mNetEngine->SyncCall<BasicRequest,
        BasicResponse>(INVALID_NID, NO_0, MMS_OP_C_BASIC, req, rsp);
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
    mArtQuerySwitch = rsp.artQuerySwitch;
    mDataChangeCallbackSwitch = rsp.dataChangeCallbackSwitch;
    mNotifyGroupIndex = rsp.notifyGroupIndex;
    mMaxMsgBuffSize = rsp.maxMsgBuffSize;
    mBlockInfo = {rsp.valueBlockSize};
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
        cfg.valueBlockSize = mBlockInfo.valueBlockSize + sizeof(BlockHeader) + sizeof(DataHeader);
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
    mNotifyChannel = nullptr;
    mNotifyPid = 0;
    mNotifyGroupIndex = 0;
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

void MmsClient::MarkClientOffline()
{
    mStarted = false;
    if (mServiceable) {
        mServiceable = false;
        mServiceCallback(mServiceable);
    }
    mServerOnline.store(false);
}

BResult MmsClient::WaitAndResetResource()
{
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
        CLIENT_LOG_ERROR("Reset resource failed, ret:" << ret << ".");
        return MMS_ERR;
    }
    return MMS_OK;
}

BResult MmsClient::ReconnectLocalServer(uint32_t interval)
{
    uint32_t retryCnt = 0;
    BResult ret = MMS_OK;
    do {
        ConnectInfo info(INVALID_NID, static_cast<uint32_t>(getpid()), INVALID_NID, true);
        ret = mNetEngine->SyncConnect(info);
        if (ret != MMS_OK) {
            sleep(interval);
            CLIENT_LOG_WARN("Connect to local server failed, retry cnt:" << retryCnt++ << ".");
        }
    } while (ret != MMS_OK);

    mServerOnline.store(true);
    return MMS_OK;
}

BResult MmsClient::RebuildServices(uint32_t interval)
{
    uint32_t retryCnt = 0;
    BResult ret = MMS_OK;
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
        }
    } while (ret != MMS_OK);
    return MMS_OK;
}

void MmsClient::ReregisterNotifyCallback()
{
    auto callback = mNotifyCallback.load(std::memory_order_acquire);
    if (callback == nullptr) {
        return;
    }

    auto ret = RegisterNotifyCallback(callback);
    if (ret != MMS_OK) {
        CLIENT_LOG_ERROR("Re-register notify callback failed, ret:" << ret << ".");
    }
}

BResult MmsClient::BuildThreadTask(void)
{
    MarkClientOffline();
    auto ret = WaitAndResetResource();
    if (ret != MMS_OK) {
        return ret;
    }

    constexpr uint16_t interval = 2;
    ret = ReconnectLocalServer(interval);
    if (ret != MMS_OK) {
        return ret;
    }
    ret = RebuildServices(interval);
    if (ret != MMS_OK) {
        return ret;
    }

    mStarted = true;
    ReregisterNotifyCallback();
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
    BResult ret = mNetEngine->SyncCall<ServiceRequest, ServiceResponse>(INVALID_NID, g_groupIndex, MMS_OP_C_SERVICEABLE,
                                                                        req, rsp);
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
        ret = mNetEngine->SyncCall<BasicRequest, BResult>(INVALID_NID, g_groupIndex, MMS_OP_C_CRB_START_CATCH_UP, req,
                                                          rsp);
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

    void *handler = dlopen("libclient_diagnose.so", RTLD_NOW);
    if (handler == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library libclient_diagnose.so, error " << dlerror() << ".");
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
