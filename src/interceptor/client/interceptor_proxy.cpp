/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#include "proxy_operations.h"
#include "interceptor_context.h"

using namespace ock::bio;

extern "C" {
struct InterceptorProxyOperations *RegisterHookFunctions(const struct InterceptorNativeOperations *nativeOperations)
{
    if (nativeOperations == nullptr) {
        return nullptr;
    }
    BioInterceptorContext::GetInstance().SetNativeOperations(nativeOperations);
    return ProxyOperations::GetOperations();
}
}
