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
    LVOS_TP_REG(SERVER_NET_FAIL_TO_CONNECT_CTRL_PLANE, "Failed to connect ctrl plane to peer target node id", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SERVER_NET_FAIL_TO_REGISTER_BY_SIZE, "failed to register by size", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SERVER_NET_FAIL_TO_CREATE_MEMORY_FILE, "failed to create memory file", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_FAIL_TO_TRUNCATE_FILE_WITH_SIZE, "truncate file with size failed", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_FAIL_TO_MMAP_SHM_SIZE, "Mmap bio_shm size failed", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_GET_CHANNEL_NOT_EXIST, "get channel not exist", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_FAILED_ASYNC_CALL_WITH_OP, "failed async call with op", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_GET_DATA_CHANNEL_NOT_EXIST, "get data channel not exist", tp::CommonTp::IntValueCallback);
}

void tp::ServerTp::Deregister() noexcept
{
    LVOS_TP_UNREG(SERVER_UNDERFS_PUT);
    LVOS_TP_UNREG(SERVER_UNDERFS_GET);
    LVOS_TP_UNREG(SERVER_UNDERFS_DELETE);
    LVOS_TP_UNREG(SERVER_UNDERFS_STAT);
    LVOS_TP_UNREG(SERVER_NET_FAIL_TO_CONNECT_CTRL_PLANE);
    LVOS_TP_UNREG(SERVER_NET_FAIL_TO_REGISTER_BY_SIZE);
    LVOS_TP_UNREG(SERVER_NET_FAIL_TO_CREATE_MEMORY_FILE);
    LVOS_TP_UNREG(SERVER_NET_FAIL_TO_TRUNCATE_FILE_WITH_SIZE);
    LVOS_TP_UNREG(SERVER_NET_FAIL_TO_MMAP_SHM_SIZE);
    LVOS_TP_UNREG(SERVER_NET_GET_CHANNEL_NOT_EXIST);
    LVOS_TP_UNREG(SERVER_NET_FAILED_ASYNC_CALL_WITH_OP);
    LVOS_TP_UNREG(SERVER_NET_GET_DATA_CHANNEL_NOT_EXIST);
}
#else
void tp::ServerTp::Register() noexcept {}

void tp::ServerTp::Deregister() noexcept {}
#endif
