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
#ifndef NET_EXECUTOR_POOL_H
#define NET_EXECUTOR_POOL_H

#include "mms_execution.h"
#include "mms_err.h"
#include "net_common.h"

namespace ock {
namespace mms {
using NetTaskHandler = std::function<int32_t(ServiceContext &ctx)>;
class NetTask : public Runnable {
public:
    explicit NetTask(NetTaskHandler &handler) : mHandler(handler) {}

    ~NetTask() override = default;

    void Run() override
    {
        mHandler(mContext);
    }

    inline BResult CloneCtx(ServiceContext &oldCtx)
    {
        auto ret = ServiceContext::Clone(mContext, oldCtx, true);
        if (UNLIKELY(ret != MMS_OK)) {
            NET_LOG_ERROR("Failed to clone service ctx for cm event");
            return MMS_ALLOC_FAIL;
        }

        return MMS_OK;
    }

protected:
    NetTaskHandler mHandler;
    ServiceContext mContext;
    friend class NetExecutorPool;
};

class NetExecutorPool {
public:
    explicit NetExecutorPool(const std::string &name)
    {
        /* thread name should be less than 16 chars */
        mName = "NetExecutor" + name;
    }

    ~NetExecutorPool()
    {
        Stop();
    }

    BResult Start(uint32_t coreThreadNum, uint32_t queueSiz);

    void Stop();

    BResult AddTask(NetTaskHandler &handler, ServiceContext &context);

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    void StopInner()
    {
        if (mExeService != nullptr) {
            mExeService->Stop();
            mExeService = nullptr;
        }
    };

private:
    DEFINE_REF_COUNT_VARIABLE;

    ExecutorServicePtr mExeService{ nullptr };
    std::mutex mMutex;
    bool mStarted = false;
    std::string mName;
};
using NetExecutorPoolPtr = Ref<NetExecutorPool>;
}
}

#endif // NET_EXECUTOR_POOL_H

