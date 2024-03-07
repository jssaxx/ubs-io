/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "bio_execution.h"
#include "bio_ref.h"
#include "net_engine.h"
#include "net_connector.h"

namespace ock {
namespace bio {
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
    BResult ret = BIO_OK;
    ChannelPtr Chanel = nullptr;
    if ((ret = mEngine->GetChannelMgr()->GetChannel(mConnectInfo.peerId, Chanel)) == BIO_NOT_EXISTS) {
        ret = mEngine->ConnectToPeer(mode, mConnectInfo, Chanel);
        if (ret != BIO_OK) {
            NET_LOG_ERROR("Failed to connect data plane to peer target node id " << mConnectInfo.peerId);
            return BIO_ERR;
        }
        mEngine->GetChannelMgr()->AddChannel(mConnectInfo.peerId, Chanel);
        NET_LOG_INFO("Connect succeed, channel " << Chanel->Id() << ", target node id:" << mConnectInfo.peerId);
        return ret;
    } else if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to repeat connect data plane by target id " << mConnectInfo.peerId);
    }
    NET_LOG_INFO("Connect exist, channel " << Chanel->Id() << ", target node id:" << mConnectInfo.peerId);
    return BIO_OK;
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
        return BIO_OK;
    }

    mExeService = ExecutorService::Create(NO_1, NO_2048);
    if (UNLIKELY(mExeService == nullptr)) {
        NET_LOG_ERROR("Failed to start execution service for Net connector " << mName << ", probably out of memory.");
        return BIO_ALLOC_FAIL;
    }

    mExeService->SetThreadName("NetConnect-" + mName);
    if (!(mExeService->Start())) {
        NET_LOG_ERROR("Failed to start execution service for Net connector " << mName);
        mExeService->Stop();
        return BIO_ERR;
    }

    mStarted = true;
    return BIO_OK;
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
    ChkTrueNot(!info.ip.empty(), BIO_INVALID_PARAM);
    ChkTrueNot(info.peerId != 0, BIO_INVALID_PARAM);
    ChkTrueNot(info.port != 0, BIO_INVALID_PARAM);
    ChkTrueNot(info.retryTimes != 0, BIO_INVALID_PARAM);
    ChkTrueNot(handler != nullptr, BIO_INVALID_PARAM);

    auto task = MakeRef<ConnectTask>(mEngine);
    if (UNLIKELY(task == nullptr)) {
        NET_LOG_ERROR("Failed to new connection task in Net connector " << mName << ", probably out of memory.");
        return BIO_ALLOC_FAIL;
    }

    task->mode = (info.peerId == mLocalNodeId) ? CONNECT_IPC : CONNECT_RPC;
    task->mEngine = mEngine;
    task->mConnectInfo = info;
    task->mSyncConnect = false;
    task->mAsyncHandler = handler;
    task->mUserCtx = ctx;
    auto result = mExeService->Execute(task.Get());
    if (UNLIKELY(!result)) {
        NET_LOG_ERROR("Failed to enqueue connecting task into Net connector " << mName << ".");
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult NetConnector::SyncConnect(ConnectInfo &info)
{
    auto task = MakeRef<ConnectTask>(mEngine);
    if (UNLIKELY(task == nullptr)) {
        NET_LOG_ERROR("Failed to new connection task in Net connector " << mName << ", probably out of memory.");
        return BIO_ALLOC_FAIL;
    }

    task->mode =
        (info.peerId == mLocalNodeId || info.peerId == static_cast<uint32_t>(getpid())) ? CONNECT_IPC : CONNECT_RPC;
    task->mEngine = mEngine;
    task->mConnectInfo = info;
    task->mSyncConnect = true;
    auto result = mExeService->Execute(task.Get());
    if (UNLIKELY(!result)) {
        NET_LOG_ERROR("Failed to enqueue connecting task into Net connector " << mName << ".");
        return BIO_ERR;
    }

    return task->Wait();
}
}
}