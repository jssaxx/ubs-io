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
    LVOS_TP_REG(SERVER_NET_FAIL_TO_CONNECT_CTRL_PLANE, "Failed to connect ctrl plane to peer target node id", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_GET_CHANNEL_BY_ID_FAILED, "Failed to get channel by dstNid", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_PEER_CONNECTION_REFUSED, "peer connection has been refused", tp::CommonTp::IntValueCallback);
}

void tp::ServerTp::Deregister() noexcept
{
    LVOS_TP_UNREG(SERVER_UNDERFS_PUT);
    LVOS_TP_UNREG(SERVER_UNDERFS_GET);
    LVOS_TP_UNREG(SERVER_UNDERFS_DELETE);
    LVOS_TP_UNREG(SERVER_UNDERFS_STAT);
    LVOS_TP_UNREG(SERVER_NET_FAIL_TO_CONNECT_CTRL_PLANE);
    LVOS_TP_UNREG(SERVER_NET_GET_CHANNEL_BY_ID_FAILED);
    LVOS_TP_UNREG(SERVER_NET_PEER_CONNECTION_REFUSED);
}
#else
void tp::ServerTp::Register() noexcept {}

void tp::ServerTp::Deregister() noexcept {}
#endif
