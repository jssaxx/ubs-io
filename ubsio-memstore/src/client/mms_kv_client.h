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

#ifndef MMS_KV_CLIENT_H
#define MMS_KV_CLIENT_H

#include <semaphore.h>
#include <atomic>
#include "net_engine.h"
#include "mms_ref.h"
#include "mms_lock.h"
#include "mms_message.h"
#include "mms_cache.h"
#include "mms_mem_mgr.h"
#include "mms_mem_allocator.h"
#include "mms.h"

namespace ock {
namespace mms {

struct KvClientPara {
    CachePtr cache;
    NetEnginePtr netEngine;
    MmsMemMgrPtr memMgr;
    MmsMemAllocatorPtr memAllocator;
    uint32_t ioTimeOut;
    uint32_t maxMsgBuffSize;
};

class MmsKvClient;
using MmsKvClientPtr = Ref<MmsKvClient>;
class MmsKvClient {
public:
    BResult Initialize(const KvClientPara &para);
    BResult Start(void);
    void Exit(void);

    MmsKvClient() = default;

    BResult SendSingleReq(IoCtrlRequest &req);

    BResult HandleSendReqs(uint16_t numaId, uint64_t userId, MmsOpCode opCode, std::vector<IOCtxItem> &ctxItems);

    BResult MmsPut(uint64_t userId, PutItems *itemList, uint32_t itemNum);

    BResult MmsGet(uint64_t userId, GetItems *itemList, uint32_t itemNum);

    BResult MmsUpdate(uint64_t userId, UpdateItems *itemList, uint32_t itemNum);

    BResult MmsDelete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum);

    BResult MmsReplace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult FailHandle(BResult lastRet, MmsOpCode opCode, IoCtrlRequest &req, BResult &rsp);
    void FreeBlocks(std::vector<IOCtxItem> &ctxItems);
    void HandleUpdatePtVersion(uint64_t ptVersion);
    BResult UpdateClientPtVersion();
private:
    uint32_t mIoTimeOut = NO_60;
    std::atomic<uint64_t> mPtVersion {NO_1};
    uint32_t mMaxMsgBuffSize;
    bool mEnableCrc { false };

    CachePtr mCache{ nullptr };
    NetEnginePtr mNetEngine{ nullptr };
    MmsMemMgrPtr mMemMgr{ nullptr };
    MmsMemAllocatorPtr mMemAllocator{ nullptr };
    AllocFunc allocFunc = nullptr;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
