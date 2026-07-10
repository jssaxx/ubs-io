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

#include <cerrno>
#include "bio_tp_common.h"

using namespace ock::bio;

void tp::CommonTp::Register() noexcept {}

void tp::CommonTp::Deregister() noexcept {}

int *g_int_bak;
void tp::CommonTp::IntValueCallback(BioTracepointParam *user, int *value, int set) noexcept
{
    g_int_bak = value;
    *value = set;
}

void tp::CommonTp::IntValueResetCallback(BioTracepointParam *user, int *value) noexcept
{
    *value = *g_int_bak;
}

void tp::CommonTp::IntValueErrnoCallback(BioTracepointParam *user, int *value, int set, int err) noexcept
{
    *value = set;
    errno = err;
}

void tp::CommonTp::LongValueCallback(BioTracepointParam *user, long *value, long set) noexcept
{
    *value = set;
}

void tp::CommonTp::LongValueErrnoCallback(BioTracepointParam *user, long *value, long set, int err) noexcept
{
    *value = set;
    errno = err;
}

void tp::CommonTp::BoolValueCallback(BioTracepointParam *user, bool *value, bool set) noexcept
{
    *value = set;
}

void tp::CommonTp::NoProcessCallback(BioTracepointParam *user) noexcept {}

void *g_bak = nullptr;
void tp::CommonTp::PointerValueCallback(BioTracepointParam *user, void** value, void* set) noexcept
{
    g_bak = std::move(*value);
    *value = set;
}

void tp::CommonTp::PointerValueResetCallback(BioTracepointParam *user, void** value) noexcept
{
    *value = g_bak;
}