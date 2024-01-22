/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef RPC_ENGINE_CONNECTOR_H
#define RPC_ENGINE_CONNECTOR_H

#include "net_common.h"
#include "bio_execution.h"

namespace ock {
namespace bio {
class RpcConnectTask : public Runnable {
public:
    explicit RpcConnectTask(RpcEngine *engine);
    ~RpcConnectTask() override;

    void Run() override;

    BResult Wait()
    {
        {
            std::unique_lock<std::mutex> lk(mMutex);
            /* wait if mChannel is nullptr */
            mCV.wait(lk, [this] { return mFinish; });
        }
        return mResult;
    }

private:
    void SetChannelAndNotify(BResult ret)
    {
        mResult = ret;
        {
            std::lock_guard<std::mutex> guard(mMutex);
            mFinish = true;
        }
        mCV.notify_one();
    }

    BResult DoConnect();

    inline void SyncConnect()
    {
        auto ret = DoConnect();
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to sync connect");
        }
        SetChannelAndNotify(ret);
    }

    inline void AsyncConnect()
    {
        auto ret = DoConnect();
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to async connect");
        }

        mAsyncHandler(mUserCtx, ret, mConnectInfo);
    }

private:
    RpcEngine *mEngine { nullptr };        /* engine, use raw pointer to avoid include files dead-locks */
    ConnectInfo mConnectInfo {};              /* connect info */
    BResult mResult = BIO_OK;                 /* connect result */
    bool mSyncConnect = true;                 /* connect type */
    bool mFinish = false;                     /* for sync connect */
    std::mutex mMutex;                        /* for sync connect */
    std::condition_variable mCV;              /* for sync connect */
    AsyncConnHandler mAsyncHandler = nullptr; /* for async connect */
    uintptr_t mUserCtx = 0;                   /* for async connect */

    friend class RpcConnector;
};

/*
 * Async connector which has a thread to do connection work
 */
class RpcConnector {
public:
    explicit RpcConnector(RpcEngine *engine);
    ~RpcConnector();

    BResult Start(const BioNetOptions &);
    void Stop();

    void SetMyNodeId(const BioNodeId &myId)
    {
        mMyNodeId = myId;
    }

    BResult AsyncConnect(ConnectInfo &info, AsyncConnHandler &handler, uintptr_t ctx);
    BResult SyncConnect(ConnectInfo &info);

    DEFINE_REF_COUNT_FUNCTIONS
private:
    void StopInner();

private:
    DEFINE_REF_COUNT_VARIABLE

    ExecutorServicePtr mExeService { nullptr };
    BioNodeId mMyNodeId;
    RpcEngine *mEngine { nullptr };

    std::mutex mMutex;
    bool mStarted = false;
    std::string mName;
};
using RpcConnectorPtr = Ref<RpcConnector>;
}
}

#endif // RPC_ENGINE_CONNECTOR_H
