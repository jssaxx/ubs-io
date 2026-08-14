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
namespace {
constexpr uint32_t NET_TASK_SMALL_BUFFER_SIZE = 512;
constexpr uint32_t NET_TASK_SMALL_BUFFER_CACHE = 1024;
constexpr uint32_t NET_TASK_LARGE_BUFFER_SIZE = IO_SIZE_64K;
constexpr uint32_t NET_TASK_LARGE_BUFFER_CACHE = 256;

class NetTaskBufferPool {
public:
    static NetTaskBufferPool &Instance()
    {
        static NetTaskBufferPool instance;
        return instance;
    }

    void *Acquire(uint32_t size, uint32_t &capacity)
    {
        if (size <= mSmall.capacity) {
            return Acquire(mSmall, capacity);
        }
        if (size <= mLarge.capacity) {
            return Acquire(mLarge, capacity);
        }
        capacity = size;
        return malloc(size);
    }

    void Release(void *data, uint32_t capacity)
    {
        if (data == nullptr) {
            return;
        }
        if (capacity == mSmall.capacity) {
            Release(mSmall, data);
            return;
        }
        if (capacity == mLarge.capacity) {
            Release(mLarge, data);
            return;
        }
        free(data);
    }

private:
    struct Bucket {
        Bucket(uint32_t bufferCapacity, uint32_t cacheLimit) : capacity(bufferCapacity), limit(cacheLimit) {}

        uint32_t capacity;
        uint32_t limit;
        std::mutex lock;
        std::vector<void *> buffers;
    };

    NetTaskBufferPool()
        : mSmall(NET_TASK_SMALL_BUFFER_SIZE, NET_TASK_SMALL_BUFFER_CACHE),
          mLarge(NET_TASK_LARGE_BUFFER_SIZE, NET_TASK_LARGE_BUFFER_CACHE)
    {}

    ~NetTaskBufferPool()
    {
        Clear(mSmall);
        Clear(mLarge);
    }

    static void *Acquire(Bucket &bucket, uint32_t &capacity)
    {
        capacity = bucket.capacity;
        {
            std::lock_guard<std::mutex> lock(bucket.lock);
            if (!bucket.buffers.empty()) {
                void *data = bucket.buffers.back();
                bucket.buffers.pop_back();
                return data;
            }
        }
        return malloc(bucket.capacity);
    }

    static void Release(Bucket &bucket, void *data)
    {
        std::lock_guard<std::mutex> lock(bucket.lock);
        if (bucket.buffers.size() < bucket.limit) {
            bucket.buffers.push_back(data);
            return;
        }
        free(data);
    }

    static void Clear(Bucket &bucket)
    {
        for (auto data : bucket.buffers) {
            free(data);
        }
        bucket.buffers.clear();
    }

    Bucket mSmall;
    Bucket mLarge;
};
} // namespace

NetTaskContext::~NetTaskContext()
{
    ReleaseData();
}

void NetTaskContext::ReleaseData()
{
    if (mData != nullptr) {
        NetTaskBufferPool::Instance().Release(mData, mDataCapacity);
    }
    mData = nullptr;
    mDataLen = 0;
    mDataCapacity = 0;
    mDataType = INVALID_DATA;
}

BResult NetTaskContext::Clone(ServiceContext &oldCtx)
{
    ReleaseData();
    auto ret = ServiceContext::Clone(*this, oldCtx, false);
    if (UNLIKELY(ret != MMS_OK)) {
        return MMS_ALLOC_FAIL;
    }

    uint32_t dataLen = oldCtx.MessageDataLen();
    if (dataLen == 0) {
        return MMS_OK;
    }

    void *data = NetTaskBufferPool::Instance().Acquire(dataLen, mDataCapacity);
    if (UNLIKELY(data == nullptr)) {
        mDataCapacity = 0;
        return MMS_ALLOC_FAIL;
    }
    if (UNLIKELY(memcpy_s(data, mDataCapacity, oldCtx.MessageData(), dataLen) != EOK)) {
        NetTaskBufferPool::Instance().Release(data, mDataCapacity);
        mDataCapacity = 0;
        return MMS_ERR;
    }
    mData = data;
    mDataLen = dataLen;
    mDataType = OUTER_DATA;
    return MMS_OK;
}

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
