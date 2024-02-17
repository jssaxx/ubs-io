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
    uint32_t nodeId;
    pid_t pid;
    ChannelPtr channel{};

    ChannelInfo() : nodeId(UINT32_MAX), pid(0) {}
    ChannelInfo(uint32_t nid, pid_t procId, ChannelPtr ch) : nodeId(nid), pid(procId), channel(std::move(ch)) {}
};

class NetChannelMgr {
public:
    explicit NetChannelMgr(std::string name) : mName(std::move(name)) {}
    ~NetChannelMgr()
    {
        UnInitialize();
    }

    BResult Initialize();
    void UnInitialize();

    BResult AddChannel(uint32_t dstNid, ChannelPtr &ch, bool forceUpdate = false);
    BResult RemoveChannel(uint32_t peerVNodeId, ChannelPtr &ch);

    inline BResult GetChannel(uint32_t dstNid, ChannelPtr &ch)
    {
        ChkTrue(dstNid < CHANNEL_BUCKET, BIO_INVALID_PARAM,
            "Failed to get channel in link manager '" << mName << "' as nodeId " << dstNid << " is larger then " <<
            CHANNEL_BUCKET);
        if (LIKELY(mChannelBits[dstNid] == BIO_N_BIT_ESTABLISHED)) {
            ch = mChannels[dstNid];
            return BIO_OK;
        }
        return BIO_NOT_EXISTS;
    }

    inline uint16_t ChannelCount() const
    {
        return mChannelCount;
    }

    void AddAcceptChannel(const ChannelPtr &ch, int32_t nodeId, pid_t pid);
    void RemoveAcceptChannel(uint64_t channelId, ChannelInfo &info);
    std::pair<int32_t, ChannelInfo> GetAcceptChannel(uint64_t channelId);
    std::list<ChannelInfo> ListAll();

    DEFINE_REF_COUNT_FUNCTIONS

private:
    constexpr static uint16_t CHANNEL_BUCKET = 512L;

private:
    ChannelPtr mChannels[CHANNEL_BUCKET]{};
    uint16_t mChannelBits[CHANNEL_BUCKET]{};

    std::unordered_map<uint64_t, ChannelInfo *> mAcceptChannels;
    std::mutex lock;

    DEFINE_REF_COUNT_VARIABLE
    bool mInited = false;
    std::atomic<uint16_t> mChannelCount{ 0 };
    std::string mName;
};

using NetChannelMgrPtr = Ref<NetChannelMgr>;
}
}
#endif // NET_CHANNEL_MGR_H
