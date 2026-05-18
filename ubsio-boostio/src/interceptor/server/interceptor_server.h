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

#ifndef BOOSTIO_INTERCEPTOR_SERVER_H
#define BOOSTIO_INTERCEPTOR_SERVER_H

#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "bio_err.h"
#include "bio_client_net.h"
#include "interceptor_read_index.h"

namespace ock {
namespace bio {

struct InterceptorReadBufferSlot {
    uintptr_t address = 0;
};

class InterceptorServer {
public:
    static InterceptorServer &GetInstance()
    {
        static InterceptorServer instance;
        return instance;
    }

    ~InterceptorServer();

    BResult Initialize();

    BResult HandleInterceptorRead(ServiceContext &ctx);
    BResult HandleInterceptorWritePrepareSpace(ServiceContext &ctx);
    BResult HandleInterceptorWriteCommitSpace(ServiceContext &ctx);
    BResult HandleInterceptorBioShmInit(ServiceContext &ctx);
    BResult HandleInterceptorReadIndexInvalidate(ServiceContext &ctx);
    BResult HandleInterceptorReadBufferRelease(ServiceContext &ctx);

    bool CheckInterceptorReadReq(InterceptorPreadIn *req);

private:
    BResult RegisterOpcode();
    void HandleProcBroken(uint32_t pid);
    BResult EnsureReadIndex();
    void CleanupReadIndex();
    void CleanupReadIndexLocked();
    BResult AcquireReadBuffer(uint32_t pid, uintptr_t &address);
    void TrackReadBufferLocked(uint32_t pid, uintptr_t address);
    bool UntrackReadBufferLocked(uint32_t pid, uintptr_t address);
    void TryReleaseReadBufferFromRequest(const InterceptorPreadIn *req);
    void GetReadBufferPoolState(uint64_t &allocated, uint64_t &freeCount);
    bool ReleaseReadBuffer(uint32_t pid, uintptr_t address);
    void ReleaseReadBufferFromResp(uint32_t pid, const InterceptorPreadOut &resp);
    void ReleaseReadBuffersByPid(uint32_t pid);
    void CleanupReadBuffers();
    int ReadDataToReadBuffer(uint32_t pid, uint64_t inode, uint64_t offset, uint64_t nbytes, bool prefetch,
        InterceptorPreadOut &resp);
    bool TryReplyReadDataFallback(ServiceContext &ctx, const InterceptorPreadIn *req, bool prefetch,
        int readAddrRet, const CacheReadAddrDesc &desc, int &traceRet);
    void ReplyReadError(ServiceContext &ctx, const InterceptorPreadIn *req, int ret, const CacheReadAddrDesc &desc);
    void ReplyReadAddress(ServiceContext &ctx, const InterceptorPreadIn *req, const CacheReadAddrDesc &desc);
    void PublishReadIndex(uint64_t inode, uint64_t offset, const InterceptorPreadOut &resp);
    void InvalidateReadIndex(uint64_t inode, uint64_t offset, uint64_t length);
    void BroadcastReadIndexInvalidate(uint64_t inode, uint64_t offset, uint64_t length);

    std::mutex mReadIndexLock;
    int32_t mReadIndexFd = -1;
    uint64_t mReadIndexLength = 0;
    InterceptorReadIndexHeader *mReadIndex = nullptr;
    std::mutex mReadBufferLock;
    std::unordered_map<uint32_t, std::vector<InterceptorReadBufferSlot>> mReadBuffers;
    uint64_t mReadBufferAllocatedCount = 0;
};
}
}
#endif // BOOSTIO_INTERCEPTOR_SERVER_H
