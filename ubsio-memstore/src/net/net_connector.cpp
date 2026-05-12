/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "mms_execution.h"
#include "mms_ref.h"
#include "net_engine.h"
#include "net_connector.h"

namespace ock {
namespace mms {
ConnectTask::ConnectTask(NetEngine *engine) : mEngine(engine)
{
    if (mEngine != nullptr) {
        mEngine->IncreaseRef();
    }
}

ConnectTask::~ConnectTask()
{
    if (mEngine != nullptr) {
        mEngine->DecreaseRef();
        mEngine = nullptr;
    }
}

BResult ConnectTask::DoConnect()
{
    BResult ret = MMS_OK;
    uint16_t groupNum = mEngine->GetGroupNum(mode);
    uint32_t groupIndex;
    for (groupIndex = 0; groupIndex < groupNum; groupIndex++) {
        ChannelPtr channel = nullptr;
        if ((ret = mEngine->GetChannelMgr()->GetChannel(mConnectInfo.peerId, channel, groupIndex)) == MMS_NOT_EXISTS) {
            ret = mEngine->ConnectToPeer(mode, mConnectInfo, groupIndex, channel);
            if (ret != MMS_OK) {
                NET_LOG_ERROR("Failed to connect to peer, dstNid:" << mConnectInfo.peerId.nid <<
                    ", pid:" << mConnectInfo.peerId.pid << ", groupIndex:" << groupIndex << ".");
                return MMS_ERR;
            }
            mEngine->GetChannelMgr()->AddChannel(mConnectInfo.peerId, channel, groupIndex);
        } else {
            NET_LOG_INFO("Exist connect by target node id " << mConnectInfo.peerId.nid << ", pid:" <<
                mConnectInfo.peerId.pid << ", groupIndex:" << groupIndex << ".");
        }
    }

    return MMS_OK;
}

void ConnectTask::Run()
{
    if (mSyncConnect) {
        return SyncConnect();
    } else {
        return AsyncConnect();
    }
}

NetConnector::NetConnector(NetEngine *engine) : mEngine(engine)
{
    if (mEngine != nullptr) {
        mEngine->IncreaseRef();
    }
}

NetConnector::~NetConnector()
{
    if (mEngine != nullptr) {
        mEngine->DecreaseRef();
        mEngine = nullptr;
    }
}

BResult NetConnector::Start()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        NET_LOG_WARN("Net connector has been already started");
        return MMS_OK;
    }

    mExeService = ExecutorService::Create(NO_1, NO_2048);
    if (UNLIKELY(mExeService == nullptr)) {
        NET_LOG_ERROR("Failed to start execution service for Net connector " << mName << ", probably out of memory.");
        return MMS_ALLOC_FAIL;
    }

    mExeService->SetThreadName("NetConnect-" + mName);
    if (!(mExeService->Start())) {
        NET_LOG_ERROR("Failed to start execution service for Net connector " << mName);
        mExeService->Stop();
        return MMS_ERR;
    }

    mStarted = true;
    return MMS_OK;
}

void NetConnector::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }
    mExeService->Stop();
    mStarted = false;
}

BResult NetConnector::AsyncConnect(ConnectInfo &info, AsyncConnHandler &handler, uintptr_t ctx)
{
    ChkTrueNot(!info.ip.empty(), MMS_INVALID_PARAM);
    ChkTrueNot(info.port != 0, MMS_INVALID_PARAM);
    ChkTrueNot(info.retryTimes != 0, MMS_INVALID_PARAM);
    ChkTrueNot(handler != nullptr, MMS_INVALID_PARAM);

    auto task = MakeRef<ConnectTask>(mEngine);
    if (UNLIKELY(task == nullptr)) {
        NET_LOG_ERROR("Failed to new connection task in Net connector " << mName << ", probably out of memory.");
        return MMS_ALLOC_FAIL;
    }

    task->mode = (info.srcId.nid == INVALID_NID) ? CONNECT_IPC : CONNECT_RPC;
    task->mEngine = mEngine;
    task->mConnectInfo = info;
    task->mSyncConnect = false;
    task->mAsyncHandler = handler;
    task->mUserCtx = ctx;
    auto result = mExeService->Execute(task.Get());
    if (UNLIKELY(!result)) {
        NET_LOG_ERROR("Failed to enqueue connecting task into Net connector " << mName << ".");
        return MMS_ERR;
    }

    return MMS_OK;
}

BResult NetConnector::SyncConnect(ConnectInfo &info)
{
    auto task = MakeRef<ConnectTask>(mEngine);
    if (UNLIKELY(task == nullptr)) {
        NET_LOG_ERROR("Failed to new connection task in Net connector " << mName << ", probably out of memory.");
        return MMS_ALLOC_FAIL;
    }

    task->mode = (info.srcId.nid == INVALID_NID) ? CONNECT_IPC : CONNECT_RPC;
    task->mEngine = mEngine;
    task->mConnectInfo = info;
    task->mSyncConnect = true;
    auto result = mExeService->Execute(task.Get());
    if (UNLIKELY(!result)) {
        NET_LOG_ERROR("Failed to enqueue connecting task into Net connector " << mName << ".");
        return MMS_ERR;
    }

    return task->Wait();
}
}
}
