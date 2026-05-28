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

#include "bio_server_tp.h"

using namespace ock::bio;
using namespace ock::bio::tp;

#ifdef __aarch64__
static uint32_t MY_PID = 102;

void tp::ServerTp::Register() noexcept
{
    BIO_TP_REG(SERVER_UNDERFS_PUT, "server underfs put error", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_UNDERFS_GET, "server underfs get error", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_UNDERFS_DELETE, "server underfs delete error", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_UNDERFS_STAT, "server underfs stat error", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_UNDERFS_STAT_SIZE, "server underfs stat invalid size", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_UNDERFS_LIST, "server underfs list error", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_FAIL_TO_CREATE_MEMORY_FILE, "failed to create memory file", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_FAIL_TO_TRUNCATE_FILE_WITH_SIZE, "truncate file with size failed",
               CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_FAIL_TO_MMAP_SHM_SIZE, "Mmap bio_shm size failed", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_CRB_SEND_FLUSH_FAIL, "connect fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_CONNECT_FAIL, "connect fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_ASYNC_CALL_FAIL, "async call fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_RDMA_WRITE_FAIL, "rdma write fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_RDMA_READ_FAIL, "rdma read fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVICE_START_FAIL, "server start fail", CommonTp::IntValueCallback);
    BIO_TP_REG(DESERIALIZE_SET_VSIZE, "set vsize", CommonTp::IntValueCallback);
    BIO_TP_REG(MIRRIR_SERVER_CHECK_FREE_MEM_REQ_PASS_CHECK, "pass check", CommonTp::NoProcessCallback);
    BIO_TP_REG(MIRROR_SERVER_TRACE_EXCEED_ARRAY_SIZE, "get trace fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_GET_TRACEDATA_ERR, "server get tace data err", CommonTp::PointerValueCallback);
    BIO_TP_REG(SERVER_BDM_UPDATE_SUCCESS, "server bdm update success", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_ADD_NEW_DISK_FAIL, "server add new disk fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_OLD_DISK_EXIST, "server old disk exist", CommonTp::BoolValueCallback);
    BIO_TP_REG(SERVER_NO_DISK_CHECK, "server no disk check", CommonTp::NoProcessCallback);
    BIO_TP_REG(SERVER_SET_OLD_DISK_ID, "server set old disk id", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_ADD_CHANNEL_FAIL, "server net add channel fail", CommonTp::PointerValueCallback);
}

void tp::ServerTp::Deregister() noexcept
{
    BIO_TP_UNREG(SERVER_UNDERFS_PUT);
    BIO_TP_UNREG(SERVER_UNDERFS_GET);
    BIO_TP_UNREG(SERVER_UNDERFS_DELETE);
    BIO_TP_UNREG(SERVER_UNDERFS_STAT);
    BIO_TP_UNREG(SERVER_UNDERFS_STAT_SIZE);
    BIO_TP_UNREG(SERVER_UNDERFS_LIST);
    BIO_TP_UNREG(SERVER_NET_FAIL_TO_CREATE_MEMORY_FILE);
    BIO_TP_UNREG(SERVER_NET_FAIL_TO_TRUNCATE_FILE_WITH_SIZE);
    BIO_TP_UNREG(SERVER_NET_FAIL_TO_MMAP_SHM_SIZE);
    BIO_TP_UNREG(SERVER_CRB_SEND_FLUSH_FAIL);
    BIO_TP_UNREG(SERVER_NET_CONNECT_FAIL);
    BIO_TP_UNREG(SERVER_NET_ASYNC_CALL_FAIL);
    BIO_TP_UNREG(SERVER_NET_RDMA_WRITE_FAIL);
    BIO_TP_UNREG(SERVER_NET_RDMA_READ_FAIL);
    BIO_TP_UNREG(SERVICE_START_FAIL);
    BIO_TP_UNREG(MIRRIR_SERVER_CHECK_FREE_MEM_REQ_PASS_CHECK);
    BIO_TP_UNREG(DESERIALIZE_SET_VSIZE);
    BIO_TP_UNREG(MIRROR_SERVER_TRACE_EXCEED_ARRAY_SIZE);
    BIO_TP_UNREG(SERVER_GET_TRACEDATA_ERR);
    BIO_TP_UNREG(SERVER_BDM_UPDATE_SUCCESS);
    BIO_TP_UNREG(SERVER_ADD_NEW_DISK_FAIL);
    BIO_TP_UNREG(SERVER_OLD_DISK_EXIST);
    BIO_TP_UNREG(SERVER_NO_DISK_CHECK);
    BIO_TP_UNREG(SERVER_SET_OLD_DISK_ID);
    BIO_TP_UNREG(SERVER_NET_ADD_CHANNEL_FAIL);
}
#else
void tp::ServerTp::Register() noexcept {}

void tp::ServerTp::Deregister() noexcept {}
#endif