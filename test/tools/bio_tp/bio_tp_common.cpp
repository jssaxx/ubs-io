/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include <cerrno>
#include "bio_tp_common.h"

using namespace ock::bio;

void tp::CommonTp::Register() noexcept
{
}

void tp::CommonTp::Deregister() noexcept
{
}

void tp::CommonTp::IntValueCallback(LVOS_TRACEP_PARAM_S *user, int *value, int set) noexcept
{
    *value = set;
}

void tp::CommonTp::IntValueErrnoCallback(LVOS_TRACEP_PARAM_S *user, int *value, int set, int err) noexcept
{
    *value = set;
    errno = err;
}

void tp::CommonTp::LongValueCallback(LVOS_TRACEP_PARAM_S *user, long *value, long set) noexcept
{
    *value = set;
}

void tp::CommonTp::LongValueErrnoCallback(LVOS_TRACEP_PARAM_S *user, long *value, long set, int err) noexcept
{
    *value = set;
    errno = err;
}

void tp::CommonTp::NoProcessCallback(LVOS_TRACEP_PARAM_S *user) noexcept
{
}