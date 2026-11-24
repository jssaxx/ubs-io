/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-io is licensed under the Mulan PSL v2. */
#include <algorithm>
#include <cerrno>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>
extern "C" {
#include "nds_api.h"
}
#include "stub_control.h"

struct nds_io_ctx {
    size_t capacity;
    std::deque<nds_io_event> events;
};

namespace {
struct MemoryRange {
    uintptr_t addr;
    uint64_t size;

    bool Contains(const nds_io_vec &iov) const
    {
        return iov.buf_addr >= addr && iov.buf_len <= size && iov.buf_addr - addr <= size - iov.buf_len;
    }
};

std::mutex stateMutex;
std::unordered_map<std::string, long long> results;
std::unordered_map<std::string, int> calls;
std::vector<MemoryRange> registeredMemory;

long long Result(const char *name, long long fallback = 0)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    ++calls[name];
    auto found = results.find(name);
    return found == results.end() ? fallback : found->second;
}

bool IsRegistered(const nds_io_cb &request)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return std::any_of(registeredMemory.begin(), registeredMemory.end(), [&](const MemoryRange &range) {
        return std::all_of(request.iov, request.iov + request.iov_cnt, [&](const nds_io_vec &iov) {
            return range.Contains(iov);
        });
    });
}
} // namespace

extern "C" void FakeNdsReset()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    results.clear();
    calls.clear();
}

extern "C" void FakeNdsSetResult(const char *name, long long result)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    results[name] = result;
}

extern "C" int FakeNdsGetCallCount(const char *name)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return calls[name];
}

extern "C" int nds_init(nds_init_param *param)
{
    param->version = 1;
    return Result("nds_init");
}

extern "C" int nds_exit()
{
    const int ret = Result("nds_exit");
    std::lock_guard<std::mutex> lock(stateMutex);
    registeredMemory.clear();
    return ret;
}

extern "C" int nds_register_fs(const nds_fs_desc *) { return Result("nds_register_fs"); }
extern "C" int nds_unregister_fs(const nds_fs_desc *) { return Result("nds_unregister_fs"); }

extern "C" int nds_register_mem(void *addr, uint64_t size, int)
{
    (void)Result("nds_register_mem");
    std::lock_guard<std::mutex> lock(stateMutex);
    registeredMemory.push_back({reinterpret_cast<uintptr_t>(addr), size});
    return 0;
}

extern "C" int nds_unregister_mem(void *addr, uint64_t size, int)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    const auto entry = std::find_if(registeredMemory.begin(), registeredMemory.end(), [addr, size](const MemoryRange &r) {
        return r.addr == reinterpret_cast<uintptr_t>(addr) && r.size == size;
    });
    if (entry == registeredMemory.end()) {
        return -ENOENT;
    }
    registeredMemory.erase(entry);
    return 0;
}

extern "C" int nds_io_new_ctx(const nds_io_ctx_param *param, nds_io_ctx **ctx)
{
    *ctx = new nds_io_ctx{param->max_io_cnt, {}};
    return Result("nds_io_new_ctx");
}

extern "C" int nds_io_destroy_ctx(nds_io_ctx *ctx)
{
    delete ctx;
    return Result("nds_io_destroy_ctx");
}

extern "C" int nds_io_submit(nds_io_ctx *ctx, int nr, const nds_io_cb *cbs)
{
    (void)Result("nds_io_submit");
    if (static_cast<size_t>(nr) + ctx->events.size() > ctx->capacity) {
        return -EINVAL;
    }
    for (int i = 0; i < nr; ++i) {
        const auto &request = cbs[i];
        const bool registered = (request.rw_flags & NDS_IO_F_REGISTERED_MEM) != 0;
        if (registered && !IsRegistered(request)) {
            return i == 0 ? -ERANGE : i;
        }
        (void)Result(registered ? "nds_submit_registered" : "nds_submit_unregistered");
        nds_io_event event{};
        event.user_data = request.user_data;
        uint64_t offset = request.offset;
        for (uint32_t j = 0; j < request.iov_cnt; ++j) {
            const auto &iov = request.iov[j];
            const auto bytes = pread(request.obj.fd, reinterpret_cast<void *>(iov.buf_addr), iov.buf_len, offset);
            if (bytes != static_cast<ssize_t>(iov.buf_len)) {
                event.res = -EIO;
                break;
            }
            offset += iov.buf_len;
        }
        ctx->events.push_back(event);
    }
    return nr;
}

extern "C" int nds_io_getevents(nds_io_ctx *ctx, int minNr, int nr, nds_io_event *events, timespec *)
{
    if (minNr != 1 || nr > static_cast<int>(ctx->events.size())) {
        return -EINVAL;
    }
    for (int i = 0; i < nr; ++i) {
        events[i] = ctx->events.front();
        ctx->events.pop_front();
    }
    return nr;
}
