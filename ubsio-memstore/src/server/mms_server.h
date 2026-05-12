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

#ifndef MMS_SERVER_H
#define MMS_SERVER_H

#include <mutex>
#include <utility>
#include "mms_err.h"
#include "mms_ref.h"
#include "mms_config_instance.h"
#include "mms_mem_mgr.h"
#include "mms_mem_allocator.h"
#include "mms_cache.h"
#include "net_engine.h"
#include "net_multicast_engine.h"
#include "cm.h"
#include "mms_crb_scheduler.h"
#include "mms_kv_server.h"

namespace ock {
namespace mms {
using InitFunc = std::function<int32_t()>;
using StartFunc = std::function<int32_t()>;
using ShutdownFunc = std::function<int32_t()>;
using ExitFunc = std::function<void()>;

struct ModuleDesc {
    std::string name;
    InitFunc init;
    StartFunc start;
    ShutdownFunc shutdown;
    ExitFunc exit;
    ModuleDesc() = default;
    ModuleDesc(std::string name, InitFunc initFunc, StartFunc startFunc, ShutdownFunc shutdownFunc, ExitFunc exitFunc)
        : name(std::move(name)),
          init(std::move(initFunc)),
          start(std::move(startFunc)),
          shutdown(std::move(shutdownFunc)),
          exit(std::move(exitFunc))
    {}
};

class MmsServiceProc {
public:
    explicit MmsServiceProc(std::vector<ModuleDesc> modules) noexcept : mModules(std::move(modules)) {}

    ~MmsServiceProc() noexcept
    {
        mModules.clear();
    }

    BResult Process()
    {
        auto ret = OnServiceInitialize();
        if (ret != MMS_OK) {
            return ret;
        }
        ret = OnServiceStart();
        if (ret != MMS_OK) {
            return ret;
        }
        return MMS_OK;
    }

    void Exit()
    {
        OnServiceShutdown();
        OnServiceUninitialize();
    }

    BResult OnServiceInitialize()
    {
        for (auto it = mModules.cbegin(); it != mModules.cend(); ++it) {
            if (it->init == nullptr) {
                LOG_INFO("Module (" << it->name << ") no initialize function, skip.");
                continue;
            }
            LOG_INFO("Module (" << it->name << ") initialize begin...");
            auto ret = it->init();
            if (ret == MMS_OK) {
                LOG_INFO("Module (" << it->name << ") initialize success.");
            } else {
                LOG_ERROR("Module (" << it->name << ") initialize failed, result:" << ret << ".");
                RollbackInit(it);
                return MMS_ERR;
            }
        }
        return MMS_OK;
    }

    BResult OnServiceStart()
    {
        for (auto it = mModules.cbegin(); it != mModules.cend(); ++it) {
            if (it->start == nullptr) {
                LOG_INFO("Module (" << it->name << ") no start function, skip.");
                continue;
            }
            LOG_INFO("Module (" << it->name << ") start begin...");
            int32_t ret = it->start();
            if (ret == MMS_OK) {
                LOG_INFO("Module (" << it->name << ") start success.");
            } else {
                LOG_ERROR("Module (" << it->name << ") start failed, result:" << ret << ".");
                RollbackStart(it);
                return MMS_ERR;
            }
        }
        return MMS_OK;
    }

    void OnServiceShutdown()
    {
        for (auto it = mModules.crbegin(); it != mModules.crend(); ++it) {
            if (it->shutdown == nullptr) {
                LOG_INFO("Module (" << it->name << ") no shutdown function, skip.");
                continue;
            }
            LOG_INFO("Module (" << it->name << ") shutdown begin...");
            it->shutdown();
            LOG_INFO("Module (" << it->name << ") shutdown finished.");
        }
    }

    void OnServiceUninitialize()
    {
        for (auto it = mModules.crbegin(); it != mModules.crend(); ++it) {
            if (it->exit == nullptr) {
                LOG_INFO("Module (" << it->name << ") no exit function, skip.");
                continue;
            }
            LOG_INFO("Module (" << it->name << ") exit begin...");
            it->exit();
            LOG_INFO("Module (" << it->name << ") exit finished.");
        }
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    void RollbackInit(const std::vector<ModuleDesc>::const_iterator &end) noexcept
    {
        auto next = end;
        auto pos = end;
        for (--pos; next != mModules.cbegin(); --next, --pos) {
            if (pos->exit != nullptr) {
                pos->exit();
            }
        }
    }

    void RollbackStart(const std::vector<ModuleDesc>::const_iterator &end) noexcept
    {
        auto next = end;
        auto pos = end;
        for (--pos; next != mModules.cbegin(); --next, --pos) {
            if (pos->shutdown != nullptr) {
                pos->shutdown();
            }
        }
    }

private:
    std::vector<ModuleDesc> mModules;
    DEFINE_REF_COUNT_VARIABLE;
};
using MmsServiceProcPtr = Ref<MmsServiceProc>;

class MmsServer;
using MmsServerPtr = Ref<MmsServer>;
class MmsServer {
public:
    MmsServer() noexcept;

