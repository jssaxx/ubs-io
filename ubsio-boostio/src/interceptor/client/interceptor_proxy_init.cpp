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

    CLOG_INFO("Initialize interceptor proxy context success.");
    return 0;
}

void CleanProxyContext() {}
