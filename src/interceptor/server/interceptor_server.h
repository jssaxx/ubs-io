/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#ifndef BOOSTIO_INTERCEPTOR_SERVER_H
#define BOOSTIO_INTERCEPTOR_SERVER_H

#include <utility>
#include <cstdint>
#include "net_engine.h"
#include "net_common.h"

namespace ock {
namespace bio {
class InterceptorServer {
public:
    static InterceptorServer &GetInstance()
    {
        static InterceptorServer instance;
        return instance;
    }

    int32_t StartServer();

private:
    void RegisterOpcode();
    int32_t HandleInterceptorRead(ServiceContext &ctx);
    int32_t HandleInterceptorWrite(ServiceContext &ctx);
    int32_t HandleInterceptorAllocPage(ServiceContext &ctx);
    int32_t HandleInterceptorLargeWrite(ServiceContext &ctx);
};
}
}

#endif // BOOSTIO_INTERCEPTOR_SERVER_H
