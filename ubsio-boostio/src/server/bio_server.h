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

#ifndef BIO_SERVER_H
#define BIO_SERVER_H

#include "bio_config_instance.h"
#include "bio_err.h"
#include "bio_ref.h"
#include "bio_tracepoint_helper.h"
#include "cm.h"
#include "mirror_server.h"
#include "mirror_server_crb.h"
#include "net_engine.h"
#include <mutex>
#include <utility>

namespace ock {
namespace bio {
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

class BioServiceProc {
public:
    explicit BioServiceProc(std::vector<ModuleDesc> modules) noexcept : mModules(std::move(modules)) {}

    ~BioServiceProc() noexcept
    {
        mModules.clear();
    }

    BResult Process()
    {
        auto ret = OnServiceInitialize();
        if (ret != BIO_OK) {
            return ret;
        }
        ret = OnServiceStart();
        if (ret != BIO_OK) {
            return ret;
        }
        return BIO_OK;
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
            if (ret == BIO_OK) {
                LOG_INFO("Module (" << it->name << ") initialize success.");
            } else {
                LOG_ERROR("Module (" << it->name << ") initialize failed, result:" << ret << ".");
                BIO_TP_START(NO_PROCESS_ROLLBACK_SERVICE_INIT, 0);
                RollbackInit(it);
                BIO_TP_END;
                return BIO_ERR;
            }
        }
        return BIO_OK;
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
            if (ret == BIO_OK) {
                LOG_INFO("Module (" << it->name << ") start success.");
            } else {
                LOG_ERROR("Module (" << it->name << ") start failed, result:" << ret << ".");
                RollbackStart(it);
                return BIO_ERR;
            }
        }
        return BIO_OK;
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

    DEFINE_REF_COUNT_FUNCTIONS

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
    DEFINE_REF_COUNT_VARIABLE
};
using BioServiceProcPtr = Ref<BioServiceProc>;

class BioServer;
using BioServerPtr = Ref<BioServer>;
class BioServer {
public:
    BioServer() noexcept;

    BResult Start();
    void Exit();

    static BioServerPtr &Instance()
    {
        static auto instance = MakeRef<BioServer>();
        return instance;
    }

    inline NetEnginePtr GetNetEngine()
    {
        return mNetEngine;
    }

    inline MirrorServerPtr GetMirrorServer()
    {
        return mMirror;
    }

    inline MirrorServerCrbPtr GetMirrorCrb()
    {
        return mMirrorCrb;
    }

    inline bool GetCrbProcessing()
    {
        return mCrbProcessing.load();
    }

    inline CmPtr GetCm()
    {
        return mCm;
    }

    inline bool GetCrcFlag()
    {
        return mConfig->GetDaemonConfig().enableCrc;
    }

    inline bool GetCliFlag()
    {
        return mConfig->GetDaemonConfig().enableCli;
    }

    inline bool GetPrometheusToggle()
    {
        return mConfig->GetDaemonConfig().enablePrometheus;
    }

    inline std::string GetPrometheusListenAddress()
    {
        return mConfig->GetDaemonConfig().listenAddress;
    }

    inline uint32_t GetNegoWorkIoTimeOut()
    {
        return mConfig->GetDaemonConfig().workIoTimeOut;
    }

    inline uint32_t GetPrometheusScrapeIntervalSec()
    {
        return mConfig->GetDaemonConfig().scrapeIntervalSec;
    }

    inline BioConfigPtr GetConfig()
    {
        return mConfig;
    }

    inline CmNodeId GetLocalNid()
    {
        return mLocalNid;
    }

    inline uint16_t GetNetProtocol()
    {
        return static_cast<uint16_t>(mConfig->GetNetConfig().protocol);
    }

    inline bool CheckIsOnline(uint16_t nodeId, std::string &ip, uint16_t &port)
    {
        CmNodeId node(mLocalNid.groupId, nodeId);
        bool isOnline = false;
        if (mNodeView.find(node) != mNodeView.end()) {
            if (mNodeView[node].status == CM_NODE_NORMAL) {
                isOnline = true;
                ip = mNodeView[node].ip;
                port = mNodeView[node].port;
            }
            return isOnline;
        }
        return false;
    }

    inline bool GetServiceState()
    {
        return mCm->GetServiceState();
    }

    inline std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> GetNodeView(uint64_t *curNodeTimes)
    {
        std::lock_guard<std::mutex> lock(mNodeViewMutex);
        *curNodeTimes = mCurNodeTimes;
        return mNodeView;
    }

