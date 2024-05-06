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
    ChannelPtr ctrlChanel = nullptr;
    if ((ret = mEngine->GetCtrlChannelMgr()->GetChannel(mConnectInfo.peerId, ctrlChanel)) == BIO_NOT_EXISTS) {
        LVOS_TP_START(SERVER_NET_CONNECT_FAIL, &ret, BIO_INNER_RETRY);
        ret = mEngine->ConnectToPeer(mode, mConnectInfo, true, ctrlChanel);
        LVOS_TP_END;
        if (ret != BIO_OK) {
            NET_LOG_ERROR("Failed to connect ctrl plane to peer target node id " << mConnectInfo.peerId.nid <<
                ", pid:" << mConnectInfo.peerId.pid << ".");
            return BIO_ERR;
        }
        mEngine->GetCtrlChannelMgr()->AddChannel(mConnectInfo.peerId, ctrlChanel);
        NET_LOG_INFO("Connect ctrl succeed, channel id:" << ctrlChanel->Id() << ", target node id:" <<
            mConnectInfo.peerId.nid << ", pid:" << mConnectInfo.peerId.pid << ".");
    } else {
        NET_LOG_INFO("Exist connect ctrl plane by target node id " << mConnectInfo.peerId.nid << ", pid:" <<
            mConnectInfo.peerId.pid << ".");
    }

    ChannelPtr dataChanel = nullptr;
    if ((ret = mEngine->GetDataChannelMgr()->GetChannel(mConnectInfo.peerId, dataChanel)) == BIO_NOT_EXISTS) {
        ret = mEngine->ConnectToPeer(mode, mConnectInfo, false, dataChanel);
        if (ret != BIO_OK) {
            NET_LOG_ERROR("Failed to connect data plane to peer target node id " << mConnectInfo.peerId.nid <<
                ", pid:" << mConnectInfo.peerId.pid << ".");
            return BIO_ERR;
        }
        mEngine->GetDataChannelMgr()->AddChannel(mConnectInfo.peerId, dataChanel);
        NET_LOG_INFO("Connect data succeed, channel id:" << dataChanel->Id() << ", target node id:" <<
            mConnectInfo.peerId.nid << ", pid:" << mConnectInfo.peerId.pid << ".");
    } else if (ret != BIO_OK) {
        NET_LOG_INFO("Exist connect data plane by target node id " << mConnectInfo.peerId.nid << ", pid:" <<
            mConnectInfo.peerId.pid << ".");
    }

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

    mExeService = ExecutorService::Create(NO_4, NO_2048);
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
    ChkTrueNot(info.port != 0, BIO_INVALID_PARAM);
    ChkTrueNot(info.retryTimes != 0, BIO_INVALID_PARAM);
    ChkTrueNot(handler != nullptr, BIO_INVALID_PARAM);

    auto task = MakeRef<ConnectTask>(mEngine);
    if (UNLIKELY(task == nullptr)) {
        NET_LOG_ERROR("Failed to new connection task in Net connector " << mName << ", probably out of memory.");
        return BIO_ALLOC_FAIL;
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

    task->mode = (info.srcId.nid == INVALID_NID) ? CONNECT_IPC : CONNECT_RPC;
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