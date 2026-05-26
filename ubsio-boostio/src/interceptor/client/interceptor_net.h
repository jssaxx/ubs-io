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

#include "net_engine.h"
#include "bio_ref.h"
#include "net_common.h"
#include "bio_err.h"

namespace ock {
namespace bio {
class InterceptorClientNetService {
public:
    static InterceptorClientNetService &Instance()
    {
        static InterceptorClientNetService instance;
        return instance;
    }

    int32_t StartNetService();
    BResult CorrectFd();
    BResult CheckShmFd();
    BResult ShmInitInner();
    BResult ShmInit();

    uint8_t *GetShmAddress(uint64_t offset, uint32_t len)
    {
        return mNetEngine->GetShmAddress(offset, len);
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
    std::atomic<bool> mReady = {false};
    NetEnginePtr mNetEngine = nullptr;
    int32_t mShmFd = -1;
    uint64_t mShmOffset = 0;
    uint64_t mShmLength = 0;
    uint8_t *mShmAddr = nullptr;
};
} // namespace bio
} // namespace ock
#endif // BOOSTIO_INTERCEPTOR_NET_H
