/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include "rpc_engine_connector.h"

#include "rpc_engine.h"
#include "bio_execution.h"
#include "bio_ref.h"

namespace ock {
namespace bio {
RpcConnectTask::RpcConnectTask(RpcEngine *engine) : mEngine(engine)
{
    if (mEngine != nullptr) {
        mEngine->IncreaseRef();
    }
}

RpcConnectTask::~RpcConnectTask()
{
    if (mEngine != nullptr) {
        mEngine->DecreaseRef();
        mEngine = nullptr;
    }
}

BResult RpcConnectTask::DoConnect()
{
    BResult ret = BIO_OK;
    ChannelPtr ctrlChanel = nullptr;
    if ((ret = mEngine->mPeerCtrlChannels->GetChannel(mConnectInfo.peerId, ctrlChanel)) == BIO_NOT_EXISTS) {
        ret = mEngine->ConnectToPeer(mConnectInfo, true, ctrlChanel);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to connect ctrl plane to peer vnode id " << mConnectInfo.peerId);
            return BIO_ERR;
        }
        mEngine->mPeerCtrlChannels->AddChannel(mConnectInfo.peerId, ctrlChanel);
    } else if (ret != BIO_OK) {
        LOG_ERROR("Failed to connect ctrl plane by vnode id " << mConnectInfo.peerId);
    }

    ChannelPtr dataChanel = nullptr;
    if ((ret = mEngine->mPeerDataChannels->GetChannel(mConnectInfo.peerId, ctrlChanel)) == BIO_NOT_EXISTS) {
        ret = mEngine->ConnectToPeer(mConnectInfo, false, dataChanel);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to connect data plane to peer vnode id " << mConnectInfo.peerId);
            return BIO_ERR;
        }
        mEngine->mPeerDataChannels->AddChannel(mConnectInfo.peerId, dataChanel);
    } else if (ret != BIO_OK) {
        LOG_ERROR("Failed to connect data plane by vnode id " << mConnectInfo.peerId);
    }

    return BIO_OK;
}

void RpcConnectTask::Run()
{
    if (mSyncConnect) {
        return SyncConnect();
    } else {
        return AsyncConnect();
    }
}
/*
 * Async connector code starts here
 */
RpcConnector::RpcConnector(RpcEngine *engine) : mEngine(engine)
{
    if (mEngine != nullptr) {
        mEngine->IncreaseRef();
    }
}

RpcConnector::~RpcConnector()
{
    if (mEngine != nullptr) {
        mEngine->DecreaseRef();
        mEngine = nullptr;
    }
}

BResult RpcConnector::Start(const BioNetOptions &opt)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        LOG_WARN("Net connector " << opt.name << " has been already started");
        return BIO_OK;
    }

    ASSERT_RETURN(mEngine != nullptr, BIO_INVALID_PARAM);

    mName = opt.name;

    mExeService = ExecutorService::Create(1, NO_2048);
    if (mExeService == nullptr) {
        LOG_ERROR("Failed to start execution service for Net connector " << mName << ", probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    mExeService->SetThreadName("NetConn-" + mName);
    auto result = mExeService->Start();
    if (!result) {
        StopInner();
        LOG_ERROR("Failed to start execution service for Net connector " << mName);
        return BIO_ERR;
    }

    mStarted = true;
    return BIO_OK;
}

void RpcConnector::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    StopInner();
    mStarted = false;
}

void RpcConnector::StopInner()
{
    if (mExeService != nullptr) {
        mExeService->Stop();
        mExeService = nullptr;
    }
}

BResult RpcConnector::AsyncConnect(ConnectInfo &info, AsyncConnHandler &handler, uintptr_t ctx)
{
    ASSERT_RETURN(!info.ip.empty(), BIO_INVALID_PARAM);
    ASSERT_RETURN(info.peerId != 0, BIO_INVALID_PARAM);
    ASSERT_RETURN(info.port != 0, BIO_INVALID_PARAM);
    ASSERT_RETURN(info.retryTimes != 0, BIO_INVALID_PARAM);
    ASSERT_RETURN(handler != nullptr, BIO_INVALID_PARAM);

    /* create a connecting task */
    auto task = MakeRef<RpcConnectTask>(mEngine);
    if (UNLIKELY(task == nullptr)) {
        LOG_ERROR("Failed to new connection task in Net connector " << mName << ", probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    /* set task info */
    task->mEngine = mEngine;
    task->mConnectInfo = info;
    task->mSyncConnect = false;
    task->mAsyncHandler = handler;
    task->mUserCtx = ctx;

    auto result = mExeService->Execute(task.Get());
    if (UNLIKELY(!result)) {
        LOG_ERROR("Failed to enqueue connecting task into Net connector " << mName);
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult RpcConnector::SyncConnect(ConnectInfo &info)
{
    ASSERT_RETURN(!info.ip.empty(), BIO_INVALID_PARAM);
    ASSERT_RETURN(info.port != 0, BIO_INVALID_PARAM);
    ASSERT_RETURN(info.retryTimes != 0, BIO_INVALID_PARAM);

    /* create a connecting task */
    auto task = MakeRef<RpcConnectTask>(mEngine);
    if (UNLIKELY(task == nullptr)) {
        LOG_ERROR("Failed to new connection task in Net connector " << mName << ", probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    /* set task info */
    task->mEngine = mEngine;
    task->mConnectInfo = info;
    task->mSyncConnect = true;

    auto result = mExeService->Execute(task.Get());
    if (UNLIKELY(!result)) {
        LOG_ERROR("Failed to enqueue connecting task into Net connector " << mName);
        return BIO_ERR;
    }

    return task->Wait();
}
}
}