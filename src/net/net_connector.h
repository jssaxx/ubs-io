/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef NET_CONNECTOR_H
#define NET_CONNECTOR_H

#include "net_common.h"
#include "bio_execution.h"

namespace ock {
namespace bio {
class ConnectTask : public Runnable {
public:
    explicit ConnectTask(NetEngine *engine);
    ~ConnectTask() override;

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
            NET_LOG_ERROR("Failed to sync connect");
        }
        SetChannelAndNotify(ret);
    }

    inline void AsyncConnect()
    {
        auto ret = DoConnect();
        if (ret != BIO_OK) {
            NET_LOG_ERROR("Failed to async connect");
        }
        mAsyncHandler(mUserCtx, ret, mConnectInfo);
    }

private:
    ConnectMode mode = ConnectMode::CONNECT_RPC;    /* connect mode */
    NetEngine *mEngine = nullptr;                   /* engine, use raw pointer to avoid include files dead-locks */
    ConnectInfo mConnectInfo{};                     /* connect info */
    BResult mResult = BIO_OK;                       /* connect result */
    bool mSyncConnect = true;                       /* connect type */
    bool mFinish = false;                           /* for sync connect */
    std::mutex mMutex;                              /* for sync connect */
    std::condition_variable mCV;                    /* for sync connect */
    AsyncConnHandler mAsyncHandler = nullptr;       /* for async connect */
    uintptr_t mUserCtx = 0;                         /* for async connect */

    friend class NetConnector;
};

class NetConnector {
public:
    explicit NetConnector(NetEngine *engine);
    ~NetConnector();

    BResult Start();
    void Stop();

    inline void SetLocalNodeId(const uint32_t &nodeId)
    {
        mLocalNodeId = nodeId;
    }

    BResult AsyncConnect(ConnectInfo &info, AsyncConnHandler &handler, uintptr_t ctx);
    BResult SyncConnect(ConnectInfo &info);

    DEFINE_REF_COUNT_FUNCTIONS

private:
    DEFINE_REF_COUNT_VARIABLE

    bool mStarted = false;
    ExecutorServicePtr mExeService = nullptr;
    uint32_t mLocalNodeId = UINT32_MAX;
    NetEngine *mEngine = nullptr;
    std::mutex mMutex;
    std::string mName;
};

using NetConnectorPtr = Ref<NetConnector>;
}
}
#endif // NET_CONNECTOR_H