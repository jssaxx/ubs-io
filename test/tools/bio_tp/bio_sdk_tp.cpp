/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include "bio_sdk_tp.h"

using namespace ock::bio;
using namespace ock::bio::tp;

#ifdef __aarch64__
static uint32_t MY_PID = 102;

void tp::SdkTp::Register() noexcept
{
    LVOS_TP_REG(SDK_MIRROR_PT_VIEW_FIND_FAIL, "sdk mirror pt entry find failed.", CommonTp::BoolValueCallback);
    LVOS_TP_REG(SDK_MIRROR_CHECK_PT_FAIL, "sdk mirror check pt failed, return BIO_CHECK_PT_FAIL", CommonTp::BoolValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_CREAT_FAIL, "sdk agent creat fail, return BIO_ALLOC_FAIL", CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_LOG_CREAT_FAIL, "sdk log creat fail, return -1", CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_LOG_INIT_FAIL, "sdk log init fail, return -1", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_START_OP_FAIL, "sdk agent start op fail, return BIO_INNER_ERR", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_LOAD_FUNC_FAIL, "sdk agent load func fail, return BIO_INNER_ERR", CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_MIRROR_SEND_SYNC_FAIL, "sdk mirror sendsync fail, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_NET_START_RPC_FAIL, "start net post rpc fail, return BIO_INNER_ERR", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_NET_START_CONNECT_FAIL, "net post connect fail, return BIO_INNER_ERR", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_PUT_RECV_FAIL, "sdk mirror recv put request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_GET_RECV_FAIL, "sdk mirror recv get request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_DELETE_RECV_FAIL, "sdk mirror recv delete request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_ADD_DISK_RECV_FAIL, "sdk mirror recv add disk request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_LOAD_RECV_FAIL, "sdk mirror recv load request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_STAT_RECV_FAIL, "sdk mirror recv stat request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_LIST_RECV_FAIL, "sdk mirror recv list request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL, "sdk mirror notify update request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_CHECK_UPDATE_RECV_FAIL, "sdk mirror check update request failed, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_ASYNC_CALL_FAIL, "async call fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_RDMA_WRITE_FAIL, "rdma write fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_RDMA_READ_FAIL, "rdma read fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_SET_PT_ID_FAIL, "sdk mirror ptId failed, return UINT16_MAX", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL, "sdk mirror alloc put offset fail, return BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_SELECT_PT_FAIL, "sdk select pt fail, return UINT16_MAX", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_SET_RETRY_TIME, "sdk mirror client set retry time zero", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_PREPARE_PUT_WITH_SPACE_FAIL, "prepare put with space fail", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_PREPARE_FAIL, "prepare fail", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY, "not exsit local copy", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_QUERY_FAIL, "mirror client query fail, retuen nullptr", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_GET_RETRY, "sdk mirror client get return retry", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_CLIENT_GET_CEPH_STAT_OK, "sdk mirror client get ceph stat ok", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_CLIENT_GET_CEPH_STAT_SIZE, "sdk mirror client get ceph stat size unequit zero", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_ADDRNUM_INVALID, "sdk mirror client alloc space fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_RSP_NUM_ERROR, "set mirror rsp num error", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_CLIENT_GET_UNDERFS_CONFIG_PASS_SYNC_CALL, "get underfs config pass sync call", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_QUERY_CACHE_RESOURCE_SEND_FAIL, "mirror client query cache resource fail",
                CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_QUERY_CACHE_HIT_SEND_FAIL, "mirror client query cache hit fail",
                CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_ADD_DISK_BY_SEPARATES, "sdk add disk by separates", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_REQUEST_IPC_OPCODE_EXCEED, "sdk request IPC opcode exceed 256, return BIO_ERR", CommonTp::IntValueCallback);
}

void tp::SdkTp::Deregister() noexcept
{
    LVOS_TP_UNREG(SDK_MIRROR_PT_VIEW_FIND_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_CHECK_PT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_AGENT_CREAT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_LOG_CREAT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_LOG_INIT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_AGENT_START_OP_FAIL);
    LVOS_TP_UNREG(SDK_BIO_AGENT_LOAD_FUNC_FAIL);
    LVOS_TP_UNREG(SDK_BIO_MIRROR_SEND_SYNC_FAIL);
    LVOS_TP_UNREG(SDK_BIO_NET_START_RPC_FAIL);
    LVOS_TP_UNREG(SDK_BIO_NET_START_CONNECT_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_PUT_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_GET_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_DELETE_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_ADD_DISK_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_LOAD_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_STAT_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_LIST_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_CHECK_UPDATE_RECV_FAIL);
    LVOS_TP_UNREG(SERVER_NET_ASYNC_CALL_FAIL);
    LVOS_TP_UNREG(SERVER_NET_RDMA_WRITE_FAIL);
    LVOS_TP_UNREG(SERVER_NET_RDMA_READ_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_SET_PT_ID_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_PREPARE_PUT_WITH_SPACE_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_QUERY_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_SELECT_PT_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_SET_RETRY_TIME);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_GET_RETRY);
    LVOS_TP_UNREG(SDK_CLIENT_GET_CEPH_STAT_OK);
    LVOS_TP_UNREG(SDK_CLIENT_GET_CEPH_STAT_SIZE);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_ADDRNUM_INVALID);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_PREPARE_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY);
    LVOS_TP_UNREG(SDK_MIRROR_RSP_NUM_ERROR);
    LVOS_TP_UNREG(SDK_CLIENT_GET_UNDERFS_CONFIG_PASS_SYNC_CALL);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_QUERY_CACHE_RESOURCE_SEND_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_QUERY_CACHE_HIT_SEND_FAIL);
    LVOS_TP_UNREG(SDK_ADD_DISK_BY_SEPARATES);
    LVOS_TP_UNREG(SDK_REQUEST_IPC_OPCODE_EXCEED);
}
#else
void tp::SdkTp::Register() noexcept {}

void tp::SdkTp::Deregister() noexcept {}
#endif
