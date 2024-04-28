/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BIO_SERVER_H
#define BIO_SERVER_H

#include <mutex>
#include <utility>
#include "bio_err.h"
#include "bio_ref.h"
#include "bio_config_instance.h"
#include "net_engine.h"
#include "cm.h"
#include "mirror_server.h"
#include "mirror_server_crb.h"
#ifdef USE_DEBUG_TOOLS
#include "bio_tracepoint_helper.h"
#endif

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
                LVOS_TP_START(NO_PROCESS_ROLLBACK_SERVICE_INIT, 0);
                RollbackInit(it);
                LVOS_TP_END;
                return BIO_ERR;
            }
        }
        return BIO_OK;
    }

    BResult OnServiceStart()
    {
        for (auto it = mModules.cbegin(); it != mModules.cend(); ++it) {
            int32_t ret = 0;
            LVOS_TP_START(SERVICE_START_FAIL, &ret, -1);
            if (it->start == nullptr) {
                LOG_INFO("Module (" << it->name << ") no start function, skip.");
                continue;
            }
            LOG_INFO("Module (" << it->name << ") start begin...");
            ret = it->start();
            LVOS_TP_END;
            if (ret == BIO_OK) {
                LOG_INFO("Module (" << it->name << ") start success.");
            } else {
                LOG_ERROR("Module (" << it->name << ") start failed, result:" << ret << ".");
                LVOS_TP_START(NO_PROCESS_ROLLBACK_SERVICE_START, 0);
                RollbackStart(it);
                LVOS_TP_END;
                return BIO_ERR;
            }
        }
        return BIO_OK;
    }

    BResult OnServiceShutdown()
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
        return BIO_OK;
    }

    BResult OnServiceUninitialize()
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
        return BIO_OK;
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
    void Stop();

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

    inline std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> GetNodeView(uint64_t *curNodeTimes)
    {
        *curNodeTimes = mCurNodeTimes;
        return mNodeView;
    }

    BResult GetDiskStatusFromNodeView(uint16_t diskId, CmDiskStatus &diskStatus)
    {
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
        *curPtTimes = mCurPtTimes;
        return mPtView;
    }

    inline CmPtInfo GetPtEntry(uint64_t ptId)
    {
        if (mPtView.find(ptId) == mPtView.end()) {
            return CmPtInfo();
        }
        return mPtView[ptId];
    }

    inline uint32_t GetLocalMrKey()
    {
        uint32_t key = 0;
        mNetEngine->GetLocalMrKey(key);
        return key;
    }

    inline BResult MemAlloc(uint64_t size, NetMrInfo &mr)
    {
        auto ret = mNetEngine->AllocLocalMrSingle(mr.address, mr.key);
        mr.size = size;
        return ret;
    }

    inline BResult MemAlloc(uint64_t size, uint64_t *addr)
    {
        uintptr_t address;
        uint32_t outKey;
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

    BResult GetHbInfo(uint64_t *curNodeTimes, uint64_t *curPtTimes)
    {
        *curNodeTimes = mCurNodeTimes;
        *curPtTimes = mCurPtTimes;
        return BIO_OK;
    }

    inline BResult GetNodeInfo(CmNodeId nid, CmNodeInfo &nodeInfo)
    {
        if (UNLIKELY(mCm == nullptr)) {
            return BIO_NOT_READY;
        }
        return mCm->GetNodeInfo(nid, nodeInfo);
    }

    DEFINE_REF_COUNT_FUNCTIONS;

protected:
    BResult BioConfigInit();
    BResult BioLoggerInit(std::string pathName);
    BResult BioTraceInit();
    void BioTraceExit();
    BResult BioUnderFsInit();
    BResult BioBdmInit();
    void BioBdmExit();
    BResult BioNetInit();
    void BioNetExit();
    BResult BioCmInit();
    void BioCmExit();
    BResult BioMirrorServerInit();
    void BioMirrorServerExit();
    BResult BioCacheInit();
    BResult BioFlowInit();
    BResult BioInterceptorServerInit();
#ifdef USE_DEBUG_TOOLS
    BResult BioServerDiagnoseInit();
    BResult BioServerDiagnoseInitInner();
    BResult BioServerTracePointInit();
#endif
    void Connection();
    BResult HandleCmNodeEvent(const std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeInfos);
    BResult HandleCmPtEvent(const std::map<uint16_t, CmPtInfo> &ptInfos);

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
    uint64_t mCurNodeTimes = 0;
    uint64_t mCurPtTimes = 0;

    bool mNetEngineInited = false;
    bool mCacheInited = false;
    bool mInterceptorInited = false;
    bool mMirrorInited = false;
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // BIO_SERVER_H