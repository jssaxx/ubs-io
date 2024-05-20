/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef NET_CHANNEL_MGR_H
#define NET_CHANNEL_MGR_H

#include <unordered_map>
#include <utility>
#include <list>

#include "net_common.h"
#ifdef USE_DEBUG_TOOLS
#include "bio_tracepoint_helper.h"
#endif

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

    BResult Initialize(uint8_t role);
    void UnInitialize();

    BResult AddChannel(NetNode dstNid, ChannelPtr &ch);
    BResult RemoveChannel(const NetNode &dstNid, const ChannelPtr &ch);

    inline BResult GetChannel(uint32_t dstNid, uint32_t pid, ChannelPtr &ch)
    {
        std::unique_lock<std::mutex> locker(lock);
        NetNode rDstNid(dstNid, pid);
        auto iter = mChannelMgr.end();
        LVOS_TP_START(SERVER_NET_GET_DATA_CHANNEL_NOT_EXIST, &iter, mChannelMgr.end());
        iter = mChannelMgr.find(rDstNid.whole);
        LVOS_TP_END;
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
        auto iter = mChannelMgr.end();
        LVOS_TP_START(SERVER_NET_GET_CHANNEL_NOT_EXIST, &iter, mChannelMgr.end());
        iter = mChannelMgr.find(rDstNid.whole);
        LVOS_TP_END;
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
    uint8_t mRole = 0;

    DEFINE_REF_COUNT_VARIABLE
};

using NetChannelMgrPtr = Ref<NetChannelMgr>;
}
}
#endif // NET_CHANNEL_MGR_H
