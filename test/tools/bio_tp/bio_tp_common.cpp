/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include <cerrno>
#include "bio_tp_common.h"

using namespace ock::bio;

void tp::CommonTp::Register() noexcept {}

void tp::CommonTp::Deregister() noexcept {}

int *g_int_bak;
void tp::CommonTp::IntValueCallback(LVOS_TRACEP_PARAM_S *user, int *value, int set) noexcept
{
    g_int_bak = value;
    *value = set;
}

void tp::CommonTp::IntValueResetCallback(LVOS_TRACEP_PARAM_S *user, int *value) noexcept
{
    *value = *g_int_bak;
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

void tp::CommonTp::LongIntValueCallback(LVOS_TRACEP_PARAM_S *user, int64_t *value, int64_t set) noexcept
{
    *value = set;
}

void tp::CommonTp::BoolValueCallback(LVOS_TRACEP_PARAM_S *user, bool *value, bool set) noexcept
{
    *value = set;
}

void tp::CommonTp::NoProcessCallback(LVOS_TRACEP_PARAM_S *user) noexcept {}

void *g_bak = nullptr;
void tp::CommonTp::PointerValueCallback(LVOS_TRACEP_PARAM_S *user, void** value, void* set) noexcept
{
    g_bak = std::move(*value);
    *value = set;
}

void tp::CommonTp::PointerValueResetCallback(LVOS_TRACEP_PARAM_S *user, void** value) noexcept
{
    *value = g_bak;
}