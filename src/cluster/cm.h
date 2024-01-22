/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef BOOSTIO_BIO_CM_H
#define BOOSTIO_BIO_CM_H

#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <functional>
#include <sstream>

#include "bio_log.h"
#include "bio_ref.h"
#include "bio_err.h"
#include "bio_lock.h"
#include "bio_types.h"

#include "cm_inner.h"

namespace ock {
namespace bio {
enum CmRole : uint16_t {
    ROLE_CMM = 1,
    ROLE_DATA = 2,
    ROLE_TOGETHER = 3,
};

struct CmGroups {
    uint16_t groupId;
    uint16_t replicaNum;
    uint16_t initialNodeNum;
    uint16_t maxNodeNum;
    uint16_t maxPtNum;
};

struct CmOptions {
    CmRole role;
    std::string zkIpMask;

    uint32_t hbTempTimeout = NO_60;
    uint32_t hbPermFaultTime = NO_1024;

    CmGroups groups;
};

enum CmStatus : uint16_t {
    CM_INIT = 0,
    CM_NORMAL = 1,
    CM_UPDATING = 2,
};

enum CmNodeStatus : uint16_t {
    CM_NODE_NORMAL = 0,
    CM_NODE_FAULT = 1,
};

enum CmDiskStatus : uint16_t {
    CM_DISK_NORMAL = 0,
    CM_DISK_FAULT = 1,
};

struct CmDiskInfo {
    uint16_t diskId;
    CmDiskStatus diskStatus;
};

union CmNodeId {
    struct {
        uint32_t groupId : NO_8;
        uint32_t nodeId : NO_24;
    };
    uint32_t whole = 0;

    CmNodeId() = default;
    explicit CmNodeId(uint32_t w) : whole(w) {}

    CmNodeId(uint32_t gId, uint32_t vId)
    {
        groupId = gId;
        nodeId = vId;
    }

    inline uint32_t GroupId() const
    {
        return groupId;
    }

    inline uint32_t VNodeId() const
    {
        return nodeId;
    }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "groupId " << groupId << ", nodeId " << nodeId << ", whole " << whole;
        return oss.str();
    }
};

struct CmNodeIdCmp {
    bool operator () (const CmNodeId &first, const CmNodeId &second) const
    {
        return first.whole < second.whole;
    }
};

struct CmNodeInfo {
    CmNodeId id { 0 };
    std::string ip;
    uint16_t port { 0 };
    CmNodeStatus status { CM_NODE_NORMAL };
    std::vector<CmDiskInfo> disks;

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "node " << id.ToString() << ", ip " << ip.c_str() << ", port " << port << ", status " <<
            status << ", disknum " << disks.size();
        for (uint32_t idx = 0; idx < disks.size(); idx++) {
            oss << ", disk " << disks[idx].diskId << ", status " << disks[idx].diskStatus;
        }
        return oss.str();
    }
};

enum CmCopyState : uint16_t {
    CM_COPY_INIT = 0,
    CM_COPY_RUNNING = 1,
    CM_COPY_DOWN = 2,
    CM_COPY_OUT = 3,
    CM_COPY_RECOVERY = 4,
    CM_COPY_BUTT
};

struct CmPtCopy {
    uint16_t nodeId;
    uint16_t diskId;
    CmCopyState state;
};

enum CmPtState : uint16_t {
    CM_PT_INIT = 0,
    CM_PT_NORMAL = 1,
    CM_PT_DEGRADE_LOSS1 = 2,
    CM_PT_DEGRADE_LOSS2 = 3,
    CM_PT_FAULT = 4,
    CM_PT_BUTT
};

struct CmPtInfo {
    uint64_t version;
    uint16_t ptId;
    CmPtState state;
    uint16_t masterNodeId;
    uint16_t masterDiskId;
    std::vector<CmPtCopy> copys;

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "ptid " << ptId << ", version " << version << ", state " << state << ", master " << masterNodeId <<
            ", copynum " << copys.size();
        for (uint32_t idx = 0; idx < copys.size(); idx++) {
            oss << ", node " << copys[idx].nodeId << ", disk " << copys[idx].diskId << ", state " << copys[idx].state;
        }
        return oss.str();
    }
};

