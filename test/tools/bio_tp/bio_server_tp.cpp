/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include "bio_server_tp.h"

using namespace ock::bio;
#ifdef __aarch64__
static uint32_t MY_PID = 102;
void tp::ServerTp::Register() noexcept
{
    LVOS_TP_REG(SERVER_UNDERFS_PUT, "server underfs put error", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_UNDERFS_GET, "server underfs get error", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_UNDERFS_DELETE, "server underfs delete error", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_UNDERFS_STAT, "server underfs stat error", tp::CommonTp::IntValueCallback);
}

void tp::ServerTp::Deregister() noexcept
{
    LVOS_TP_UNREG(SERVER_UNDERFS_PUT);
    LVOS_TP_UNREG(SERVER_UNDERFS_GET);
    LVOS_TP_UNREG(SERVER_UNDERFS_DELETE);
    LVOS_TP_UNREG(SERVER_UNDERFS_STAT);
}
#else
void tp::ServerTp::Register() noexcept
{
}

void tp::ServerTp::Deregister() noexcept
{
}
#endif
