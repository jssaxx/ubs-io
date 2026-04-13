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

#include "proxy_operations_loader.h"

#include <dlfcn.h>
#include <climits>
#include <vector>
#include <sstream>

#include "ceptor_log.h"
#include "ceptor_env.h"
#include "native_operations_loader.h"

using namespace ock::interceptor;

using GetProxyOperationsFunc = ProxyOperations* (*)();
using GetProxyOperationsFuncs = ProxyOperations* (*)(const NativeOperations *);
using ProxyInitFunc = int (*)();
using ProxyExitFunc = void (*)();

ProxyOperations* ProxyOperationsLoader::operations = nullptr;

static bool CanonicalPath(std::string &path)
{
    if (path.empty()) {
        return false;
    }

    char *tmpPath = realpath(path.c_str(), nullptr);
    if (tmpPath == nullptr) {
        return false;
    }
    path = tmpPath;
    free(tmpPath);
    return true;
}


bool ProxyOperationsLoader::Initialize()
{
    static bool initialized = false;
    if (initialized) {
        INTERCEPTORLOG_DEBUG("Proxy has been loaded");
        return true;
    }

    if (!LoadPreLoadPath() ||
        !LoadProxyDLL() ||
        !LoadProxyInitFunc() ||
        !LoadProxyOperations()) {
        initialized = true;
        return false;
    }
    initialized = true;
    return true;
}

ProxyOperationsLoader::~ProxyOperationsLoader()
{
    LoadProxyExitFunc();
}

ProxyOperations* const ProxyOperationsLoader::GetProxy()
{
    return operations;
}

bool ProxyOperationsLoader::LoadPreLoadPath()
{
    std::string tmpPath = env::GetEnv("LD_PRELOAD", "");
    if (tmpPath.empty()) {
        return false;
    }

    std::istringstream ss(tmpPath);
    std::string dllPath;
    std::string iceptorPath;
    bool onlyOne = true;
    while (std::getline(ss, dllPath, ':') && !dllPath.empty()) {
        if (dllPath.find("libock_interceptor.so") != std::string::npos) {
            if (iceptorPath.empty()) {
                iceptorPath.swap(dllPath);
            } else {
                onlyOne = false;
            }
        }
    }
    if (iceptorPath.empty() || !onlyOne) {
        return false;
    }

    std::string::size_type charPos = iceptorPath.rfind("libock_interceptor.so");
    if (charPos == 0) {
        return false;
    } else {
        char resolvedPath[PATH_MAX];
        if (realpath(iceptorPath.substr(0, charPos).c_str(), resolvedPath) == nullptr) {
            return false;
        }
        ldPrePath = resolvedPath;
    }
    return true;
}

bool ProxyOperationsLoader::LoadProxyDLL()
{
    for (auto& item : components) {
        std::string proxyName = std::string("/usr/lib64/libock_").append(item).append("_proxy.so");
        std::string prefixPath = ldPrePath;
        std::string proxyPath = prefixPath.append(proxyName);
        if (!CanonicalPath(proxyPath)) {
            continue;
        }
        handle = dlopen(proxyPath.c_str(), RTLD_NOW);
        if (handle == nullptr) {
            INTERCEPTORLOG_DEBUG("failed to dlopen %s, error(%s)",
                proxyPath.c_str(), dlerror());
            continue;
        } else {
            workProxy.swap(proxyName);
            return true;
        }
    }
    INTERCEPTORLOG_DEBUG("There is no proxy that working");
    return false;
}

bool ProxyOperationsLoader::LoadProxyOperations()
{
    GetProxyOperationsFuncs getOperationsFuncs =
        reinterpret_cast<GetProxyOperationsFuncs>(dlsym(handle, operationsFuncsName.c_str()));
    if (getOperationsFuncs == nullptr) {
        INTERCEPTORLOG_DEBUG("%s does not has symbol %s, error(%s)",
            workProxy.c_str(), operationsFuncsName.c_str(), dlerror());
        dlclose(handle);
        return false;
    }
    operations = getOperationsFuncs(&(NativeOperationsLoader::GetInstance().GetProxy()));
    if (operations == nullptr) {
        INTERCEPTORLOG_WARN("Getting null operations from  %s",
            operationsFuncsName.c_str(), workProxy.c_str());
        dlclose(handle);
        return false;
    }
    return true;
}

bool ProxyOperationsLoader::LoadProxyInitFunc()
{
    ProxyInitFunc proxyInitFunc =
        reinterpret_cast<ProxyInitFunc>(dlsym(handle, proxyInitFuncName.c_str()));
    if (proxyInitFunc == nullptr) {
        INTERCEPTORLOG_DEBUG("Symbol(%s) of %s is ambiguous, error(%s)", proxyInitFuncName.c_str(),
            workProxy.c_str(), dlerror());
        return true;
    }
    int ret = proxyInitFunc();
    if (ret != 0) {
        INTERCEPTORLOG_WARN("Failed to Init %s, error(%d)", workProxy.c_str(),
            proxyInitFuncName.c_str(), ret);
        dlclose(handle);
        return false;
    }
    return true;
}

void ProxyOperationsLoader::LoadProxyExitFunc()
{
    if (handle == nullptr) {
        return;
    }
    ProxyExitFunc proxyExitFunc =
        reinterpret_cast<ProxyExitFunc>(dlsym(handle, proxyExitFuncName.c_str()));
    if (proxyExitFunc == nullptr) {
        INTERCEPTORLOG_DEBUG("Symbol(%s) of %s is ambiguous, error(%s)", proxyExitFuncName.c_str(),
            workProxy.c_str(), dlerror());
        operations = nullptr;
        dlclose(handle);
        return;
    }
    proxyExitFunc();
    dlclose(handle);
    operations = nullptr;
}
