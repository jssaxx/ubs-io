/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include "rpc_engine_channel_mgt.h"

namespace ock {
namespace bio {
BResult RpcChannelMgr::Initialize()
{
    if (mInited) {
        LOG_WARN("Net engine channel manager '" << mName << "' has been initialized already");
        return BIO_OK;
    }

    for (auto &ch : mChannels) {
        ch = nullptr;
    }

    bzero(&mChannelBits, sizeof(uint8_t) * CHANNEL_BUCKET);

    mChannelCount = 0;

    mInited = true;
    LOG_INFO("Net engine channel manager '" << mName << "' initialized, CHANNEL_BUCKET " << CHANNEL_BUCKET <<
        ", channelCount " << mChannelCount);
    return BIO_OK;
}

void RpcChannelMgr::UnInitialize()
{
    if (!mInited) {
        return;
    }

    /*
     * loop all channels and assign it to nullptr to release them,
     * instead of bzero
     */
    for (auto &ch : mChannels) {
        ch = nullptr;
    }

    bzero(&mChannelBits, sizeof(uint16_t) * CHANNEL_BUCKET);

    mChannelCount = 0;
    mInited = false;
}

BResult RpcChannelMgr::AddChannel(uint32_t peerVNodeId, ChannelPtr &ch, bool forceUpdate)
{
    /* check peerVNodeId */
    if (peerVNodeId >= CHANNEL_BUCKET) {
        LOG_ERROR("Failed to add channel into channel manager '" << mName << "' as peerVNodeId " << peerVNodeId <<
            " is larger then " << CHANNEL_BUCKET);
        return BIO_INVALID_PARAM;
    }

    /* check if already added */
    if (!forceUpdate &&
        !__sync_bool_compare_and_swap(&mChannelBits[peerVNodeId], BIO_N_BIT_EMPTY, BIO_N_BIT_ESTABLISHED)) {
        LOG_ERROR("Failed to add channel into channel manager '" << mName << "' as channel with peerVNodeId " <<
            peerVNodeId << " already exists");
        if (mChannels[peerVNodeId].Get() == nullptr) {
            LOG_ERROR("Check Failed: mChannels[peerVNodeId].Get() != nullptr");
        }
        return BIO_ALREADY_EXISTS;
    }

    /* set channel */
    if (forceUpdate && mChannels[peerVNodeId].Get() != nullptr) {
        mChannels[peerVNodeId] = ch;
        LOG_INFO("Updated channel with peerVNodeId " << peerVNodeId << " into channel manager '" << mName << "'");
        return BIO_OK;
    }

    mChannels[peerVNodeId] = ch;
    ++mChannelCount;
    LOG_INFO("Added channel with peerVNodeId " << peerVNodeId << " into channel manager '" << mName << "'"
                                               << ", channel id " << ch->Id());

    return BIO_OK;
}

BResult RpcChannelMgr::RemoveChannel(uint32_t peerVNodeId, ChannelPtr &ch)
{
    /* check peerVNodeId */
    if (peerVNodeId >= CHANNEL_BUCKET) {
        LOG_ERROR("Failed to remove channel into channel manager '" << mName << "' as peerVNodeId " << peerVNodeId <<
            " is larger then " << CHANNEL_BUCKET);
        return BIO_INVALID_PARAM;
    }

    if (__sync_bool_compare_and_swap(&mChannelBits[peerVNodeId], BIO_N_BIT_ESTABLISHED, BIO_N_BIT_EMPTY)) {
        ChkNot(mChannels[peerVNodeId].Get() != nullptr);
        ch = mChannels[peerVNodeId];
        mChannels[peerVNodeId] = nullptr;
        LOG_INFO("Remove channel with peerVNodeId " << peerVNodeId << " into channel manager '" << mName << "'"
                                                    << ", channel id " << ch->Id());
        return BIO_OK;
    }

    LOG_INFO("Remove channel with peerVNodeId " << peerVNodeId << " not exist");
    return BIO_NOT_EXISTS;
}
}
}