    BResult GetDiskStatusFromNodeView(uint16_t diskId, CmDiskStatus &diskStatus)
    {
        std::lock_guard<std::mutex> lock(mNodeViewMutex);
        if (mNodeView.find(mLocalNid) != mNodeView.end()) {
            CmNodeInfo nodeInfo = mNodeView[mLocalNid];
            for (uint32_t idx = 0; idx < nodeInfo.disks.size(); idx++) {
                if (nodeInfo.disks[idx].diskId == diskId) {
                    diskStatus = nodeInfo.disks[idx].diskStatus;
                    return BIO_OK;
                }
            }
        }
        LOG_ERROR("Get disk status failed, diskId " << diskId);
        return BIO_ERR;
    }

    inline std::map<uint16_t, CmPtInfo> GetPtView(uint64_t *curPtTimes)
    {
        std::lock_guard<std::mutex> lock(mPtViewMutex);
        *curPtTimes = mCurPtTimes;
        return mPtView;
    }

    inline CmPtInfo GetPtEntry(uint16_t ptId)
    {
        std::lock_guard<std::mutex> lock(mPtViewMutex);
        if (mPtView.find(ptId) == mPtView.end()) {
            return CmPtInfo();
        }
        return mPtView[ptId];
    }

    inline uint64_t GetLocalMrKey()
    {
        uint64_t key = 0;
        mNetEngine->GetLocalMrKey(key);
        return key;
    }

    inline BResult MemAlloc(uint64_t size, NetMrInfo &mr)
    {
        if (size > mNetEngine->GetDataPage()) {
            return BIO_ALLOC_FAIL;
        }
        auto ret = mNetEngine->AllocLocalMrSingle(mr.address, mr.key);
        mr.size = size;
        return ret;
    }

    inline BResult MemAlloc(uint64_t size, uint64_t *addr)
    {
        uintptr_t address;
        uint64_t outKey;
        auto ret = mNetEngine->AllocLocalMrSingle(address, outKey);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }
        *addr = address;
        return BIO_OK;
    }

    inline void MemFree(uint64_t addr)
    {
        mNetEngine->FreeLocalMrSingle(addr);
    }

    inline BResult GetNodeInfo(CmNodeId nid, CmNodeInfo &nodeInfo)
    {
        if (UNLIKELY(mCm == nullptr)) {
            return BIO_NOT_READY;
        }
        return mCm->GetNodeInfo(nid, nodeInfo);
    }

    inline void* LoadFunction(const char *name, void *handler)
    {
        void *ptr = nullptr;
        ptr = dlsym(handler, name);
        return ptr;
    }

    BResult HandleCmNodeEvent(const std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeInfos);

    BResult BioBdmUpdate(std::string diskPath);

    BResult BioDiskReset(uint16_t diskId);

    DEFINE_REF_COUNT_FUNCTIONS;

protected:
    BResult BioConfigInit();
    BResult BioLoggerInit(std::string pathName);
    void BioLoggerExit();
    BResult BioTraceInit();
    void BioTraceExit();
    BResult BioUnderFsInit();
    void BioUnderFsExit();
    BResult BioBdmInit();
    void BioBdmExit();
    BResult BioNetInit();
    void BioNetExit();
    BResult BioCmInit();
    void BioCmExit();
    BResult BioMirrorServerInit();
    void BioMirrorServerExit();
    BResult BioCacheInit();
    void BioCacheExit();
    BResult BioFlowInit();
    void BioFlowExit();
    BResult BioServerDiagnoseInit();
    BResult BioServerDiagnoseInitInner();
#ifdef USE_DEBUG_TP_TOOLS
    BResult BioServerTracePointInit();
#endif
    void Connection();
    BResult HandleCmPtEvent(const std::map<uint16_t, CmPtInfo> &ptInfos);
    bool CheckNeedCrb(const std::map<uint16_t, CmPtInfo> &ptInfos);

private:
    BResult StartRpcService(const NetOptions &opt);
    BResult StartIpcService(const NetOptions &opt);
    void ReConnect(uint32_t peerId);

private:
    bool mStarted = false;
    std::mutex mStartLock;
    BioServiceProcPtr mService = nullptr;
    BioConfigPtr mConfig = nullptr;
    NetEnginePtr mNetEngine = nullptr;
    CmPtr mCm = nullptr;
    MirrorServerPtr mMirror = nullptr;
    MirrorServerCrbPtr mMirrorCrb = nullptr;
    CmNodeId mLocalNid;
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> mNodeView;
    std::map<uint16_t, CmPtInfo> mPtView;
    std::atomic<bool> mCrbProcessing{false};
    std::mutex mNodeViewMutex;
    std::mutex mPtViewMutex;
    uint64_t mCurNodeTimes = 0;
    uint64_t mCurPtTimes = 0;

    bool mNetEngineInited = false;
    bool mCacheInited = false;
    bool mMirrorInited = false;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // BIO_SERVER_H