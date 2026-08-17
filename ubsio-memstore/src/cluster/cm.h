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
#ifndef MMSCORE_MMS_CM_H
#define MMSCORE_MMS_CM_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <sstream>
#include <iomanip>

#include "mms_log.h"
#include "mms_ref.h"
#include "mms_err.h"
#include "mms_lock.h"
#include "mms_types.h"

#include "cm_inner.h"

namespace ock {
namespace mms {

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

    uint32_t nodeId;
    uint32_t hbTempTimeout = NO_30;
    uint32_t hbPermFaultTime = NO_MAX_VALUE32;

    CmGroups groups;
};

enum CmServiceStatus : uint16_t {
    CM_NORMAL = 0,
    CM_ABNORMAL = 1,
};

enum CmNodeStatus : uint16_t {
    CM_NODE_NORMAL = 0,
    CM_NODE_FAULT = 1,
};

struct CmNodeInfo {
    uint16_t id{ 0 };
    std::string ip;
    uint16_t port{ 0 };
    uint16_t multiPort{ 0 };
    CmNodeStatus status{ CM_NODE_NORMAL };
    std::vector<uint16_t> numas;

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "node: id " << std::setw(NO_2) << id << ", ip " << ip.c_str() << ", port " << port << ", multiPort "
            << multiPort << ", status " << status << ", numa num " << numas.size();
        oss << ", numa list [";
        for (uint32_t idx = 0; idx < numas.size(); idx++) {
            oss << numas[idx];
            if (idx != numas.size() - 1) {
                oss << " ";
            }
        }
        oss << "].";
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
}; // 同 PtCopyState 定义

struct CmPtCopy {
    uint16_t nodeId : 8;
    uint16_t state : 8; // 见 CmCopyState
};

enum CmPtState : uint16_t {
    CM_PT_INIT = 0,
    CM_PT_NORMAL = 1,
    CM_PT_MST_MIGRATE = 2,
    CM_PT_FAULT = 3,
    CM_PT_BUTT
};

struct CmPtInfo {
    uint64_t version = 0;
    uint16_t ptId;
    CmPtState state{ CM_PT_INIT };
    uint16_t masterNodeId;
    uint16_t resv[2L];
    std::vector<CmPtCopy> copys;

    void Clone(const CmPtInfo &ptInfo)
    {
        version = ptInfo.version;
        masterNodeId = ptInfo.masterNodeId;
        copys.reserve(ptInfo.copys.size());
        copys.insert(copys.end(), ptInfo.copys.begin(), ptInfo.copys.end());
        return;
    }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "pt id " << std::setw(NO_2) << ptId << ", version " << version << ", master " <<
            masterNodeId << ", copy num " << copys.size() << ", pt state " << state;
        oss << ", copy list [node-state][";
        for (uint32_t idx = 0; idx < copys.size(); idx++) {
            oss << copys[idx].nodeId << "-" << copys[idx].state;
            if (idx != copys.size() - 1) {
                oss << " ";
            }
        }
        oss << "].";
        return oss.str();
    }
};

using CmNodeHandler = std::function<BResult(const std::map<uint16_t, CmNodeInfo> &nodeInfos)>;
using CmPtHandler = std::function<BResult(const std::map<uint16_t, CmPtInfo> &ptInfos, bool serviceable)>;
using CmPtMigrateHandler = std::function<BResult(uint16_t ptId)>;

