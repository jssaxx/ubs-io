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
#include <cstdint>

#include "bio_err.h"
#include "bio_client_net.h"

namespace ock {
namespace bio {

struct DataMsgMemItem {
    int32_t shmFd;
    uint64_t offset;
    uint64_t size;
    uint8_t *address;

    DataMsgMemItem() : shmFd(-1), offset(0), size(0), address(nullptr) {}

    DataMsgMemItem(int32_t fd, uint64_t off, uint64_t sz, uint8_t *addr)
        : shmFd(fd), offset(off), size(sz), address(addr) {}
};

class InterceptorServer {
public:
    static InterceptorServer &GetInstance()
    {
        static InterceptorServer instance;
        return instance;
    }

    BResult Initialize();

    BResult HandleInterceptorRead(ServiceContext &ctx);
    BResult HandleInterceptorWrite(ServiceContext &ctx);
    BResult HandleInterceptorAllocPage(ServiceContext &ctx);
    BResult HandleInterceptorLargeWrite(ServiceContext &ctx);
    BResult HandleInterceptorLargeRead(ServiceContext &ctx);
    BResult HandleInterceptorCreateDataMsgMemPool(ServiceContext &ctx);

    bool CheckInterceptorLargeWriteReq(InterceptorLargePwriteIn *req);
    bool CheckInterceptorLargeReadReq(InterceptorLargePreadIn *req);
    bool CheckInterceptorAllocPageReq(InterceptorAllocPageReq *req);
    bool CheckInterceptorWriteReq(InterceptorPwriteIn *req);
    bool CheckInterceptorReadReq(InterceptorPreadIn *req);

    uint8_t *TransDataMsgMemAddr(uint32_t pid, uint64_t mrOffset);

private:
    BResult RegisterOpcode();

    std::mutex mDataMsgMemLock;
    std::unordered_map<uint32_t, DataMsgMemItem> mDataMsgMemMgr;
};
}
}
#endif // BOOSTIO_INTERCEPTOR_SERVER_H
