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

#ifndef MMSCORE_MULTICAST_CONNECTOR_H
#define MMSCORE_MULTICAST_CONNECTOR_H

#include <condition_variable>

#include "net_common.h"
#include "mms_execution.h"

namespace ock {
namespace mms {
class MultiConnectTask : public Runnable {
public:
    explicit MultiConnectTask(NetMulticastEngine *engine);

    ~MultiConnectTask() override;

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
    void NotifyConnectDone(BResult ret)
    {
        mResult = ret;
        {
            std::lock_guard<std::mutex> guard(mMutex);
            mFinish = true;
        }
        mCV.notify_one();
    }

    BResult DoConnect();

    void SyncConnect()
    {
        auto ret = DoConnect();
        if (ret != MMS_OK) {
            NET_LOG_ERROR("Failed to sync connect");
        }
        NotifyConnectDone(ret);
    }

    void AsyncConnect()
    {
        auto ret = DoConnect();
        if (ret != MMS_OK) {
            NET_LOG_ERROR("Failed to async connect");
        }
        mAsyncHandler(ret, mConnectInfo);
    }

private:
    NetMulticastEngine *mMultiEngine = nullptr; /* engine, use raw pointer to avoid include files dead-locks */
    SubscriptionInfo mConnectInfo{}; /* connect info */
    BResult mResult = MMS_OK; /* connect result */
    bool mSyncConnect = false; /* connect type */
    bool mFinish = false; /* for sync connect */
    std::mutex mMutex; /* for sync connect */
    std::condition_variable mCV; /* for sync connect */
    MulticastAsyncHandler mAsyncHandler = nullptr; /* for async connect */

    friend class MultiNetConnector;
};

class MultiNetConnector {
public:
    explicit MultiNetConnector(NetMulticastEngine *engine);
    ~MultiNetConnector();

    BResult Start();
    void Stop();

    BResult AsyncConnect(SubscriptionInfo &info, MulticastAsyncHandler &handler);
    BResult SyncConnect(SubscriptionInfo &info);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    DEFINE_REF_COUNT_VARIABLE;

    bool mStarted = false;
    ExecutorServicePtr mExeService = nullptr;
    NetMulticastEngine *mMultiEngine = nullptr;
    std::mutex mMutex;
    std::string mName;
};

using MultiNetConnectorPtr = Ref<MultiNetConnector>;
}
}
#endif // MMSCORE_MULTICAST_CONNECTOR_H
