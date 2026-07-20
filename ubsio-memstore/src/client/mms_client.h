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

#include "net_engine.h"
#include "net_common.h"
#include "mms_execution.h"
#include "mms_client_log.h"
#include "mms_kv_client.h"
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

    BResult MmsPut(uint64_t userId, PutItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsPut(userId, itemList, itemNum);
    }

    BResult MmsGet(uint64_t userId, GetItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsGet(userId, itemList, itemNum);
    }

    BResult MmsUpdate(uint64_t userId, UpdateItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsUpdate(userId, itemList, itemNum);
    }

    BResult MmsDelete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsDelete(userId, itemList, itemNum);
    }

    BResult MmsReplace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsReplace(userId, itemList, itemNum);
    }

    BResult MmsStartCatchUpTask(void);

private:
    void BackCheckStateTask();
    BResult ClientGlobVarInit(void);
    BResult ClientLoggerInit(void);
    void ClientLoggerExit(void);
    BResult ClientNetInit(const MmsOptions &options);
    void ClientNetExit(void);
    BResult ClientBasicInit(void);
    void ClientBasicExit(void);
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
    BResult BuildThreadTask(void);
    BResult ResetResource();
    BResult BuildServices(void);

    BResult CheckServiceState(std::atomic<bool> &serviceable);
    void DestroyStartService(void);

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
    uint16_t mNumaId[MAX_NUMAS_NUM];
    uint64_t mNumaSize[MAX_NUMAS_NUM];
    int32_t mAreaFd[MMAP_AREA_BUTT];
    uint32_t mIoTimeOut;
    int32_t mLogLevel;
    bool mEnableCrc;
    uint32_t mMaxMsgBuffSize;
    DataBlockInfo mBlockInfo{};

    std::atomic<bool> mServiceable {false};
    ServiceCallback mServiceCallback = nullptr;

    std::atomic<bool> mServiceCheckStarted{false};
    std::atomic<bool> mServerOnline{false};
#ifdef USE_CLI_TOOLS
    void *mClientDiagnoseHandler = nullptr;
#endif

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
