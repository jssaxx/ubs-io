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

#ifndef PROXY_OPERATIONS_LOADER_H
#define PROXY_OPERATIONS_LOADER_H

#include <string>
#include <vector>

#include "interceptor.h"

#define CHECKPROXYLOADED (ock::interceptor::ProxyOperationsLoader::GetProxy() == nullptr)
#define CHECKPROXYFUNC(func) (ock::interceptor::ProxyOperationsLoader::GetProxy()->func == nullptr)
#define PROXY(func) ock::interceptor::ProxyOperationsLoader::GetProxy()->func

namespace ock {
namespace interceptor {
using ProxyOperations = struct InterceptorProxyOperations;
class ProxyOperationsLoader {
public:
    static ProxyOperationsLoader &GetInstance()
    {
        static ProxyOperationsLoader loader;
        return loader;
    }

    ProxyOperationsLoader() = default;

    ProxyOperationsLoader(const ProxyOperationsLoader &) = delete;

    ProxyOperationsLoader &operator=(const ProxyOperationsLoader &) = delete;

    ~ProxyOperationsLoader();

    bool Initialize();

    static ProxyOperations *const GetProxy();

private:
    bool LoadPreLoadPath();

    bool LoadProxyDLL();

    bool LoadProxyOperations();

    bool LoadProxyInitFunc();

    void LoadProxyExitFunc();

private:
    const std::string operationsFuncName = "RegisterHookFunction";
    const std::string operationsFuncsName = "RegisterHookFunctions";
    const std::string proxyInitFuncName = "InitializeProxyContext";
    const std::string proxyExitFuncName = "CleanProxyContext";
    std::string ldPrePath = "";
    const std::vector<std::string> components = {"iofwd", "adhocfs"};
    std::string workProxy = "";
    void *handle = nullptr;
    static ProxyOperations *operations;
};
} // namespace interceptor
} // namespace ock
#endif // PROXY_OPERATIONS_LOADER_H