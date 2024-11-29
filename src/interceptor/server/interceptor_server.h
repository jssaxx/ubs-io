/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#ifndef BOOSTIO_INTERCEPTOR_SERVER_H
#define BOOSTIO_INTERCEPTOR_SERVER_H

#include <utility>
#include <cstdint>

#include "bio_err.h"
#include "bio_client_net.h"

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

    bool CheckInterceptorLargeWriteReq(InterceptorLargePwriteIn **req);
    bool CheckInterceptorAllocPageReq(InterceptorAllocPageReq **req);
    bool CheckInterceptorWriteReq(InterceptorPwriteIn **req);
    bool CheckInterceptorReadReq(InterceptorPreadIn **req);

private:
    BResult RegisterOpcode();
};
}
}
#endif // BOOSTIO_INTERCEPTOR_SERVER_H
