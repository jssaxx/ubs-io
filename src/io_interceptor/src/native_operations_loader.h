/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
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
    static NativeOperationsLoader& GetInstance()
    {
        static NativeOperationsLoader loader;
        return loader;
    }

    NativeOperationsLoader() = default;

    NativeOperationsLoader(const NativeOperationsLoader&) = delete;

    NativeOperationsLoader& operator =(const NativeOperationsLoader&) = delete;

    ~NativeOperationsLoader() = default;

    bool Initialize();

    static NativeOperations& GetProxy();

private:
    template<typename T>
    void LoadProxy(const std::string& syscall, T& handle);

    void LoadControlProxy();

    void LoadFileIOProxy();

    void LoadMetaProxy();

    void LoadPathProxy();

    void LoadFileStreamProxy();

private:
    static NativeOperations operations;
};

bool InitNativeHook();
}
}

#endif // NATIVE_OPERATIONS_LOADER_H