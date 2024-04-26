/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include "bio_tracepoint.h"
#include "bio_sdk_tp.h"
#include "bio_server_tp.h"
#include "bio_cache_tp.h"

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