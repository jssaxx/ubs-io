/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef NET_CHANNEL_MGR_H
#define NET_CHANNEL_MGR_H

#include <unordered_map>
#include <utility>
#include <list>
#include "bio_tracepoint_helper.h"
#include "net_common.h"

namespace ock {
namespace bio {
struct ChannelInfo {
    NetNode id;
    ChannelPtr channel;

    ChannelInfo() = default;
    ChannelInfo(NetNode dst, ChannelPtr ch) : id(dst), channel(std::move(ch)) {}
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

    BResult AddChannel(NetNode dstNid, ChannelPtr &ch, uint8_t plane);
    BResult RemoveChannel(const NetNode &dstNid, const ChannelPtr &ch);

    inline BResult GetChannel(uint32_t dstNid, uint32_t pid, ChannelPtr &ch)
    {
        std::unique_lock<std::mutex> locker(lock);
        NetNode rDstNid(dstNid, pid);
        auto iter = mChannelMgr.find(rDstNid.whole);
        if (UNLIKELY(iter == mChannelMgr.end())) {
            return BIO_NOT_EXISTS;
        }
        ch = iter->second->channel;
        return BIO_OK;
    }

    inline BResult GetChannel(uint32_t dstNid, ChannelPtr &ch)
    {
        std::unique_lock<std::mutex> locker(lock);
        NetNode rDstNid(dstNid, 0);
        auto iter = mChannelMgr.find(rDstNid.whole);
        if (UNLIKELY(iter == mChannelMgr.end())) {
            return BIO_NOT_EXISTS;
        }
        ch = iter->second->channel;
        return BIO_OK;
    }

    inline BResult GetChannel(const NetNode &dstNid, ChannelPtr &ch)
    {
        std::unique_lock<std::mutex> locker(lock);
        auto iter = mChannelMgr.find(dstNid.whole);
        if (UNLIKELY(iter == mChannelMgr.end())) {
            return BIO_NOT_EXISTS;
        }
        ch = iter->second->channel;
        return BIO_OK;
    }

    DEFINE_REF_COUNT_FUNCTIONS

private:
    bool mInited = false;
    std::unordered_map<uint64_t, ChannelInfo *> mChannelMgr;
    std::unordered_map<uint64_t, ChannelNode *> mChannelNodeMap;
    std::mutex lock;

    DEFINE_REF_COUNT_VARIABLE
};

using NetChannelMgrPtr = Ref<NetChannelMgr>;
}
}
#endif // NET_CHANNEL_MGR_H
