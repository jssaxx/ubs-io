/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */
#ifndef BOOSTIO_FLOW_TASK_POOL_H
#define BOOSTIO_FLOW_TASK_POOL_H

#include "bio_execution.h"
#include "bio_err.h"
#ifdef USE_DEBUG_TOOLS
#include "bio_tracepoint_helper.h"
#endif

namespace ock {
namespace bio {
using FlowTaskHandler = std::function<void(void)>;
class FlowTask : public Runnable {
public:
    explicit FlowTask(FlowTaskHandler &handler) : mHandler(handler) {}

    ~FlowTask() override = default;

    void Run() override
    {
        mHandler();
    }

protected:
    FlowTaskHandler mHandler;
    friend class FlowTaskPool;
};

class FlowTaskPool {
public:
    explicit FlowTaskPool(const std::string &name)
    {
        /* thread name should be less than 16 chars */
        mName = "Evt" + name;
    }

    ~FlowTaskPool()
    {
        Stop();
    }

    BResult Start(uint32_t coreThreadNum, uint32_t queueSiz);

    void Stop();

    BResult AddTask(FlowTaskHandler &handler);

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
using FlowTaskPoolPtr = Ref<FlowTaskPool>;
}
}

#endif // BOOSTIO_FLOW_TASK_POOL_H
