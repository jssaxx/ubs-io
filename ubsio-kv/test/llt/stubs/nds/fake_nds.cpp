/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <cstring>
#include <mutex>
#include <string>
#include <sys/uio.h>
#include <unistd.h>
#include <unordered_map>
#include "ubsio_kvc_log.h"
#include "ubsio_nds_dl_api.h"
#include "stub_control.h"

namespace {
struct NdsStubState {
    std::mutex mutex;
    std::unordered_map<std::string, long long> results;
    std::unordered_map<std::string, int> calls;
};

NdsStubState &State()
{
    static NdsStubState state;
    return state;
}

long long ResultLocked(NdsStubState &state, const char *name, long long defaultValue)
{
    ++state.calls[name];
    auto found = state.results.find(name);
    return found == state.results.end() ? defaultValue : found->second;
}
}

extern "C" void FakeNdsReset()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.results.clear();
    state.calls.clear();
}

extern "C" void FakeNdsSetResult(const char *name, long long result)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.results[name] = result;
}

extern "C" int FakeNdsGetCallCount(const char *name)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.calls[name];
}

extern "C" int nds_init(int)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return static_cast<int>(ResultLocked(state, "nds_init", 0));
}

extern "C" int nds_init_async(int)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return static_cast<int>(ResultLocked(state, "nds_init_async", 0));
}

extern "C" int nds_uninit()
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return static_cast<int>(ResultLocked(state, "nds_uninit", 0));
}

extern "C" int nds_open(const char *, int, ...)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "nds_open", 0);
    if (result < 0) {
        return static_cast<int>(result);
    }
    return dup(STDERR_FILENO);
}

extern "C" int nds_regmem(ock::ubsio::nds_fileid_t, const void *, size_t)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return static_cast<int>(ResultLocked(state, "nds_regmem", 0));
}

extern "C" int nds_unregmem(ock::ubsio::nds_fileid_t, const void *, size_t)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return static_cast<int>(ResultLocked(state, "nds_unregmem", 0));
}

extern "C" ssize_t nds_read(ock::ubsio::nds_fileid_t, void *buffer, off_t bufferOffset,
                             size_t size, off_t)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto result = ResultLocked(state, "nds_read", static_cast<long long>(size));
    if (result >= 0 && buffer != nullptr) {
        std::memset(static_cast<char *>(buffer) + bufferOffset, 'N',
                    static_cast<size_t>(result));
    }
    return static_cast<ssize_t>(result);
}

extern "C" ssize_t nds_readv_batch(ock::ubsio::nds_fileid_t, const struct iovec *vectors,
                                    size_t count, off_t, size_t)
{
    auto &state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    size_t total = 0;
    for (size_t index = 0; index < count; ++index) {
        total += vectors[index].iov_len;
    }
    auto result = ResultLocked(state, "nds_readv_batch", static_cast<long long>(total));
    if (result >= 0) {
        for (size_t index = 0; index < count; ++index) {
            std::memset(vectors[index].iov_base, 'R', vectors[index].iov_len);
        }
    }
    return static_cast<ssize_t>(result);
}
