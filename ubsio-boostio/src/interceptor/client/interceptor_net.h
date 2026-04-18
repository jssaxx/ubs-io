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
#include <sys/mman.h>
#include <unistd.h>
#include "net_engine.h"
#include "net_common.h"
#include "net_block_pool.h"
#include "bio_ref.h"
#include "bio_err.h"
#include "interceptor_log.h"
namespace ock {
namespace bio {

class InterceptorClientNetService {
public:
    static InterceptorClientNetService &Instance()
    {
        static InterceptorClientNetService instance;
        return instance;
    }

    ~InterceptorClientNetService()
    {
        if (mDataMsgMemPool != nullptr) {
            mDataMsgMemPool->Stop();
            mDataMsgMemPool = nullptr;
        }
        if (mShmAddr != nullptr && mShmLength > 0) {
            munmap(mShmAddr, mShmLength);
            mShmAddr = nullptr;
        }
        if (mShmFd >= 0) {
            close(mShmFd);
            mShmFd = -1;
        }
        if (mPid != 0) {
            std::string shmName = "/interceptor_mem_pool_" + std::to_string(mPid);
            shm_unlink(shmName.c_str());
        }
        if (mBioShmAddr != nullptr && mBioShmLength > 0) {
            munmap(mBioShmAddr, mBioShmLength);
            mBioShmAddr = nullptr;
        }
        if (mBioShmFd >= 0) {
            close(mBioShmFd);
            mBioShmFd = -1;
        }
    }

    int32_t StartNetService();
    BResult CreateDataMessageMem();
    BResult InitBioShm();

    uint8_t *GetBioShmAddr()
    {
        return mBioShmAddr;
    }

    uint64_t GetBioShmLength() const
    {
        return mBioShmLength;
    }

    bool IsBioShmReady() const
    {
        return mBioShmAddr != nullptr;
    }

    uint8_t *GetShmAddress(uint64_t offset, uint32_t len)
    {
        return mNetEngine->GetShmAddress(offset, len);
    }

    uint64_t GetDataMsgMemBlockSize() const
    {
        return mDataMsgMemBlockSize;
    }

    BResult AllocShmBlock(uintptr_t &address, uint64_t &mrOffset)
    {
        if (UNLIKELY(!mReady.load())) {
            auto ret = StartNetService();
            if (ret != 0) {
                return BIO_NOT_READY;
            }
        }

        if (UNLIKELY(mDataMsgMemPool == nullptr)) {
            return BIO_NOT_READY;
        }

        auto ret = mDataMsgMemPool->AllocOne(address);
        if (UNLIKELY(ret != BIO_OK)) {
            return ret;
        }

        mrOffset = reinterpret_cast<uint8_t *>(address) - mDataMsgMemAddr;
        return BIO_OK;
    }

    uint8_t *GetShmBlockAddr(uint64_t mrOffset)
    {
        if (UNLIKELY(!mReady.load()) || mDataMsgMemAddr == nullptr) {
            return nullptr;
        }
        return mDataMsgMemAddr + mrOffset;
    }

    void ReleaseShmBlock(uint64_t mrOffset)
    {
        if (UNLIKELY(!mReady.load()) || mDataMsgMemPool == nullptr || mDataMsgMemAddr == nullptr) {
            return;
        }
        uintptr_t address = reinterpret_cast<uintptr_t>(mDataMsgMemAddr + mrOffset);
        mDataMsgMemPool->ReleaseOne(address);
    }

    template <typename TReq, typename TResp>
    inline BResult SendSync(const BioNodeId target, uint16_t opcode, TReq &req, TResp &rsp)
    {
        if (UNLIKELY(!mReady.load())) {
            auto ret = StartNetService();
            if (ret != 0) {
                return ret;
            }
        }
        return mNetEngine->SyncCall(target, opcode, req, rsp);
    }

    template <typename TReq, typename TResp>
    inline BResult SendSync(const BioNodeId target, uint16_t opcode, TReq &req, TResp **rsp, uint64_t &respLen)
    {
        if (UNLIKELY(!mReady.load())) {
            auto ret = StartNetService();
            if (ret != 0) {
                return ret;
            }
        }
        return mNetEngine->SyncCall(target, opcode, req, rsp, respLen);
    }

    template <typename TResp>
    inline BResult SendSyncBuff(const BioNodeId target, uint16_t opcode, void *req, uint32_t reqLen, TResp &rsp)
    {
        if (UNLIKELY(!mReady.load())) {
            auto ret = StartNetService();
            if (ret != 0) {
                return ret;
            }
        }
        return mNetEngine->SyncCallBuff(target, opcode, req, reqLen, rsp);
    }

    inline uint32_t GetSendPid()
    {
        return mPid;
    }

private:
    uint32_t mPid = 0;
    std::atomic<bool> mReady = { false };
    NetEnginePtr mNetEngine = nullptr;
    int32_t mShmFd = -1;
    uint64_t mShmOffset = 0;
    uint64_t mShmLength = 0;
    uint8_t *mShmAddr = nullptr;

    NetBlockPoolPtr mDataMsgMemPool = nullptr;
    uint8_t *mDataMsgMemAddr = nullptr;
    uint64_t mDataMsgMemBlockSize = 0;

    int32_t mBioShmFd = -1;
    uint64_t mBioShmOffset = 0;
    uint64_t mBioShmLength = 0;
    uint8_t *mBioShmAddr = nullptr;
};
}
}
#endif // BOOSTIO_INTERCEPTOR_NET_H
