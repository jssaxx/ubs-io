/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include "bio_sdk_tp.h"

using namespace ock::bio;
#ifdef __aarch64__
static uint32_t MY_PID = 102;
void tp::SdkTp::Register() noexcept
{
}

void tp::SdkTp::Deregister() noexcept
{
}
#else
void tp::SdkTp::Register() noexcept
{
}

void tp::SdkTp::Deregister() noexcept
{
}
#endif
