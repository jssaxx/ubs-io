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

#include <cstdint>
#include <utility>

#include "bio_client_net.h"
#include "bio_err.h"

namespace ock {
namespace bio {
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

    bool CheckInterceptorLargeWriteReq(InterceptorLargePwriteIn *req);
    bool CheckInterceptorAllocPageReq(InterceptorAllocPageReq *req);
    bool CheckInterceptorWriteReq(InterceptorPwriteIn *req);
    bool CheckInterceptorReadReq(InterceptorPreadIn *req);

private:
    BResult RegisterOpcode();
};
} // namespace bio
} // namespace ock
#endif // BOOSTIO_INTERCEPTOR_SERVER_H
