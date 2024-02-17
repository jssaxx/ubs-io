/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mutex>
#include "net_channel_mgr.h"

namespace ock {
namespace bio {
BResult NetChannelMgr::Initialize()
{
    if (UNLIKELY(mInited)) {
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

void NetChannelMgr::UnInitialize()
{
    if (!mInited) {
        return;
    }

    for (auto &ch : mChannels) {
        ch = nullptr;
    }
    bzero(&mChannelBits, sizeof(uint16_t) * CHANNEL_BUCKET);
    mChannelCount = 0;
    mInited = false;
}

BResult NetChannelMgr::AddChannel(uint32_t dstNid, ChannelPtr &ch, bool forceUpdate)
{
    ChkTrue(dstNid < CHANNEL_BUCKET, BIO_INVALID_PARAM,
        "Failed to get channel in link manager '" << mName << "' as nodeId " << dstNid << " is larger then " <<
        CHANNEL_BUCKET);

    if (!forceUpdate && !__sync_bool_compare_and_swap(&mChannelBits[dstNid], BIO_N_BIT_EMPTY, BIO_N_BIT_ESTABLISHED)) {
        LOG_ERROR("Failed to add channel into channel manager '" << mName << "' as channel with nodeId " << dstNid <<
            " already exists");
        return BIO_ALREADY_EXISTS;
    }

    if (forceUpdate && mChannels[dstNid] != nullptr) {
        mChannels[dstNid] = ch;
        LOG_INFO("Updated channel with nodeId " << dstNid << " into channel manager '" << mName << "'");
        return BIO_OK;
    }

    mChannels[dstNid] = ch;
    ++mChannelCount;
    LOG_INFO("Added channel with nodeId " << dstNid << " into channel manager '" << mName << "'"
                                          << ", channel id " << ch->Id());
    return BIO_OK;
}

BResult NetChannelMgr::RemoveChannel(uint32_t dstNid, ChannelPtr &ch)
{
    ChkTrue(dstNid < CHANNEL_BUCKET, BIO_INVALID_PARAM,
        "Failed to get channel in link manager '" << mName << "' as nodeId " << dstNid << " is larger then " <<
        CHANNEL_BUCKET);

    if (__sync_bool_compare_and_swap(&mChannelBits[dstNid], BIO_N_BIT_ESTABLISHED, BIO_N_BIT_EMPTY)) {
        ch = mChannels[dstNid];
        mChannels[dstNid] = nullptr;
        LOG_INFO("Remove channel with nodeId " << dstNid << " into channel manager '" << mName << "'"
                                               << ", channel id " << ch->Id());
        return BIO_OK;
    }

    LOG_INFO("Remove channel with nodeId " << dstNid << " not exist.");
    return BIO_NOT_EXISTS;
}

void NetChannelMgr::AddAcceptChannel(const ChannelPtr &ch, int32_t nodeId, pid_t pid)
{
    auto channelId = ch->Id();
    std::unique_lock<std::mutex> locker(lock);
    auto *v = new ChannelInfo(nodeId, pid, ch);
    ChkTrueEx(v != nullptr, "New channel mapping value failed.");
    mAcceptChannels.emplace(std::make_pair(channelId, v));
}

void NetChannelMgr::RemoveAcceptChannel(uint64_t channelId, ChannelInfo &info)
{
    std::unique_lock<std::mutex> locker(lock);
    auto pos = mAcceptChannels.find(channelId);
    if (pos != mAcceptChannels.end()) {
        auto value = pos->second;
        info = *value;
        delete value;
        mAcceptChannels.erase(pos);
    }
}

std::pair<int32_t, ChannelInfo> NetChannelMgr::GetAcceptChannel(uint64_t channelId)
{
    std::unique_lock<std::mutex> locker(lock);
    auto pos = mAcceptChannels.find(channelId);
    if (pos != mAcceptChannels.end()) {
        return std::make_pair(pos->second->nodeId,
            ChannelInfo(pos->second->nodeId, pos->second->pid, pos->second->channel));
    }
    return std::make_pair(-1, ChannelInfo());
}

std::list<ChannelInfo> NetChannelMgr::ListAll()
{
    std::list<ChannelInfo> result;
    std::unique_lock<std::mutex> locker(lock);
    for (auto &it : mAcceptChannels) {
        result.push_back(*(it.second));
    }
    return std::move(result);
}
}
}