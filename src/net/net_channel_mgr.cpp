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

BResult NetChannelMgr::AddChannel(NetNode dstNid, ChannelPtr &ch)
{
    if (UNLIKELY(ch == nullptr)) {
        NET_LOG_ERROR("The channel is nullptr.");
        return BIO_INNER_ERR;
    }
    std::unique_lock<std::mutex> locker(lock);
    auto chNode = new ChannelNode(dstNid, ch);
    if (UNLIKELY(chNode == nullptr)) {
        NET_LOG_ERROR("Alloc memory failed.");
        return BIO_ALLOC_FAIL;
    }
    mChannelNodeMap.emplace(std::make_pair(ch->Id(), chNode));
    auto chInfo = new ChannelInfo(dstNid, ch);
    if (UNLIKELY(chInfo == nullptr)) {
        NET_LOG_ERROR("Alloc memory failed.");
        return BIO_ALLOC_FAIL;
    }
    mChannelMgr.insert(std::make_pair(dstNid.whole, chInfo));
    NET_LOG_INFO("Added channel, dstNid:" << dstNid.nid << ", pid:" << dstNid.pid << ", channel:" << ch->Id() << ".");
    return BIO_OK;
}

BResult NetChannelMgr::RemoveChannel(const NetNode &dstNid, const ChannelPtr &ch)
{
    std::unique_lock<std::mutex> locker(lock);
    auto pos = mChannelNodeMap.find(ch->Id());
    if (pos == mChannelNodeMap.end()) {
        return BIO_NOT_EXISTS;
    }
    auto chNode = pos->second;
    mChannelNodeMap.erase(pos);
    delete chNode;
    auto iter = mChannelMgr.find(dstNid.whole);
    if (iter == mChannelMgr.end()) {
        NET_LOG_WARN("Impossible, not found, with nodeId " << dstNid.nid);
        return BIO_ERR;
    }
    auto chInfo = iter->second;
    mChannelMgr.erase(iter);
    delete chInfo;
    chInfo = nullptr;
    NET_LOG_INFO("Remove channel, dstNid:" << dstNid.nid << ", pid:" << dstNid.pid << ", channel:" << ch->Id() << ".");
    return BIO_OK;
}
}
}