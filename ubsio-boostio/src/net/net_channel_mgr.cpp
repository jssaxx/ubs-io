/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "net_channel_mgr.h"
#include <mutex>

namespace ock {
namespace bio {
BResult NetChannelMgr::Initialize()
{
    if (UNLIKELY(mInited)) {
        NET_LOG_WARN("Net engine channel manager has been initialized already");
        return BIO_OK;
    }

    mChannelMgr.clear();
    mChannelNodeMap.clear();
    mInited = true;
    NET_LOG_INFO("Net engine channel manager initialize success.");
    return BIO_OK;
}

void NetChannelMgr::UnInitialize()
{
    if (!mInited) {
        return;
    }
    for (auto &node : mChannelNodeMap) {
        auto chNode = node.second;
        delete chNode;
    }
    mChannelNodeMap.clear();
    for (auto &node : mChannelMgr) {
        auto chInfo = node.second;
        delete chInfo;
    }
    mChannelMgr.clear();
    mInited = false;
}

BResult NetChannelMgr::AddChannel(const NetNode &dstNid, ChannelPtr &ch, uint8_t plane)
{
    if (UNLIKELY(ch == nullptr)) {
        NET_LOG_ERROR("The channel is nullptr.");
        return BIO_INNER_ERR;
    }
    std::unique_lock<std::mutex> locker(lock);
    auto chNode = new (std::nothrow) ChannelNode(dstNid, ch);
    if (UNLIKELY(chNode == nullptr)) {
        NET_LOG_ERROR("Alloc memory failed.");
        return BIO_ALLOC_FAIL;
    }
    mChannelNodeMap.emplace(std::make_pair(ch->GetId(), chNode));
    auto chInfo = new (std::nothrow) ChannelInfo(dstNid, ch);
    if (UNLIKELY(chInfo == nullptr)) {
        NET_LOG_ERROR("Alloc memory failed.");
        mChannelNodeMap.erase(ch->GetId());
        delete chNode;
        return BIO_ALLOC_FAIL;
    }
    mChannelMgr.insert(std::make_pair(dstNid.whole, chInfo));
    NET_LOG_INFO("Add channel success, dstNid:" << dstNid.nid << ", pid:" << dstNid.pid << ", channel:" << ch->GetId()
                                                << ", plane:" << static_cast<uint32_t>(plane) << ".");
    return BIO_OK;
}

BResult NetChannelMgr::RemoveChannel(const NetNode &dstNid, const ChannelPtr &ch)
{
    std::unique_lock<std::mutex> locker(lock);
    auto pos = mChannelNodeMap.find(ch->GetId());
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
    NET_LOG_INFO("Remove channel, dstNid:" << dstNid.nid << ", pid:" << dstNid.pid << ", channel:" << ch->GetId()
                                           << ".");
    return BIO_OK;
}
} // namespace bio
} // namespace ock