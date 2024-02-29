/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef NET_CHANNEL_MGR_H
#define NET_CHANNEL_MGR_H

#include <unordered_map>
#include <utility>
#include <list>

#include "net_common.h"

namespace ock {
namespace bio {
enum NetChBit {
    BIO_N_BIT_EMPTY = 0,
    BIO_N_BIT_ESTABLISHED = 1,
};

struct ChannelInfo {
    uint32_t id;
    ChannelPtr channel;

    ChannelInfo() = default;
    ChannelInfo(uint32_t dst, ChannelPtr ch) : id(dst), channel(std::move(ch)) {}
};

struct ChannelNode {
    uint32_t id;
    ChannelPtr channel;

    ChannelNode() : id(UINT32_MAX), channel(nullptr) {}
    ChannelNode(uint32_t id, ChannelPtr ch) : id(id), channel(std::move(ch)) {}
};

class NetChannelMgr {
public:
    NetChannelMgr() = default;
    ~NetChannelMgr()
    {
        UnInitialize();
    }

    BResult Initialize();
    void UnInitialize();

    BResult AddChannel(uint32_t dstNid, ChannelPtr &ch, bool forceUpdate = false);
    BResult RemoveChannel(uint32_t peerVNodeId, const ChannelPtr &ch);

    inline BResult GetChannel(uint32_t dstNid, ChannelPtr &ch)
    {
        std::unique_lock<std::mutex> locker(lock);
        auto iter = mChannelMgr.find(dstNid);
        if (UNLIKELY(iter == mChannelMgr.end())) {
            return BIO_NOT_EXISTS;
        }
        ch = iter->second->channel;
        return BIO_OK;
    }

    BResult AddChannelNode(uint32_t dstNid, ChannelPtr &ch);
    void RemoveChannelNode(uint64_t channelId, ChannelNode &chNode);
    ChannelNode GetChannelNode(uint64_t channelId);

    DEFINE_REF_COUNT_FUNCTIONS

private:
    bool mInited = false;
    std::unordered_map<uint16_t, ChannelInfo *> mChannelMgr;
    std::unordered_map<uint64_t, ChannelNode *> mChannelNodeMap;
    std::mutex lock;

    DEFINE_REF_COUNT_VARIABLE
};

using NetChannelMgrPtr = Ref<NetChannelMgr>;
}
}
#endif // NET_CHANNEL_MGR_H
