/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */
#include "flow_task_pool.h"

namespace ock {
namespace bio {
BResult FlowTaskPool::Start(uint32_t coreThreadNum, uint32_t queueSize)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        LOG_WARN("Flow executor " << mName << " has been already started");
        return BIO_OK;
    }

    mExeService = ExecutorService::Create(coreThreadNum, queueSize);
    if (mExeService == nullptr) {
        LOG_ERROR("Failed to start execution service for " << mName << ", probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    mExeService->SetThreadName(mName);
    auto result = mExeService->Start();
    if (!result) {
        StopInner();
        LOG_ERROR("Failed to start execution service for " << mName);
        return BIO_ERR;
    }

    mStarted = true;
    return BIO_OK;
}

void FlowTaskPool::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    StopInner();
    mStarted = false;
}

BResult FlowTaskPool::AddTask(FlowTaskHandler &handler)
{
    ASSERT_RETURN(handler != nullptr, BIO_INVALID_PARAM);

    auto task = MakeRef<FlowTask>(handler);
    if (UNLIKELY(task == nullptr)) {
        LOG_ERROR("Failed to new event task in " << mName << ", probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    auto ret = mExeService->Execute(task.Get());
    if (UNLIKELY(!ret)) {
        LOG_ERROR("Failed to enqueue event task into " << mName);
        return BIO_ERR;
    }

    return BIO_OK;
}
}
}