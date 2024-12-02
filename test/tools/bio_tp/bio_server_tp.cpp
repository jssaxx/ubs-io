/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include "bio_server_tp.h"

using namespace ock::bio;
using namespace ock::bio::tp;

#ifdef __aarch64__
static uint32_t MY_PID = 102;

void tp::ServerTp::Register() noexcept
{
    LVOS_TP_REG(SERVER_UNDERFS_PUT, "server underfs put error", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_UNDERFS_GET, "server underfs get error", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_UNDERFS_DELETE, "server underfs delete error", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_UNDERFS_STAT, "server underfs stat error", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_UNDERFS_LIST, "server underfs list error", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_FAIL_TO_CREATE_MEMORY_FILE, "failed to create memory file", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_FAIL_TO_TRUNCATE_FILE_WITH_SIZE, "truncate file with size failed", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_FAIL_TO_MMAP_SHM_SIZE, "Mmap bio_shm size failed", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_CRB_SEND_FLUSH_FAIL, "connect fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_CONNECT_FAIL, "connect fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_ASYNC_CALL_FAIL, "async call fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_RDMA_WRITE_FAIL, "rdma write fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_RDMA_READ_FAIL, "rdma read fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVICE_START_FAIL, "server start fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(DESERIALIZE_SET_VSIZE, "set vsize", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NO_PROCESS_SHM_INIT_SKIP, "server handle shm init skip", CommonTp::NoProcessCallback);
    LVOS_TP_REG(MIRRIR_SERVER_CHECK_FREE_MEM_REQ_PASS_CHECK, "pass check", CommonTp::NoProcessCallback);
    LVOS_TP_REG(MIRROR_SERVER_PUT_PASS_MESSAGE_CHECK, "pass check", CommonTp::NoProcessCallback);
}

void tp::ServerTp::Deregister() noexcept
{
    LVOS_TP_UNREG(SERVER_UNDERFS_PUT);
    LVOS_TP_UNREG(SERVER_UNDERFS_GET);
    LVOS_TP_UNREG(SERVER_UNDERFS_DELETE);
    LVOS_TP_UNREG(SERVER_UNDERFS_STAT);
    LVOS_TP_UNREG(SERVER_UNDERFS_LIST);
    LVOS_TP_UNREG(SERVER_NET_FAIL_TO_CREATE_MEMORY_FILE);
    LVOS_TP_UNREG(SERVER_NET_FAIL_TO_TRUNCATE_FILE_WITH_SIZE);
    LVOS_TP_UNREG(SERVER_NET_FAIL_TO_MMAP_SHM_SIZE);
    LVOS_TP_UNREG(SERVER_CRB_SEND_FLUSH_FAIL);
    LVOS_TP_UNREG(SERVER_NET_CONNECT_FAIL);
    LVOS_TP_UNREG(SERVER_NET_ASYNC_CALL_FAIL);
    LVOS_TP_UNREG(SERVER_NET_RDMA_WRITE_FAIL);
    LVOS_TP_UNREG(SERVER_NET_RDMA_READ_FAIL);
    LVOS_TP_UNREG(SERVICE_START_FAIL);
    LVOS_TP_UNREG(SERVER_NO_PROCESS_SHM_INIT_SKIP);
    LVOS_TP_UNREG(MIRRIR_SERVER_CHECK_FREE_MEM_REQ_PASS_CHECK);
    LVOS_TP_UNREG(MIRROR_SERVER_PUT_PASS_MESSAGE_CHECK);
    LVOS_TP_UNREG(DESERIALIZE_SET_VSIZE);
}
#else
void tp::ServerTp::Register() noexcept {}

void tp::ServerTp::Deregister() noexcept {}
#endif
