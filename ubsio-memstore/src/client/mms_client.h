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

#ifndef MMS_CLIENT_H
#define MMS_CLIENT_H

#include <atomic>
#include <mutex>

#include "net_engine.h"
#include "net_common.h"
#include "mms_execution.h"
#include "mms_client_log.h"
#include "mms_kv_client.h"
#include "mms_notify_shm_client.h"
#ifdef USE_CLI_TOOLS
#include "cli.h"
#include "client_diagnose.h"
#endif

namespace ock {
namespace mms {

struct DataBlockInfo {
    uint32_t valueBlockSize;
};

class MmsClient;
using MmsClientPtr = Ref<MmsClient>;
class MmsClient {
public:
    static MmsClientPtr &Instance()
    {
        static auto instance = MakeRef<MmsClient>();
        return instance;
    }

    BResult Initialize(const MmsOptions &options, ServiceCallback service);
    void Exit();

    BResult MmsPut(PutItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsPut(itemList, itemNum);
    }

    BResult MmsGet(GetItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsGet(itemList, itemNum);
    }

    BResult MmsUpdate(UpdateItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsUpdate(itemList, itemNum);
    }

    BResult MmsDelete(DeleteItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsDelete(itemList, itemNum);
    }

    BResult MmsReplace(ReplaceItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsReplace(itemList, itemNum);
    }

    BResult MmsStartCatchUpTask(void);
    BResult RegisterNotifyCallback(NotifyCallback callback, void *lpUserData);

private:
    void BackCheckStateTask();
    BResult ClientGlobVarInit(void);
    BResult ClientLoggerInit(void);
    BResult ClientNetInit(const MmsOptions &options);
    void ClientNetExit(void);
    BResult ClientBasicInit(void);
    BResult InitMemMgr();
    BResult ClientMemInit(void);
    void ClientMemExit(void);
    BResult ClientCacheInit(void);
    void ClientCacheExit(void);
    BResult ClientKvInit(void);
    void ClientKvExit(void);
#ifdef USE_CLI_TOOLS
    BResult ClientDiagnoseInit(void);
    void ClientDiagnoseExit(void);
#endif
    BResult InitClientBase(const MmsOptions &options);
    BResult InitClientDataPath(void);
    BResult BuildThreadTask(void);
    BResult ResetResource();
    BResult BuildServices(void);

    BResult CheckServiceState(std::atomic<bool> &serviceable);
    BResult InitExpireChecker(const MmsOptions &options);
    BResult RegisterClientChannelBrokenHandler();
    BResult StartClientServiceExecutor();
    BResult ConnectLocalServer();
    void HandleClientChannelBroken();
    void MarkClientOffline();
    BResult WaitAndResetResource();
    BResult ReconnectLocalServer(uint32_t interval);
    BResult RebuildServices(uint32_t interval);
    BResult ReregisterNotifyCallback(uint32_t interval);
    BResult StartNotifyConsumerLocked();
    void DestroyStartService();

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    bool mStarted = false;
    ExecutorServicePtr mStartService{ nullptr };

    MmsKvClientPtr mKvClient = nullptr;
    CachePtr mCache = nullptr;
    NetEnginePtr mNetEngine = nullptr;
    MmsMemMgrPtr mMemMgr = nullptr;
    MmsMemAllocatorPtr mMemAllocator = nullptr;

    MmsOptions mOptions;

    uint16_t mNumaNum = 0;
    uint16_t mNumaId[MAX_NUMAS_NUM] = {0};
    uint64_t mNumaSize[MAX_NUMAS_NUM] = {0};
    uint64_t mIoCtxNumaSize[MAX_NUMAS_NUM] = {0};
    uint64_t mClientGeneration = 0;
    int32_t mAreaFd[MMAP_AREA_BUTT] = {-1, -1, -1, -1};
    uint32_t mIoTimeOut = 0;
    int32_t mLogLevel = 0;
    bool mKeyRouteEnabled = false;
    bool mDataChangeCallbackSwitch = false;
    uint32_t mMaxMsgBuffSize;
    DataBlockInfo mBlockInfo{};

    std::atomic<bool> mServiceable {false};
    ServiceCallback mServiceCallback = nullptr;
    std::atomic<NotifyCallback> mNotifyCallback {nullptr};
    std::atomic<void *> mNotifyUserData {nullptr};
    std::mutex mNotifyMutex;
    MmsNotifyShmConsumer mNotifyShmConsumer;
    uint32_t mServerPid = 0;

    std::atomic<bool> mServiceCheckStarted{false};
    std::atomic<bool> mServerOnline{false};
#ifdef USE_CLI_TOOLS
    bool mClientDiagnoseInited = false;
#endif

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