struct CmKeyRoute {
    const char *key;
    uint16_t keyLen;
};

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

    inline CmServiceStatus GetServiceStatus()
    {
        return mStatus;
    }

    inline uint16_t GetLocalNid()
    {
        return mNodeId;
    }

    inline uint16_t GetMasterNode()
    {
        return mMasterNode;
    }

    inline BResult GetNodeInfo(uint16_t nid, CmNodeInfo &nodeInfo)
    {
        ReadLocker<ReadWriteLock> lock(&mNLock);
        if (mNodeInfos.find(nid) != mNodeInfos.end()) {
            nodeInfo = mNodeInfos[nid];
            return MMS_OK;
        }
        LOG_ERROR("Invalid, nodeId:" << nid);
        return MMS_ERR;
    }

    inline bool CheckIsOnline(uint16_t nodeId, std::string &ip, uint16_t &port)
    {
        bool isOnline = false;
        ReadLocker<ReadWriteLock> lock(&mNLock);
        if (mNodeInfos.find(nodeId) != mNodeInfos.end()) {
            if (mNodeInfos[nodeId].status == CM_NODE_NORMAL) {
                isOnline = true;
                ip = mNodeInfos[nodeId].ip;
                port = mNodeInfos[nodeId].port;
            }
            return isOnline;
        }
        return false;
    }

    inline bool CheckIsOnlineMulti(uint16_t nodeId, std::string &ip, uint16_t &port)
    {
        bool isOnline = false;
        ReadLocker<ReadWriteLock> lock(&mNLock);
        if (mNodeInfos.find(nodeId) != mNodeInfos.end()) {
            if (mNodeInfos[nodeId].status == CM_NODE_NORMAL) {
                isOnline = true;
                ip = mNodeInfos[nodeId].ip;
                port = mNodeInfos[nodeId].multiPort;
            }
            return isOnline;
        }
        return false;
    }

    inline std::map<uint16_t, CmNodeInfo> GetNodeView(void)
    {
        ReadLocker<ReadWriteLock> lock(&mNLock);
        return mNodeInfos;
    }

    inline std::map<uint16_t, CmPtInfo> GetPtView(void)
    {
        ReadLocker<ReadWriteLock> lock(&mPLock);
        return mPtInfos;
    }

    inline CmPtInfo GetLocalPtInfo()
    {
        ReadLocker<ReadWriteLock> lock(&mPLock);
        return mPtInfo;
    }

    inline uint16_t GetPtNum(void)
    {
        return mOptions.groups.maxPtNum;
    }

    inline BResult GetPtInfo(uint16_t ptId, CmPtInfo &ptInfo)
    {
        ReadLocker<ReadWriteLock> lock(&mPLock);
        auto iter = mPtInfos.find(ptId);
        if (iter == mPtInfos.end()) {
            return MMS_ERR;
        }
        ptInfo.Clone(iter->second);
        return MMS_OK;
    }

    inline BResult GetPtInfo(uint16_t &ptId, uint64_t &ptv, uint16_t remoteId[], uint16_t &remoteNum)
    {
        ReadLocker<ReadWriteLock> lock(&mPLock);
        if (mPtInfo.state != CM_PT_NORMAL) {
            LOG_WARN("Not ready, ptId:" << mPtInfo.ptId << ", state:" << mPtInfo.state);
            return MMS_INNER_RETRY;
        }
        ptId = mPtInfo.ptId;
        ptv = mPtInfo.version;
        remoteNum = 0;
        for (const auto& elem : mPtInfo.copys) {
            if (elem.state != CM_COPY_RUNNING) {
                continue;
            }
            if (elem.nodeId != mNodeId) {
                remoteId[remoteNum] = elem.nodeId;
                remoteNum++;
            }
        }
        return MMS_OK;
    }

    inline BResult GetPtInfo(uint16_t &ptId, uint64_t &ptv, uint16_t &remoteNum)
    {
        ReadLocker<ReadWriteLock> lock(&mPLock);
        if (mPtInfo.state != CM_PT_NORMAL) {
            LOG_WARN("Not ready, ptId:" << mPtInfo.ptId << ", state:" << mPtInfo.state);
            return MMS_INNER_RETRY;
        }

        ptId = mPtInfo.ptId;
        ptv = mPtInfo.version;
        remoteNum = 0;
        for (const auto& elem : mPtInfo.copys) {
            if (elem.state != CM_COPY_RUNNING) {
                continue;
            }
            if (elem.nodeId != mNodeId) {
                remoteNum++;
            }
        }

        return MMS_OK;
    }

    static inline uint16_t HashKeyToPt(const char *key, uint16_t keyLen, uint16_t ptNum)
    {
        if (key == nullptr || keyLen == 0 || keyLen > MAX_KEY_LENGTH || ptNum == 0) {
            return 0;
        }

        uint64_t hash = 14695981039346656037ULL;
        for (uint16_t index = 0; index < keyLen; ++index) {
            hash ^= static_cast<unsigned char>(key[index]);
            hash *= 1099511628211ULL;
        }
        return static_cast<uint16_t>(hash % ptNum);
    }

    inline BResult GetPtInfoByKey(const char *key, uint16_t keyLen, uint16_t &ptId, uint64_t &ptv,
                                  uint16_t remoteId[], uint16_t &remoteNum)
    {
        ReadLocker<ReadWriteLock> lock(&mPLock);
        if (key == nullptr || keyLen == 0 || keyLen > MAX_KEY_LENGTH || mOptions.groups.maxPtNum == 0) {
            return MMS_INVALID_PARAM;
        }

        ptId = HashKeyToPt(key, keyLen, mOptions.groups.maxPtNum);
        auto iter = mPtInfos.find(ptId);
        if (iter == mPtInfos.end() || iter->second.state != CM_PT_NORMAL) {
            LOG_WARN("Not ready, key:" << std::string(key, keyLen) << ", ptId:" << ptId << ".");
            return MMS_INNER_RETRY;
        }

        ptv = iter->second.version;
        remoteNum = 0;
        uint16_t runningCopyNum = 0;
        bool localCopy = false;
        for (const auto &elem : iter->second.copys) {
            if (elem.state != CM_COPY_RUNNING) {
                continue;
            }
            runningCopyNum++;
            if (elem.nodeId != mNodeId) {
                remoteId[remoteNum] = elem.nodeId;
                remoteNum++;
            } else {
                localCopy = true;
            }
        }
        if (runningCopyNum == 0 || (!localCopy && remoteNum == 0)) {
            LOG_WARN("No running copy, key:" << std::string(key, keyLen) << ", ptId:" << ptId << ".");
            return MMS_INNER_RETRY;
        }
        return MMS_OK;
    }

    inline BResult ResolveBatchPtIds(const CmKeyRoute items[], uint32_t itemNum, uint16_t ptIds[])
    {
        return ResolveBatchPtIdsFromItems(items, itemNum, ptIds);
    }

    template <typename Item>
    inline BResult ResolveBatchPtIdsFromItems(const Item items[], uint32_t itemNum, uint16_t ptIds[])
    {
        ReadLocker<ReadWriteLock> lock(&mPLock);
        uint16_t ptNum = mOptions.groups.maxPtNum;
        if (items == nullptr || ptIds == nullptr || itemNum == 0 || ptNum == 0) {
            return MMS_INVALID_PARAM;
        }

        static thread_local std::vector<uint8_t> checked;
        checked.assign(ptNum, 0);
        for (uint32_t index = 0; index < itemNum; ++index) {
            if (items[index].key == nullptr || items[index].keyLen == 0 ||
                items[index].keyLen > MAX_KEY_LENGTH) {
                return MMS_INVALID_PARAM;
            }

            uint16_t ptId = HashKeyToPt(items[index].key, items[index].keyLen, ptNum);
            ptIds[index] = ptId;
            if (checked[ptId] != 0) {
                continue;
            }

            auto iter = mPtInfos.find(ptId);
            if (iter == mPtInfos.end() || iter->second.state != CM_PT_NORMAL) {
                return MMS_INNER_RETRY;
            }
            bool runningCopy = false;
            for (const auto &copy : iter->second.copys) {
                if (copy.state == CM_COPY_RUNNING) {
                    runningCopy = true;
                    break;
                }
            }
            if (!runningCopy) {
                return MMS_INNER_RETRY;
            }
            checked[ptId] = 1;
        }
        return MMS_OK;
    }

    inline BResult ValidateBatchOwner(const CmKeyRoute items[], uint32_t itemNum, uint64_t expectedPtVersion,
                                      uint16_t targetNid)
    {
        return ValidateBatchOwnerItems(items, itemNum, expectedPtVersion, targetNid);
    }

    template <typename Item>
    inline BResult ValidateBatchOwnerItems(const Item items[], uint32_t itemNum, uint64_t expectedPtVersion,
                                           uint16_t targetNid)
    {
        ReadLocker<ReadWriteLock> lock(&mPLock);
        uint16_t ptNum = mOptions.groups.maxPtNum;
        if (items == nullptr || itemNum == 0 || ptNum == 0 || expectedPtVersion != mPtViewVersion) {
            return expectedPtVersion == mPtViewVersion ? MMS_INVALID_PARAM : MMS_NEED_UPDATE_PT_VERSION;
        }

        static thread_local std::vector<uint8_t> checked;
        checked.assign(ptNum, 0);
        for (uint32_t index = 0; index < itemNum; ++index) {
            if (items[index].key == nullptr || items[index].keyLen == 0 ||
                items[index].keyLen > MAX_KEY_LENGTH) {
                return MMS_INVALID_PARAM;
            }

            uint16_t ptId = HashKeyToPt(items[index].key, items[index].keyLen, ptNum);
            if (checked[ptId] != 0) {
                continue;
            }
            auto iter = mPtInfos.find(ptId);
            if (iter == mPtInfos.end() || iter->second.state != CM_PT_NORMAL) {
                return MMS_INNER_RETRY;
            }
            bool masterRunning = false;
            for (const auto &copy : iter->second.copys) {
                if (copy.nodeId == iter->second.masterNodeId && copy.state == CM_COPY_RUNNING) {
                    masterRunning = true;
                    break;
                }
            }
            if (!masterRunning) {
                return MMS_INNER_RETRY;
            }
            if (iter->second.masterNodeId != targetNid) {
                return MMS_NEED_UPDATE_PT_VERSION;
            }
            checked[ptId] = 1;
        }
        return MMS_OK;
    }

    inline BResult UpdatePtState(uint16_t ptId)
    {
        LOG_INFO("update pt state.");
        WriteLocker<ReadWriteLock> lock(&mPLock);
        if (mPtInfos.find(ptId) != mPtInfos.end()) {
            if (mPtInfos[ptId].state == CM_PT_MST_MIGRATE) {
                mPtInfos[ptId].state = CM_PT_NORMAL;
                mPtInfo.state = (mPtInfo.ptId == ptId) ? CM_PT_NORMAL : mPtInfo.state;
                return MMS_OK;
            } else {
                LOG_WARN("No needed, ptId:" << mPtInfo.ptId << ", state:" << mPtInfo.state);
                return MMS_OK;
            }
        }
        LOG_WARN("Not found, ptId:" << ptId);
        return MMS_ERR;
    }

    inline std::vector<CmPtCopy> GetLocalPtCopys()
    {
        // HandleCmPtEvent在锁里执行,此处不加锁
        std::vector<CmPtCopy> copys{};
        copys = mPtInfo.copys;
        return std::move(copys);
    }

    inline uint64_t GetLocalPtVersion() const
    {
        // HandleCmPtEvent在锁里执行,此处不加锁
        return mPtInfo.version;
    }

    inline uint64_t GetPtVersion()
    {
        ReadLocker<ReadWriteLock> lock(&mPLock);
        return mPtViewVersion;
    }

    inline uint32_t GetOnlineNodesNum() const
    {
        // HandleCmPtEvent在锁里执行,此处不加锁
        uint32_t count = 0;
        for (auto &item:mNodeInfos) {
            if (item.second.status == CmNodeStatus::CM_NODE_NORMAL) {
                count++;
            }
        }

        return count;
    }

    BResult ReportPtFinish(uint16_t ptId, uint64_t birthVersion);
    BResult RegisterNode(CmNodeInfo &node);

    BResult RegisterNodeHandler(const CmNodeHandler &nodeHandler);

    BResult RegisterPtHandler(const CmPtHandler &ptHandler);

    BResult RegisterPtMigrateHandler(const CmPtMigrateHandler &ptMigrateHandler);

    DEFINE_REF_COUNT_FUNCTIONS;

    static int32_t QueryLocalNodeInfo(NodeInfo *nodeInfo, void *ctx);
    static int32_t NotifyNodeListChange(NodeStateList *nodeList, void *ctx);
    static int32_t NotifyPtListChange(PtEntryList *ptList, void *ctx);
public:
    CmOptions mOptions;
    CmNodeInfo mNode;
    CmNodeHandler mNodeHandler;
    CmPtHandler mPtHandler;
    CmPtMigrateHandler mPtMigrateHandler;

    CmServiceStatus mStatus = CM_ABNORMAL;

    uint16_t mNodeId{ NODE_ID_INVALID };
    MmsNodeId mMasterNode{ NODE_ID_INVALID };

    std::map<uint16_t, CmNodeInfo> mNodeInfos;
    std::map<uint16_t, CmPtInfo> mPtInfos;

    CmPtInfo mPtInfo;
    uint64_t mPtViewVersion{0};

    ReadWriteLock mNLock;
    ReadWriteLock mPLock;

    bool mStarted{ false };
    bool mInited{ false };
    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // MMSCORE_MMS_CM_H
