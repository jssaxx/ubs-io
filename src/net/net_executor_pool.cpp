/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#include "net_executor_pool.h"

namespace ock {
namespace bio {
BResult NetExecutorPool::Start(uint32_t coreThreadNum, uint32_t queueSize)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        LOG_WARN("Net executor " << mName << " has been already started");
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

void NetExecutorPool::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    StopInner();
    mStarted = false;
}

BResult NetExecutorPool::AddTask(NetTaskHandler &handler, ServiceContext &context)
{
    ASSERT_RETURN(handler != nullptr, BIO_INVALID_PARAM);

    auto task = MakeRef<NetTask>(handler);
    if (UNLIKELY(task == nullptr)) {
        LOG_ERROR("Failed to new event task in " << mName << ", probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    auto result = task->CloneCtx(context);
    if (UNLIKELY(result != BIO_OK)) {
        LOG_ERROR("Failed to clone context in " << mName);
        return BIO_ERR;
    }

    auto ret = mExeService->Execute(task.Get());
    if (UNLIKELY(!ret)) {
        LOG_ERROR("Failed to enqueue event task into " << mName << ", opcode " << context.OpCode());
        return BIO_ERR;
    }

    return BIO_OK;
}
}
}