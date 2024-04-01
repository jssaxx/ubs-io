/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#ifndef BOOST_IO_INTERCEPTOR_CONTEXT_H
#define BOOST_IO_INTERCEPTOR_CONTEXT_H

#include "interceptor.h"
#include "proxy_operations.h"
#include "interceptor_fd.h"

namespace ock {
namespace bio {
struct BioInterceptorContext {
    const InterceptorNativeOperations *nativeOperations;
    std::string mountPoint = "/bfs";
    OpenFileMap files;
    static BioInterceptorContext &GetInstance()
    {
        static BioInterceptorContext instance;
        return instance;
    }

    BioInterceptorContext() = default;

    void SetNativeOperations(const InterceptorNativeOperations *native)
    {
        nativeOperations = native;
    }

    const InterceptorNativeOperations *GetOperations()
    {
        return nativeOperations;
    }
};
}
}


#endif // BOOST_IO_INTERCEPTOR_CONTEXT_H
