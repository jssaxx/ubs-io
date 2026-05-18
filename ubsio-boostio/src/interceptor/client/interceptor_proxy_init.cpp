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

#include <cstdlib>
#include <cstring>
#include <climits>
#include <string>
#include <atomic>
#include <unistd.h>
#include "interceptor.h"
#include "interceptor_context.h"
#include "interceptor_log.h"
#include "interceptor_net.h"
#include "proxy_operations.h"

using namespace ock::bio;

static std::atomic<bool> g_initialized{ false };

namespace {
bool NormalizeMountPoint(const char *mountPoint, std::string &normalizedPath)
{
    if (mountPoint == nullptr || std::strlen(mountPoint) == 0 || mountPoint[0] != '/') {
        return false;
    }

    char realPath[PATH_MAX] = { 0 };
    if (realpath(mountPoint, realPath) == nullptr) {
        return false;
    }

    normalizedPath = realPath;
    while (normalizedPath.size() > 1 && normalizedPath.back() == '/') {
        normalizedPath.pop_back();
    }
    return true;
}
}

int InitializeProxyContext()
{
    if (g_initialized.load()) {
        return 0;
    }

    auto &ctx = BioInterceptorContext::GetInstance();
    const char *mountPoint = std::getenv("INTERCEPTOR_MOUNT_POINT");
    std::string normalizedMountPoint;
    if (NormalizeMountPoint(mountPoint, normalizedMountPoint)) {
        ctx.SetMountPoint(normalizedMountPoint);
        CLOG_INFO("Apply INTERCEPTOR_MOUNT_POINT success, value:" << ctx.mountPoint << ".");
    } else if (mountPoint != nullptr && std::strlen(mountPoint) != 0) {
        CLOG_WARN("Ignore invalid INTERCEPTOR_MOUNT_POINT:" << mountPoint << ".");
    }

    auto &netService = InterceptorClientNetService::Instance();
    if (netService.InitNetForCurrentProcess() == BIO_OK) {
        (void)netService.EnsureBioShmForCurrentProcess();
    }
    g_initialized.store(true);

    CLOG_INFO("Initialize interceptor proxy context success, mountPoint:" << ctx.mountPoint << ".");
    return 0;
}

void CleanProxyContext()
{
    if (!g_initialized.load()) {
        return;
    }

    ProxyOperations::FlushAllPendingWriteWindows();
    ProxyOperations::FlushPendingReadBufferReleaseForCurrentThread();
    InterceptorClientNetService::Instance().ShutdownNetService();
    g_initialized.store(false);
}
