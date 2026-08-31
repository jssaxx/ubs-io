/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef BOOSTIO_FLOW_TASK_POOL_H
#define BOOSTIO_FLOW_TASK_POOL_H

#include "bio_err.h"
#include "bio_execution.h"
#include "bio_tracepoint_helper.h"

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

    ~FlowTaskPool() = default;

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

    ExecutorServicePtr mExeService{nullptr};
    std::mutex mMutex;
    bool mStarted = false;
    std::string mName;
};
using FlowTaskPoolPtr = Ref<FlowTaskPool>;
} // namespace bio
} // namespace ock

#endif // BOOSTIO_FLOW_TASK_POOL_H
