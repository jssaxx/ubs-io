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
    LVOS_TP_REG(SDK_MIRROR_PUT_RECV_FAIL, "sdk mirror recv put request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_GET_RECV_FAIL, "sdk mirror recv get request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_DELETE_RECV_FAIL, "sdk mirror recv delete request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_ADD_DISK_RECV_FAIL, "sdk mirror recv add disk request failed, rturn BIO_INNER_RETRY", CommonTp::IntValueCallback);
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
    LVOS_TP_REG(SDK_MIRROR_CLIENT_PREPARE_FAIL, "prepare fail", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY, "not exsit local copy", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_QUERY_FAIL, "mirror client query fail, retuen nullptr", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_ADDRNUM_INVALID, "sdk mirror client alloc space fail", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_RSP_NUM_ERROR, "set mirror rsp num error", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_CLIENT_GET_UNDERFS_CONFIG_PASS_SYNC_CALL, "get underfs config pass sync call", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_QUERY_CACHE_RESOURCE_SEND_FAIL, "mirror client query cache resource fail",
                CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_CLIENT_QUERY_CACHE_HIT_SEND_FAIL, "mirror client query cache hit fail",
                CommonTp::IntValueCallback);
    LVOS_TP_REG(START_IPC_SERVICE_NULL, "start ipc service null", CommonTp::PointerValueCallback);
    LVOS_TP_REG(START_IPC_SERVICE_NOT_PREPARE_HSE_CRYPTOR, "start ipc service not prepare hse cryptor",
                CommonTp::NoProcessCallback);
    LVOS_TP_REG(START_IPC_SERVICE_SET_TLS_ENABLE, "start ipc service set tls enable", CommonTp::BoolValueCallback);
    LVOS_TP_REG(BIO_CRYPTOR_HELPER_UNDO, "bio cryptor helper undo",
                CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_ADD_DISK_BY_SEPARATES, "sdk add disk by separates", CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_PUT_NOT_RETRY, "sdk put do not retry", CommonTp::BoolValueCallback);
    LVOS_TP_REG(SDK_CREATE_DATA_MESSAGE_MEM_POOL_SEND_SUCCESS, "create data message mem pool send sync success", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_PREPARE_RESOURCE_LOCAL_SUCCESS, "Send prepare resource local success", CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_GET_LOCAL_NODE_INFO_REQUEST, "Send get local node info request success",
                CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_CREATE_FLOW_REQUEST_SEND_SUCCESS, "create flow request send sync success",
                CommonTp::IntValueCallback);
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
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_ADDRNUM_INVALID);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_PREPARE_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_NOT_EXIST_LOCAL_COPY);
    LVOS_TP_UNREG(SDK_MIRROR_RSP_NUM_ERROR);
    LVOS_TP_UNREG(SDK_CLIENT_GET_UNDERFS_CONFIG_PASS_SYNC_CALL);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_QUERY_CACHE_RESOURCE_SEND_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_CLIENT_QUERY_CACHE_HIT_SEND_FAIL);
    LVOS_TP_UNREG(START_IPC_SERVICE_NULL);
    LVOS_TP_UNREG(START_IPC_SERVICE_NOT_PREPARE_HSE_CRYPTOR);
    LVOS_TP_UNREG(START_IPC_SERVICE_SET_TLS_ENABLE);
    LVOS_TP_UNREG(BIO_CRYPTOR_HELPER_UNDO);
    LVOS_TP_UNREG(SDK_ADD_DISK_BY_SEPARATES);
    LVOS_TP_UNREG(SDK_PUT_NOT_RETRY);
    LVOS_TP_UNREG(SDK_CREATE_DATA_MESSAGE_MEM_POOL_SEND_SUCCESS);
    LVOS_TP_UNREG(SDK_CREATE_FLOW_REQUEST_SEND_SUCCESS);
    LVOS_TP_UNREG(SDK_GET_LOCAL_NODE_INFO_REQUEST);
    LVOS_TP_UNREG(SDK_PREPARE_RESOURCE_LOCAL_SUCCESS);
}
#else
void tp::SdkTp::Register() noexcept {}

void tp::SdkTp::Deregister() noexcept {}
#endif
