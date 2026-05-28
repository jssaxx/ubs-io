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

#ifndef NATIVE_OPERATIONS_LOADER_H
#define NATIVE_OPERATIONS_LOADER_H

#include <string>
#include <vector>

#include "interceptor.h"

#define NATIVE(func) ock::interceptor::NativeOperationsLoader::GetProxy().func
#define CHECKNATIVEFUNC(func) (ock::interceptor::NativeOperationsLoader::GetProxy().func == nullptr)
namespace ock {
namespace interceptor {
using NativeOperations = struct InterceptorNativeOperations;
class NativeOperationsLoader {
public:
    static NativeOperationsLoader &GetInstance()
    {
        static NativeOperationsLoader loader;
        return loader;
    }

    NativeOperationsLoader() = default;

    NativeOperationsLoader(const NativeOperationsLoader &) = delete;

    NativeOperationsLoader &operator=(const NativeOperationsLoader &) = delete;

    ~NativeOperationsLoader() = default;

    bool Initialize();

    static NativeOperations &GetProxy();

private:
    template <typename T>
    void LoadProxy(const std::string &syscall, T &handle);

    void LoadControlProxy();

    void LoadFileIOProxy();

    void LoadMetaProxy();

    void LoadPathProxy();

    void LoadFileStreamProxy();

private:
    static NativeOperations operations;
};

bool InitNativeHook();
} // namespace interceptor
} // namespace ock

#endif // NATIVE_OPERATIONS_LOADER_H