    BResult Start(ServiceCallback service);
    void Exit();

    static MmsServerPtr &Instance()
    {
        static auto instance = MakeRef<MmsServer>();
        return instance;
    }

    inline NetEnginePtr GetNetEngine()
    {
        return mNetEngine;
    }

    inline NetMulticastEnginePtr GetMulticastEngine()
    {
        return mMulticastEngine;
    }

    inline MmsMemMgrPtr GetMemMgr()
    {
        return mMemMgr;
    }

    inline MmsMemAllocatorPtr GetMemAllocator()
    {
        return mIoCtxMemAllocator;
    }

    inline CachePtr GetCache()
    {
        return mCache;
    }

    inline MmsKvServerPtr GetKvServer()
    {
        return mKvServer;
    }

    inline CmPtr GetCm()
    {
        return mCm;
    }

    inline MmsConfigPtr GetConfig()
    {
        return mConfig;
    }

    inline const CrbSchedulerPtr GetCrbScheduler() const
    {
        return mCrbSchedulerPtr;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

protected:
    BResult MmsConfigInit();
    BResult MmsLoggerInit(std::string pathName);
    void MmsLoggerExit();
    BResult InitMemMgr();
    BResult InitIndexMemAllocator();
    BResult InitValueMemAllocator();
    BResult InitIOCtxMemAllocator();
    BResult MmsMemInit();
    void MmsMemExit();
    BResult MmsMulticastNetInit();
    void FillNetOptions(NetOptions &netOptions);
    BResult MmsUnicastNet();
    BResult MmsNetInit();
    void MmsNetExit();
    BResult MmsCacheInit();
    void MmsCacheExit();
    BResult MmsCmInit();
    void MmsCmExit();
    BResult MmsKvServerInit();
    void MmsKvServerExit();
    BResult MmsCrbSchedulerInit();
    void MmsCrbSchedulerExit();
#ifdef USE_CLI_TOOLS
    BResult MmsServerDiagnoseInit();
#endif
    BResult HandleNodeEvent(const std::map<uint16_t, CmNodeInfo> &nodeInfos);
    BResult HandlePtMigrateEvent(uint16_t ptId);
    BResult HandleCmPtEvent(const std::map<uint16_t, CmPtInfo> &ptInfos, bool serviceable);
    void RunPtMigrateTask(uint16_t ptId);
    void CreateSubscribers(const std::map<uint16_t, CmNodeInfo> &nodeInfos);
    void ReCreateSubscriber(uint16_t peerNodeId);
    void NetConnect(const std::map<uint16_t, CmNodeInfo> &nodeInfos);
    void NetReConnect(uint32_t peerId);
    void NotifyServiceable(bool serviceable);

private:
    bool mStarted = false;
    std::mutex mStartLock;
    MmsServiceProcPtr mService = nullptr;
    MmsConfigPtr mConfig = nullptr;
    NetEnginePtr mNetEngine = nullptr;
    NetMulticastEnginePtr mMulticastEngine = nullptr;
    MmsMemMgrPtr mMemMgr = nullptr;
    MmsMemAllocatorPtr mMemAllocator = nullptr;
    MmsMemAllocatorPtr mIoCtxMemAllocator = nullptr;
    MmsMemAllocatorPtr mIndexMemAllocator = nullptr;
    CachePtr mCache = nullptr;
    CmPtr mCm = nullptr;
    MmsKvServerPtr mKvServer = nullptr;
    uint64_t mCurNodeTimes = 0;
    uint64_t mCurPtTimes = 0;

    std::atomic<bool> mIsFirst{true};
    std::atomic<bool> mServiceable{false};
    ServiceCallback mServiceCallback = nullptr;
    ExecutorServicePtr mTaskService{ nullptr };

    CrbSchedulerPtr mCrbSchedulerPtr = nullptr;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // MMS_SERVER_H
