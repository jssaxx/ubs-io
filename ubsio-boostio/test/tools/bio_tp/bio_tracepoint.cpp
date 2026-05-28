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

#include "bio_tracepoint.h"
#include "bio_cache_tp.h"
#include "bio_sdk_tp.h"
#include "bio_server_tp.h"

using namespace ock::bio;
#ifdef __aarch64__
int tp::TracePointManager::Initialize() noexcept
{
    TracePointInit();
    return RegisterAllPoints();
}

void tp::TracePointManager::Destroy() noexcept
{
    RemoveAllPoints();
    TracePointExit();
}

int tp::TracePointManager::RegisterAllPoints() noexcept
{
    tp::SdkTp::Register();
    tp::ServerTp::Register();
    tp::CacheTp::Register();
    return 0;
}

void tp::TracePointManager::RemoveAllPoints() noexcept
{
    tp::SdkTp::Deregister();
    tp::ServerTp::Deregister();
    tp::CacheTp::Deregister();
}
#else
int tp::TracePointManager::Initialize() noexcept
{
    return 0;
}

void tp::TracePointManager::Destroy() noexcept
{
    return;
}

int tp::TracePointManager::RegisterAllPoints() noexcept
{
    return 0;
}

void tp::TracePointManager::RemoveAllPoints() noexcept
{
    return;
}
#endif