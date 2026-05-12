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
#include "net_executor_pool.h"

namespace ock {
namespace mms {
BResult NetExecutorPool::Start(uint32_t coreThreadNum, uint32_t queueSize)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        NET_LOG_INFO("Net executor " << mName << " has been already started");
        return MMS_OK;
    }

    mExeService = ExecutorService::Create(coreThreadNum, queueSize);
    if (mExeService == nullptr) {
        NET_LOG_ERROR("Failed to start execution service for " << mName << ", probably out of memory");
        return MMS_ALLOC_FAIL;
    }

    mExeService->SetThreadName(mName);
    if (!(mExeService->Start())) {
        NET_LOG_ERROR("Failed to start execution service for " << mName << ".");
        StopInner();
        return MMS_ERR;
    }

    mStarted = true;
    return MMS_OK;
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
        return MMS_INVALID_PARAM;
    }

    auto task = MakeRef<NetTask>(handler);
    if (UNLIKELY(task == nullptr)) {
        NET_LOG_ERROR("Failed to new event task in " << mName << ", probably out of memory");
        return MMS_ALLOC_FAIL;
    }

    auto result = task->CloneCtx(context);
    if (UNLIKELY(result != MMS_OK)) {
        NET_LOG_ERROR("Failed to clone context in " << mName);
        return MMS_ERR;
    }

    if (UNLIKELY(!(mExeService->Execute(task.Get())))) {
        NET_LOG_ERROR("Failed to enqueue event task into " << mName << ", opcode " << context.OpCode());
        return MMS_ERR;
    }

    return MMS_OK;
}
}
}