struct CmPtFinish {
    uint64_t version;
    uint16_t ptId;
};

using CmNodeHandler = std::function<BResult(const std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeInfos)>;
using CmPtHandler = std::function<BResult(const std::map<uint16_t, CmPtInfo> &ptInfos)>;

class Cm;
using CmPtr = Ref<Cm>;
class Cm {
public:
    Cm() = default;
    ~Cm() = default;

    static CmPtr &Instance()
    {
        static auto instance = MakeRef<Cm>();
        return instance;
    }

    BResult Initialize(const CmOptions &opt);

    BResult Start();

    void Stop();

    inline CmStatus GetCmStatus() const
    {
        return mStatus;
    }

    inline CmNodeId GetCmLocalNodeId() const
    {
        return mNodeId;
    }

    inline BResult GetNodeInfo(CmNodeId nid, CmNodeInfo &nodeInfo)
    {
        ReadLocker<ReadWriteLock> lock(&mLock);
        if (mNodeInfos.find(nid) != mNodeInfos.end()) {
            nodeInfo = mNodeInfos[nid];
            return BIO_OK;
        }
        LOG_ERROR("Invalid, nodeId:" << nid.ToString());
        return BIO_ERR;
    }

    inline BResult AssignPtAffinity(uint16_t &ptId)
    {
        ReadLocker<ReadWriteLock> lock(&mLock);
        if (mLocals.size() == 0) {
            return BIO_ERR;
        }
        ptId = mLocals[mLocalIdx++ % mLocals.size()];
        return BIO_OK;
    }

    inline BResult AssignPtByHash(size_t hashValue, uint16_t &ptId)
    {
        ReadLocker<ReadWriteLock> lock(&mLock);
        if (mPtInfos.size() == 0) {
            return BIO_ERR;
        }
        ptId = mPtInfoIdx++ % mPtInfos.size();
        return BIO_OK;
    }

    inline BResult GetPtInfo(uint16_t ptId, CmPtInfo &ptInfo)
    {
        ReadLocker<ReadWriteLock> lock(&mLock);
        if (mPtInfos.find(ptId) != mPtInfos.end()) {
            ptInfo = mPtInfos[ptId];
            return BIO_OK;
        }
        return BIO_ERR;
    }

    BResult ReportDiskStatus(uint16_t diskId, CmDiskStatus status);

    BResult ReportPtFinish(std::vector<CmPtFinish> &ptFinish);

    BResult RegisterNode(CmNodeInfo &node);

    BResult RegisterNodeHandler(const CmNodeHandler &nodeHandler);

    BResult RegisterPtHandler(const CmPtHandler &ptHandler);

    DEFINE_REF_COUNT_FUNCTIONS;

    static int32_t QueryLocalNodeInfo(NodeInfo *nodeInfo, void *ctx);
    static int32_t NotifyNodeListChange(NodeStateList *nodeList, void *ctx);
    static int32_t NotifyPtListChange(PtEntryList *ptList, void *ctx);

    void ScanPtListAffinity();

public:
    CmOptions mOptions;
    CmNodeInfo mNode;
    CmNodeHandler mNodeHandler;
    CmPtHandler mPtHandler;

    CmNodeId mNodeId;

    std::atomic<CmStatus> mStatus = { CM_INIT };

    uint64_t mNodeVersion { 0 };
    std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> mNodeInfos;

    uint64_t mPtVersion { 0 };
    std::map<uint16_t, CmPtInfo> mPtInfos;
    std::atomic<uint16_t> mPtInfoIdx = { 0 };

    std::vector<uint16_t> mLocals;
    std::atomic<uint16_t> mLocalIdx = { 0 };

    ReadWriteLock mLock;
    bool mStarted { false };
    bool mInited { false };
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // BOOSTIO_BIO_CM_H
