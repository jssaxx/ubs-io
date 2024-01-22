/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef RPC_ENGINE_CHANNEL_MGT_H
#define RPC_ENGINE_CHANNEL_MGT_H

#include <unordered_map>
#include <utility>

#include "net_common.h"

namespace ock {
namespace bio {
enum RpcChBit {
    BIO_N_BIT_EMPTY = 0,
    BIO_N_BIT_ESTABLISHED = 1,
};

class RpcChannelMgr {
public:
    explicit RpcChannelMgr(std::string name) : mName(std::move(name)) {}
    ~RpcChannelMgr()
    {
        UnInitialize();
    }

    BResult Initialize();
    void UnInitialize();

    BResult AddChannel(uint32_t peerVNodeId, ChannelPtr &ch, bool forceUpdate = false);
    BResult RemoveChannel(uint32_t peerVNodeId, ChannelPtr &ch);

    BResult GetChannel(uint32_t peerVNodeId, ChannelPtr &ch)
    {
        /* check peerVNodeId */
        if (UNLIKELY(peerVNodeId >= CHANNEL_BUCKET)) {
            LOG_ERROR("Failed to get channel in link manager '" << mName << "' as peerVNodeId " << peerVNodeId <<
                " is larger then " << CHANNEL_BUCKET);
            return BIO_INVALID_PARAM;
        }

        if (mChannelBits[peerVNodeId] == BIO_N_BIT_ESTABLISHED) {
            ch = mChannels[peerVNodeId];
            return BIO_OK;
        }

        return BIO_NOT_EXISTS;
    }

    inline uint16_t ChannelCount() const
    {
        return mChannelCount;
    }

    DEFINE_REF_COUNT_FUNCTIONS

private:
    constexpr static uint16_t CHANNEL_BUCKET = 512L;

private:
    /*
     * use flatten vector instead of map, for faster query,
     * this needs no duplicated peerVNodeId
     */
    ChannelPtr mChannels[CHANNEL_BUCKET]{};
    uint16_t mChannelBits[CHANNEL_BUCKET]{};

    DEFINE_REF_COUNT_VARIABLE

    bool mInited = false;
    std::atomic<uint16_t> mChannelCount{ 0 };
    std::string mName;
};
using RpcChannelMgrPtr = Ref<RpcChannelMgr>;
}
}

#endif // RPC_ENGINE_CHANNEL_MGT_H
