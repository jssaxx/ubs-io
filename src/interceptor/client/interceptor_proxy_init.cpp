/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#include <cstdio>
#include "interceptor.h"
#include "interceptor_log.h"
#include "interceptor_net.h"

using namespace ock::bio;

static std::atomic<bool> g_initialized{ false };

int InitializeProxyContext()
{
    if (g_initialized.load()) {
        return 0;
    }

    g_initialized.store(true);

    CLOG_DEBUG("Init bio interceptor ok.");
    return 0;
}

void CleanProxyContext() {}