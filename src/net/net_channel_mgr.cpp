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
        NET_LOG_WARN("Net engine channel manager has been initialized already");
        return BIO_OK;
    }

    mInited = true;
    NET_LOG_INFO("Net engine channel manager initialize success.");
    return BIO_OK;
}

void NetChannelMgr::UnInitialize()
{
    if (!mInited) {
        return;
    }
    mInited = false;
}

BResult NetChannelMgr::AddChannel(uint32_t dstNid, ChannelPtr &ch, bool forceUpdate)
{
    if (UNLIKELY(ch == nullptr)) {
        NET_LOG_ERROR("The channel is nullptr.");
        return BIO_INNER_ERR;
    }
    std::unique_lock<std::mutex> locker(lock);
    auto chNode = new ChannelNode(dstNid, ch);
    mChannelNodeMap.emplace(std::make_pair(ch->Id(), chNode));
    auto chInfo = new ChannelInfo(dstNid, ch);
    mChannelMgr.insert(std::make_pair(dstNid, chInfo));
    NET_LOG_INFO("Added channel with nodeId " << dstNid << " into channel manager channel id " << ch->Id());
    return BIO_OK;
}

BResult NetChannelMgr::RemoveChannel(uint32_t dstNid, const ChannelPtr &ch)
{
    std::unique_lock<std::mutex> locker(lock);
    auto pos = mChannelNodeMap.find(ch->Id());
    if (pos != mChannelNodeMap.end()) {
        auto chNode = pos->second;
        mChannelNodeMap.erase(pos);
        delete chNode;
    }
    auto iter = mChannelMgr.find(dstNid);
    if (iter != mChannelMgr.end()) {
        auto chInfo = iter->second;
        mChannelMgr.erase(iter);
        delete chInfo;
    }
    NET_LOG_INFO("Remove channel with nodeId " << dstNid << ", channel " << ch->Id() << " success.");
    return BIO_OK;
}

BResult NetChannelMgr::AddChannelNode(uint32_t dstNid, ChannelPtr &ch)
{
    std::unique_lock<std::mutex> locker(lock);
    auto chNode = new ChannelNode(dstNid, ch);
    mChannelNodeMap.emplace(std::make_pair(ch->Id(), chNode));
    return BIO_OK;
}

void NetChannelMgr::RemoveChannelNode(uint64_t channelId, ChannelNode &chNode)
{
    std::unique_lock<std::mutex> locker(lock);
    auto pos = mChannelNodeMap.find(channelId);
    if (pos != mChannelNodeMap.end()) {
        auto chNodeP = pos->second;
        chNode = *chNodeP;
        mChannelNodeMap.erase(pos);
        delete chNodeP;
    }
}

ChannelNode NetChannelMgr::GetChannelNode(uint64_t channelId)
{
    std::unique_lock<std::mutex> locker(lock);
    auto pos = mChannelNodeMap.find(channelId);
    if (pos != mChannelNodeMap.end()) {
        return ChannelNode(pos->second->id, pos->second->channel);
    }
    return ChannelNode();
}
}
}