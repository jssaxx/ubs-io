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

#include "bio_sdk_tp.h"

using namespace ock::bio;
using namespace ock::bio::tp;

#ifdef __aarch64__
static uint32_t MY_PID = 102;

void tp::SdkTp::Register() noexcept
{
    BIO_TP_REG(SDK_MIRROR_PT_VIEW_FIND_FAIL, "sdk mirror pt entry find failed.", CommonTp::BoolValueCallback);
    BIO_TP_REG(SDK_MIRROR_CHECK_PT_FAIL, "sdk mirror check pt failed, return BIO_CHECK_PT_FAIL", CommonTp::BoolValueCallback);
    BIO_TP_REG(SDK_BIO_AGENT_CREAT_FAIL, "sdk agent creat fail, return BIO_ALLOC_FAIL", CommonTp::PointerValueCallback);
    BIO_TP_REG(SDK_BIO_LOG_CREAT_FAIL, "sdk log creat fail, return -1", CommonTp::PointerValueCallback);
    BIO_TP_REG(SDK_BIO_LOG_INIT_FAIL, "sdk log init fail, return -1", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_BIO_AGENT_START_OP_FAIL, "sdk agent start op fail, return BIO_INNER_ERR", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_BIO_AGENT_LOAD_FUNC_FAIL, "sdk agent load func fail, return BIO_INNER_ERR", CommonTp::PointerValueCallback);
    BIO_TP_REG(SDK_BIO_MIRROR_SEND_SYNC_FAIL, "sdk mirror sendsync fail, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_BIO_NET_START_RPC_FAIL, "start net post rpc fail, return BIO_INNER_ERR", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_BIO_NET_START_CONNECT_FAIL, "net post connect fail, return BIO_INNER_ERR", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_PUT_RECV_FAIL, "sdk mirror recv put request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_GET_RECV_FAIL, "sdk mirror recv get request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_DELETE_RECV_FAIL, "sdk mirror recv delete request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_ADD_DISK_RECV_FAIL, "sdk mirror recv add disk request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_LOAD_RECV_FAIL, "sdk mirror recv load request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_STAT_RECV_FAIL, "sdk mirror recv stat request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_LIST_RECV_FAIL, "sdk mirror recv list request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL, "sdk mirror notify update request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_CHECK_UPDATE_RECV_FAIL, "sdk mirror check update request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_ASYNC_CALL_FAIL, "async call fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_RDMA_WRITE_FAIL, "rdma write fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SERVER_NET_RDMA_READ_FAIL, "rdma read fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_SET_PT_ID_FAIL, "sdk mirror ptId failed, return UINT16_MAX", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL, "sdk mirror alloc put offset fail, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_SELECT_PT_FAIL, "sdk select pt fail, return UINT16_MAX", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_CLIENT_SET_RETRY_TIME, "sdk mirror client set retry time zero", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_PREPARE_PUT_WITH_SPACE_FAIL, "prepare put with space fail", CommonTp::NoProcessCallback);
    BIO_TP_REG(SDK_MIRROR_CLIENT_PREPARE_FAIL, "prepare fail", CommonTp::NoProcessCallback);
    BIO_TP_REG(SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY, "not exsit local copy", CommonTp::NoProcessCallback);
    BIO_TP_REG(SDK_MIRROR_CLIENT_QUERY_FAIL, "mirror client query fail, retuen nullptr", CommonTp::NoProcessCallback);
    BIO_TP_REG(SDK_MIRROR_CLIENT_GET_RETRY, "sdk mirror client get return retry", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_CLIENT_GET_CEPH_STAT_OK, "sdk mirror client get ceph stat ok", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_CLIENT_GET_CEPH_STAT_SIZE, "sdk mirror client get ceph stat size unequit zero", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_CLIENT_ADDRNUM_INVALID, "sdk mirror client alloc space fail", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_RSP_NUM_ERROR, "set mirror rsp num error", CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_CLIENT_GET_UNDERFS_CONFIG_PASS_SYNC_CALL, "get underfs config pass sync call", CommonTp::NoProcessCallback);
    BIO_TP_REG(SDK_MIRROR_CLIENT_QUERY_CACHE_RESOURCE_SEND_FAIL, "mirror client query cache resource fail",
                CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_MIRROR_CLIENT_QUERY_CACHE_HIT_SEND_FAIL, "mirror client query cache hit fail",
                CommonTp::IntValueCallback);
    BIO_TP_REG(SDK_ADD_DISK_BY_SEPARATES, "sdk add disk by separates", CommonTp::NoProcessCallback);
    BIO_TP_REG(SDK_REQUEST_IPC_OPCODE_EXCEED, "sdk request IPC opcode exceed 256, return BIO_ERR", CommonTp::IntValueCallback);
}

void tp::SdkTp::Deregister() noexcept
{
    BIO_TP_UNREG(SDK_MIRROR_PT_VIEW_FIND_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_CHECK_PT_FAIL);
    BIO_TP_UNREG(SDK_BIO_AGENT_CREAT_FAIL);
    BIO_TP_UNREG(SDK_BIO_LOG_CREAT_FAIL);
    BIO_TP_UNREG(SDK_BIO_LOG_INIT_FAIL);
    BIO_TP_UNREG(SDK_BIO_AGENT_START_OP_FAIL);
    BIO_TP_UNREG(SDK_BIO_AGENT_LOAD_FUNC_FAIL);
    BIO_TP_UNREG(SDK_BIO_MIRROR_SEND_SYNC_FAIL);
    BIO_TP_UNREG(SDK_BIO_NET_START_RPC_FAIL);
    BIO_TP_UNREG(SDK_BIO_NET_START_CONNECT_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_PUT_RECV_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_GET_RECV_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_DELETE_RECV_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_ADD_DISK_RECV_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_LOAD_RECV_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_STAT_RECV_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_LIST_RECV_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_CHECK_UPDATE_RECV_FAIL);
    BIO_TP_UNREG(SERVER_NET_ASYNC_CALL_FAIL);
    BIO_TP_UNREG(SERVER_NET_RDMA_WRITE_FAIL);
    BIO_TP_UNREG(SERVER_NET_RDMA_READ_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_SET_PT_ID_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_PREPARE_PUT_WITH_SPACE_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_CLIENT_QUERY_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_SELECT_PT_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_CLIENT_SET_RETRY_TIME);
    BIO_TP_UNREG(SDK_MIRROR_CLIENT_GET_RETRY);
    BIO_TP_UNREG(SDK_CLIENT_GET_CEPH_STAT_OK);
    BIO_TP_UNREG(SDK_CLIENT_GET_CEPH_STAT_SIZE);
    BIO_TP_UNREG(SDK_MIRROR_CLIENT_ADDRNUM_INVALID);
    BIO_TP_UNREG(SDK_MIRROR_CLIENT_PREPARE_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY);
    BIO_TP_UNREG(SDK_MIRROR_RSP_NUM_ERROR);
    BIO_TP_UNREG(SDK_CLIENT_GET_UNDERFS_CONFIG_PASS_SYNC_CALL);
    BIO_TP_UNREG(SDK_MIRROR_CLIENT_QUERY_CACHE_RESOURCE_SEND_FAIL);
    BIO_TP_UNREG(SDK_MIRROR_CLIENT_QUERY_CACHE_HIT_SEND_FAIL);
    BIO_TP_UNREG(SDK_ADD_DISK_BY_SEPARATES);
    BIO_TP_UNREG(SDK_REQUEST_IPC_OPCODE_EXCEED);
}
#else
void tp::SdkTp::Register() noexcept {}

void tp::SdkTp::Deregister() noexcept {}
#endif
