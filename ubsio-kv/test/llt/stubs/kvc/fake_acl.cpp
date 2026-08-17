/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include "dl_acl_api.h"
#include "stub_control.h"

namespace {
struct AclStubState {
    std::mutex mutex;
    std::unordered_map<std::string, int> results;
    std::unordered_map<std::string, int> calls;
    int32_t currentDevice{-1};
};

AclStubState &State()
{
    static AclStubState state;
    return state;
}

int ResultLocked(AclStubState &state, const char *name)
{
    ++state.calls[name];
    auto found = state.results.find(name);
    return found == state.results.end() ? 0 : found->second;
}
}

extern "C" void FakeAclReset()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.results.clear();
    state.calls.clear();
    state.currentDevice = -1;
}

extern "C" void FakeAclSetResult(const char *name, int result)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.results[name] = result;
}

extern "C" int FakeAclGetCallCount(const char *name)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.calls[name];
}

extern "C" void FakeAclSetCurrentDevice(int32_t device)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.currentDevice = device;
}

extern "C" int32_t FakeAclGetCurrentDevice()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.currentDevice;
}

extern "C" int32_t aclrtGetDevice(int32_t *device)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtGetDevice");
    if (result == 0 && device != nullptr) {
        *device = state.currentDevice;
    }
    return result;
}

extern "C" int32_t aclrtSetDevice(int32_t device)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtSetDevice");
    if (result == 0) {
        state.currentDevice = device;
    }
    return result;
}

extern "C" int aclrtCreateStream(void **stream)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtCreateStream");
    if (result == 0 && stream != nullptr) {
        *stream = reinterpret_cast<void *>(0x1000UL);
    }
    return result;
}

extern "C" int aclrtCreateStreamWithConfig(void **stream, int32_t, uint32_t)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtCreateStreamWithConfig");
    if (result == 0 && stream != nullptr) {
        *stream = reinterpret_cast<void *>(0x2000UL);
    }
    return result;
}

extern "C" int aclrtDestroyStream(void *)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return ResultLocked(state, "aclrtDestroyStream");
}

extern "C" int aclrtSynchronizeStream(void *)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return ResultLocked(state, "aclrtSynchronizeStream");
}

extern "C" int32_t aclrtMalloc(void **ptr, size_t size, uint32_t)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtMalloc");
    if (result == 0 && ptr != nullptr) {
        *ptr = std::malloc(size);
        if (*ptr == nullptr) {
            return 1;
        }
    }
    return result;
}

extern "C" int aclrtFree(void *ptr)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtFree");
    if (result == 0) {
        std::free(ptr);
    }
    return result;
}

extern "C" int32_t aclrtMallocHost(void **ptr, size_t size)
{
    return aclrtMalloc(ptr, size, 0);
}

extern "C" int aclrtFreeHost(void *ptr)
{
    return aclrtFree(ptr);
}

extern "C" int32_t aclrtMemcpy(void *dst, size_t destMax, const void *src, size_t count, uint32_t)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtMemcpy");
    if (result == 0 && dst != nullptr && src != nullptr) {
        std::memcpy(dst, src, std::min(destMax, count));
    }
    return result;
}

extern "C" int32_t aclrtMemcpyAsync(void *dst, size_t destMax, const void *src, size_t count,
                                     uint32_t, void *)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtMemcpyAsync");
    if (result == 0 && dst != nullptr && src != nullptr) {
        std::memcpy(dst, src, std::min(destMax, count));
    }
    return result;
}

extern "C" int32_t aclrtMemcpyBatch(void **dsts, size_t *destMax, void **srcs, size_t *sizes,
                                     size_t numBatches, ock::ubsio::AclrtMemcpyBatchAttr *,
                                     size_t *, size_t, size_t *failIndex)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtMemcpyBatch");
    if (result != 0) {
        if (failIndex != nullptr) {
            *failIndex = 0;
        }
        return result;
    }
    for (size_t index = 0; index < numBatches; ++index) {
        std::memcpy(dsts[index], srcs[index], std::min(destMax[index], sizes[index]));
    }
    return 0;
}

extern "C" int32_t aclrtMemcpy2d(void *dst, size_t dpitch, const void *src, size_t spitch,
                                  size_t width, size_t height, uint32_t)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtMemcpy2d");
    if (result == 0) {
        for (size_t row = 0; row < height; ++row) {
            std::memcpy(static_cast<char *>(dst) + row * dpitch,
                        static_cast<const char *>(src) + row * spitch, width);
        }
    }
    return result;
}

extern "C" int32_t aclrtMemcpy2dAsync(void *dst, size_t dpitch, const void *src, size_t spitch,
                                       size_t width, size_t height, uint32_t kind, void *)
{
    return aclrtMemcpy2d(dst, dpitch, src, spitch, width, height, kind);
}

extern "C" int32_t aclrtMemset(void *dst, size_t destMax, int32_t value, size_t count)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtMemset");
    if (result == 0 && dst != nullptr) {
        std::memset(dst, value, std::min(destMax, count));
    }
    return result;
}

extern "C" int32_t rtGetDeviceInfo(uint32_t deviceId, int32_t, int32_t, int64_t *value)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "rtGetDeviceInfo");
    if (result == 0 && value != nullptr) {
        *value = static_cast<int64_t>(deviceId) + 1000;
    }
    return result;
}

extern "C" int32_t aclrtGetLogicDevIdByUserDevId(int32_t userDevice, int32_t *logicDevice)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "aclrtGetLogicDevIdByUserDevId");
    if (result == 0 && logicDevice != nullptr) {
        *logicDevice = userDevice + 100;
    }
    return result;
}
