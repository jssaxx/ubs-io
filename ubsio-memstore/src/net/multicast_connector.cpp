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
#include "multicast_connector.h"
#include "net_multicast_engine.h"

namespace ock {
namespace mms {
MultiConnectTask::MultiConnectTask(NetMulticastEngine *engine) : mMultiEngine(engine)
{
    if (mMultiEngine != nullptr) {
        mMultiEngine->IncreaseRef();
    }
}

MultiConnectTask::~MultiConnectTask()
{
    if (mMultiEngine != nullptr) {
        mMultiEngine->DecreaseRef();
        mMultiEngine = nullptr;
    }
}

BResult MultiConnectTask::DoConnect()
{
    if (mMultiEngine->IsSubscriberExist(mConnectInfo.ip)) {
        NET_LOG_INFO("Subscriber exist, ip:" << mConnectInfo.ip << ", port:" << mConnectInfo.port);
        return MMS_OK;
    }

    BResult ret = MMS_ERR;
    for (uint16_t i = 0; i < mConnectInfo.retryTimes; ++i) {
        ret = mMultiEngine->CreateSubscriber(mConnectInfo.peerNodeId, mConnectInfo.ip, mConnectInfo.port);
        if (ret == MMS_OK) {
            break;
        }

        NET_LOG_WARN("Subscribe to publisher failed, ret:" << ret << ", ip:" << mConnectInfo.ip << ", port:"
                                                           << mConnectInfo.port << ", retry times:" << i << ".");
    }

    if (ret != MMS_OK) {
        NET_LOG_ERROR("Subscribe to publisher failed, ret:" << ret << ", ip:" << mConnectInfo.ip
                                                            << ", port:" << mConnectInfo.port << ".");
    }

    return ret;
}

void MultiConnectTask::Run()
{
    if (mSyncConnect) {
        return SyncConnect();
    } else {
        return AsyncConnect();
    }
}

MultiNetConnector::MultiNetConnector(NetMulticastEngine *engine) : mMultiEngine(engine)
{
    if (mMultiEngine != nullptr) {
        mMultiEngine->IncreaseRef();
    }
}

MultiNetConnector::~MultiNetConnector()
{
    if (mMultiEngine != nullptr) {
        mMultiEngine->DecreaseRef();
        mMultiEngine = nullptr;
    }
}

BResult MultiNetConnector::Start()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        NET_LOG_WARN("Multicast net connector has been already started");
        return MMS_OK;
    }

    mExeService = ExecutorService::Create(NO_1, NO_2048);
    if (UNLIKELY(mExeService == nullptr)) {
        NET_LOG_ERROR("Failed to start execution service for multicast net connector " << mName
                                                                                       << ", probably out of memory.");
        return MMS_ALLOC_FAIL;
    }

    mExeService->SetThreadName("MultiNetConnect-" + mName);
    if (!(mExeService->Start())) {
        NET_LOG_ERROR("Failed to start execution service for multicast net connector " << mName);
        mExeService->Stop();
        return MMS_ERR;
    }

    mStarted = true;
    return MMS_OK;
}

void MultiNetConnector::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }
    mExeService->Stop();
    mExeService = nullptr;
    mStarted = false;
}

BResult MultiNetConnector::AsyncConnect(SubscriptionInfo &info, MulticastAsyncHandler &handler)
{
    ChkTrueNot(!info.ip.empty(), MMS_INVALID_PARAM);
    ChkTrueNot(info.port != 0, MMS_INVALID_PARAM);
    ChkTrueNot(info.retryTimes != 0, MMS_INVALID_PARAM);
    ChkTrueNot(handler != nullptr, MMS_INVALID_PARAM);

    auto task = MakeRef<MultiConnectTask>(mMultiEngine);
    if (UNLIKELY(task == nullptr)) {
        NET_LOG_ERROR("Failed to new multicast connection task in Net connector " << mName
                                                                                  << ", probably out of memory.");
        return MMS_ALLOC_FAIL;
    }

    task->mMultiEngine = mMultiEngine;
    task->mConnectInfo = info;
    task->mSyncConnect = false;
    task->mAsyncHandler = handler;
    auto result = mExeService->Execute(task.Get());
    if (UNLIKELY(!result)) {
        NET_LOG_ERROR("Failed to enqueue multicast connecting task into Net connector " << mName << ".");
        return MMS_ERR;
    }

    return MMS_OK;
}

BResult MultiNetConnector::SyncConnect(SubscriptionInfo &info)
{
    ChkTrueNot(!info.ip.empty(), MMS_INVALID_PARAM);
    ChkTrueNot(info.port != 0, MMS_INVALID_PARAM);
    ChkTrueNot(info.retryTimes != 0, MMS_INVALID_PARAM);

    auto task = MakeRef<MultiConnectTask>(mMultiEngine);
    if (UNLIKELY(task == nullptr)) {
        NET_LOG_ERROR("Failed to new multicast connection task in Net connector " << mName
                                                                                  << ", probably out of memory.");
        return MMS_ALLOC_FAIL;
    }

    task->mMultiEngine = mMultiEngine;
    task->mConnectInfo = info;
    task->mSyncConnect = true;
    auto result = mExeService->Execute(task.Get());
    if (UNLIKELY(!result)) {
        NET_LOG_ERROR("Failed to enqueue multicast connecting task into Net connector " << mName << ".");
        return MMS_ERR;
    }

    return task->Wait();
}
}
}
