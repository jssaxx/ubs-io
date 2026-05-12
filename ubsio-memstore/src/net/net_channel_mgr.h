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
#ifndef NET_CHANNEL_MGR_H
#define NET_CHANNEL_MGR_H

#include <unordered_map>
#include <utility>
#include <list>
#include "net_common.h"
#include "mms_lock.h"

namespace ock {
namespace mms {
struct ChannelInfo {
    NetNode id;
    uint32_t num = 0;
    ChannelPtr channel[MAX_GROUPS_NUM];

    ChannelInfo() = default;
    ChannelInfo(NetNode dst, uint32_t index, ChannelPtr ch) : id(dst), num(NO_1)
    {
        for (uint16_t index = 0; index < MAX_GROUPS_NUM; index++) {
            channel[index] = nullptr;
        }
        channel[index] = std::move(ch);
    }
};

struct ChannelNode {
    NetNode id;
    ChannelPtr channel;

    ChannelNode() : id(0, 0), channel(nullptr) {}
    ChannelNode(NetNode dst, ChannelPtr ch) : id(dst), channel(std::move(ch)) {}
};

class NetChannelMgr {
public:
    NetChannelMgr() = default;
    ~NetChannelMgr() = default;

    BResult Initialize();
    void UnInitialize();

    BResult AddChannel(const NetNode &dstNid, ChannelPtr &ch, uint32_t groupIndex);
    BResult RemoveChannel(const NetNode &dstNid, const ChannelPtr &ch, uint32_t groupIndex);

    inline BResult GetChannel(uint32_t dstNid, uint32_t pid, ChannelPtr &ch, uint32_t groupIndex)
    {
        ReadLocker<ReadWriteLock> locker(&lock);
        NetNode rDstNid(dstNid, pid);
        auto iter = mChannelMgr.find(rDstNid.whole);
        if (UNLIKELY(iter == mChannelMgr.end())) {
            return MMS_NOT_EXISTS;
        }
        if (UNLIKELY(groupIndex >= MAX_GROUPS_NUM)) {
            return MMS_NOT_EXISTS;
        }
        if (UNLIKELY(iter->second->channel[groupIndex] == nullptr)) {
            return MMS_NOT_EXISTS;
        }
        ch = iter->second->channel[groupIndex];
        return MMS_OK;
    }

    inline BResult GetChannel(uint32_t dstNid, ChannelPtr &ch, uint32_t groupIndex)
    {
        ReadLocker<ReadWriteLock> locker(&lock);
        NetNode rDstNid(dstNid, 0);
        auto iter = mChannelMgr.find(rDstNid.whole);
        if (UNLIKELY(iter == mChannelMgr.end())) {
            return MMS_NOT_EXISTS;
        }
        if (UNLIKELY(groupIndex >= MAX_GROUPS_NUM)) {
            return MMS_NOT_EXISTS;
        }
        if (UNLIKELY(iter->second->channel[groupIndex] == nullptr)) {
            return MMS_NOT_EXISTS;
        }
        ch = iter->second->channel[groupIndex];
        return MMS_OK;
    }

    inline BResult GetChannel(const NetNode &dstNid, ChannelPtr &ch, uint32_t groupIndex)
    {
        ReadLocker<ReadWriteLock> locker(&lock);
        auto iter = mChannelMgr.find(dstNid.whole);
        if (UNLIKELY(iter == mChannelMgr.end())) {
            return MMS_NOT_EXISTS;
        }
        if (UNLIKELY(groupIndex >= MAX_GROUPS_NUM)) {
            return MMS_NOT_EXISTS;
        }
        if (UNLIKELY(iter->second->channel[groupIndex] == nullptr)) {
            return MMS_NOT_EXISTS;
        }
        ch = iter->second->channel[groupIndex];
        return MMS_OK;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    bool mInited = false;
    std::unordered_map<uint64_t, ChannelInfo *> mChannelMgr;
    std::unordered_map<uint64_t, ChannelNode *> mChannelNodeMap;
    ReadWriteLock lock;

    DEFINE_REF_COUNT_VARIABLE;
};

using NetChannelMgrPtr = Ref<NetChannelMgr>;
}
}
#endif // NET_CHANNEL_MGR_H

