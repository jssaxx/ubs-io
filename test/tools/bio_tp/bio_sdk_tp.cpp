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
    LVOS_TP_REG(SDK_MIRROR_CHECK_PT_FAIL, "sdk mirror check pt failed, rturn BIO_CHECK_PT_FAIL", CommonTp::BoolValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_CREAT_FAIL, "sdk agent creat fail, rturn BIO_ALLOC_FAIL", CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_LOG_CREAT_FAIL, "sdk log creat fail, rturn -1", CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_LOG_INIT_FAIL, "sdk log init fail, rturn -1", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_START_OP_FAIL, "sdk agent start op fail, rturn BIO_INNER_ERR", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_LOAD_FUNC_FAIL, "sdk agent load func fail, rturn BIO_INNER_ERR", CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_MIRROR_SEND_SYNC_FAIL, "sdk mirror sendsync fail, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_NET_START_RPC_FAIL, "start net post rpc fail, rturn BIO_INNER_ERR", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_NET_START_CONNECT_FAIL, "net post connect fail, rturn BIO_INNER_ERR", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_START_WORK_ALLOC_FAIL, "start work alloc fail, rturn BIO_ALLOC_FAIL", CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_MIRROR_PUT_RECV_FAIL, "sdk mirror recv put request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_GET_RECV_FAIL, "sdk mirror recv get request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_DELETE_RECV_FAIL, "sdk mirror recv delete request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_LOAD_RECV_FAIL, "sdk mirror recv load request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_STAT_RECV_FAIL, "sdk mirror recv stat request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_LIST_RECV_FAIL, "sdk mirror recv list request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_NOTIFY_UPDATE_RECV_FAIL, "sdk mirror notify update request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_CHECK_UPDATE_RECV_FAIL, "sdk mirror check update request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_ASYNC_CALL_FAIL, "async call fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_RDMA_WRITE_FAIL, "rdma write fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_RDMA_READ_FAIL, "rdma read fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_SET_PT_ID_FAIL, "sdk mirror ptId failed, rturn UINT16_MAX", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_ALLOC_PUT_OFFSET_FAIL, "sdk mirror alloc put offset fail, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_SELECT_PT_FAIL, "sdk select pt fail, rturn UINT16_MAX", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_SET_RETRY_TIME, "sdk mirror client set retry time zero", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_PREPARE_PUT_WITH_SPACE_FAIL, "prepare put with space fail", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_QUERY_FAIL, "mirror client query fail, retuen nullptr", CommonTp::NoProcessCallback);
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
    LVOS_TP_UNREG(SDK_BIO_START_WORK_ALLOC_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_PUT_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_GET_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_DELETE_RECV_FAIL);
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
}
#else
void tp::SdkTp::Register() noexcept {}

void tp::SdkTp::Deregister() noexcept {}
#endif
