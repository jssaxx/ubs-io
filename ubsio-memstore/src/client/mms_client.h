/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
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
    uint16_t minBlockSize;
    uint16_t maxBlockSize;
    uint8_t minBlockSizeRate;
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

    BResult MmsGet(uint64_t userId, GetItems *itemList, uint32_t itemNum)
    {
        if (UNLIKELY(!mServiceable)) {
            CLIENT_LOG_WARN("Service is not available.");
            return MMS_NOT_READY;
        }
        return mKvClient->MmsGet(userId, itemList, itemNum);
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

    bool mServiceCheckStarted = false;
    std::atomic<bool> mServerOnline{false};

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
