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

#ifndef BOOST_IO_INTERCEPTOR_CONTEXT_H
#define BOOST_IO_INTERCEPTOR_CONTEXT_H

#include "interceptor.h"
#include "interceptor_fd.h"
#include "proxy_operations.h"

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
