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
        NET_LOG_INFO("Net executor " << mName << " has been already started");
        return BIO_OK;
    }

    mExeService = ExecutorService::Create(coreThreadNum, queueSize);
    if (mExeService == nullptr) {
        NET_LOG_ERROR("Failed to start execution service for " << mName << ", probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    mExeService->SetThreadName(mName);
    if (!(mExeService->Start())) {
        NET_LOG_ERROR("Failed to start execution service for " << mName << ".");
        StopInner();
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
    if (UNLIKELY(handler == nullptr)) {
        NET_LOG_ERROR("Handler is nullptr.");
        return BIO_INVALID_PARAM;
    }

    auto task = MakeRef<NetTask>(handler);
    if (UNLIKELY(task == nullptr)) {
        NET_LOG_ERROR("Failed to new event task in " << mName << ", probably out of memory");
        return BIO_ALLOC_FAIL;
    }

    auto result = task->CloneCtx(context);
    if (UNLIKELY(result != BIO_OK)) {
        NET_LOG_ERROR("Failed to clone context in " << mName);
        return BIO_ERR;
    }

    if (UNLIKELY(!(mExeService->Execute(task.Get())))) {
        NET_LOG_ERROR("Failed to enqueue event task into " << mName << ", opcode " << context.OpCode());
        return BIO_ERR;
    }

    return BIO_OK;
}
}
}