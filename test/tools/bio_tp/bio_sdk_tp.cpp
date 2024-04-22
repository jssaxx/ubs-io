/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include "bio_sdk_tp.h"

using namespace ock::bio;
#ifdef __aarch64__
static uint32_t MY_PID = 102;
void tp::SdkTp::Register() noexcept
{
    LVOS_TP_REG(SDK_AGENT_INIT_FAIL, "sdk agent init failed, rturn BIO_ERR", tp::CommonTp::IntValueCallback);
}

void tp::SdkTp::Deregister() noexcept
{
    LVOS_TP_UNREG(SDK_AGENT_INIT_FAIL);
}
#else
void tp::SdkTp::Register() noexcept {}

void tp::SdkTp::Deregister() noexcept {}
#endif
