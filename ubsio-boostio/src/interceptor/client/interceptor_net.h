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

#ifndef BOOSTIO_INTERCEPTOR_NET_H
#define BOOSTIO_INTERCEPTOR_NET_H

#include <mutex>
#include <atomic>
#include "net_engine.h"
#include "net_common.h"
#include "bio_ref.h"
#include "bio_err.h"
#include "interceptor_log.h"
#include "interceptor_read_index.h"

namespace ock {
namespace bio {

class InterceptorClientNetService {
public:
    static InterceptorClientNetService &Instance()
    {
        static InterceptorClientNetService instance;
        return instance;
    }

    ~InterceptorClientNetService() = default;

    int32_t StartNetService();
    void StopNetService();
    void ShutdownNetService();
    BResult CreateBioServerMem();
    BResult PrepareBeforeFork();
    BResult PrepareAfterForkChild();
    BResult InitNetForCurrentProcess()
    {
        return EnsureStartedForCurrentProcess();
    }

    BResult GetReadySendPid(uint32_t &pid)
    {
        auto readyRet = EnsureStartedForCurrentProcess();
        if (UNLIKELY(readyRet != BIO_OK)) {
            pid = 0;
            return readyRet;
        }
        pid = mPid.load(std::memory_order_relaxed);
        return pid == 0 ? BIO_NOT_READY : BIO_OK;
    }

    BResult EnsureBioShmForCurrentProcess();

    uint8_t *GetBioShmAddress(uint64_t offset, uint32_t len);
    bool FindReadIndexCache(uint64_t inode, uint64_t offset, uint64_t minLen, InterceptorReadIndexCache &cache);
    bool CopyFromReadIndex(uint64_t inode, uint64_t offset, size_t count, void *buf,
        InterceptorReadIndexCache *cache = nullptr);

    uint8_t *GetBioShmAddressFast(uint64_t offset, uint64_t len)
    {
        if (UNLIKELY(mBioShmAddr == nullptr)) {
            return nullptr;
        }

        if (UNLIKELY(offset < mBioShmOffset)) {
            return nullptr;
        }
        uint64_t relativeOffset = offset - mBioShmOffset;
        if (UNLIKELY(len > mBioShmLength || relativeOffset > mBioShmLength - len)) {
            return nullptr;
        }
        return mBioShmAddr + relativeOffset;
    }

    template <typename TReq, typename TResp>
    inline BResult SendSync(const BioNodeId target, uint16_t opcode, TReq &req, TResp &rsp)
    {
        auto readyRet = EnsureStartedForCurrentProcess();
        if (UNLIKELY(readyRet != BIO_OK)) {
            return readyRet;
        }
        return mNetEngine->SyncCall(target, opcode, req, rsp);
    }

    template <typename TReq, typename TResp>
    inline BResult SendSync(const BioNodeId target, uint16_t opcode, TReq &req, TResp **rsp, uint64_t &respLen)
    {
        auto readyRet = EnsureStartedForCurrentProcess();
        if (UNLIKELY(readyRet != BIO_OK)) {
            return readyRet;
        }
        return mNetEngine->SyncCall(target, opcode, req, rsp, respLen);
    }

    template <typename TResp>
    inline BResult SendSyncBuff(const BioNodeId target, uint16_t opcode, void *req, uint32_t reqLen, TResp &rsp)
    {
        auto readyRet = EnsureStartedForCurrentProcess();
        if (UNLIKELY(readyRet != BIO_OK)) {
            return readyRet;
        }
        return mNetEngine->SyncCallBuff(target, opcode, req, reqLen, rsp);
    }

    template <typename TReq>
    inline BResult SendAsyncNoResp(const BioNodeId target, uint16_t opcode, TReq &req)
    {
        auto readyRet = EnsureStartedForCurrentProcess();
        if (UNLIKELY(readyRet != BIO_OK)) {
            return readyRet;
        }
        return mNetEngine->AsyncCallWithoutResponse(target, opcode, req);
    }

    inline uint32_t GetSendPid()
    {
        return mPid.load(std::memory_order_relaxed);
    }

private:
    BResult EnsureStartedForCurrentProcess();
    void OrphanInheritedState(uint32_t currentPid);
    void StopNetServiceLocked();

private:
    std::mutex mStartLock;
    std::atomic<uint32_t> mPid = { 0 };
    std::atomic<bool> mReady = { false };
    std::atomic<bool> mShutdown = { false };
    NetEnginePtr mNetEngine = nullptr;
    int32_t mBioShmFd = -1;
    uint64_t mBioShmOffset = 0;
    uint64_t mBioShmLength = 0;
    uint8_t *mBioShmAddr = nullptr;
    int32_t mReadIndexFd = -1;
    uint64_t mReadIndexLength = 0;
    InterceptorReadIndexHeader *mReadIndex = nullptr;
};
}
}
#endif // BOOSTIO_INTERCEPTOR_NET_H
