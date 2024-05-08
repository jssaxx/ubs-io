/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include "bio_sdk_tp.h"

using namespace ock::bio;
#ifdef __aarch64__
static uint32_t MY_PID = 102;
void tp::SdkTp::Register() noexcept
{
    LVOS_TP_REG(SDK_MIRROR_PT_VIEW_FIND_FAIL, "sdk mirror mPtView find failed, rturn BIO_INVALID_PARAM", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_MIRROR_CHECK_PT_FAIL, "sdk mirror check pt failed, rturn BIO_CHECK_PT_FAIL", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_PUT_MEMORY_FAIL, "sdk mirror alloc put memory failed, rturn BIO_INNER_ERR", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_MIRROR_SEND_PUT_FAIL, "sdk mirror send put request failed, rturn BIO_INNER_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_CREAT_FAIL, "sdk agent creat fail, rturn BIO_ALLOC_FAIL", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_LOG_CREAT_FAIL, "sdk log creat fail, rturn -1", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_LOG_INIT_FAIL, "sdk log init fail, rturn -1", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_START_OP_FAIL, "sdk agent start op fail, rturn BIO_INNER_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_LOAD_FUNC_FAIL, "sdk agent load func fail, rturn BIO_INNER_ERR", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_NET_PRE_CREAT_FAIL, "sdk net instance creat fail, rturn BIO_ALLOC_FAIL", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_NET_PRE_START_PRE_FAIL, "sdk net service instance creat fail, rturn BIO_INNER_ERR", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_MIRROR_CREAT_FAIL, "sdk mirror instance creat fail, rturn BIO_ALLOC_FAIL", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_MIRROR_SEND_SYNC_FAIL, "sdk mirror sendsync fail, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_CLUSTER_CHANGE_MODE, "change sdk cluster mode, rturn BIO_INNER_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_PT_VIEW_CHANGE_MODE, "change sdk agent ptView mode, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_NODE_CHANGE_MODE, "change sdk agent node mode, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_AGENT_RESOURCE_CHANGE_MODE, "change sdk resource node mode, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_NET_START_RPC_FAIL, "start net post rpc fail, rturn BIO_INNER_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_START_POST_CHANGE_MODE, "change start post mode, rturn BIO_INNER_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_START_POST_IGNORE_CHECK, "ignore check node id, rturn BIO_INVALID_PARAM", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(SDK_BIO_NET_START_CONNECT_FAIL, "net post connect fail, rturn BIO_INNER_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_BIO_START_WORK_ALLOC_FAIL, "start work alloc fail, rturn BIO_ALLOC_FAIL", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(SDK_BIO_CREAT_FLOW_MASTER_FAIL, "creat flow master fail, rturn BIO_INVALID_PARAM", tp::CommonTp::IntValueCallback);

    LVOS_TP_REG(SDK_MIRROR_PUT_SEND_FAIL, "sdk mirror send put request failed, rturn BIO_NET_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_PUT_RECV_FAIL, "sdk mirror recv put request failed, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_GET_RECV_FAIL, "sdk mirror recv get request failed, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_DELETE_RECV_FAIL, "sdk mirror recv delete request failed, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_LOAD_RECV_FAIL, "sdk mirror recv load request failed, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_STAT_RECV_FAIL, "sdk mirror recv stat request failed, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_LIST_RECV_FAIL, "sdk mirror recv list request failed, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);

    LVOS_TP_REG(SERVER_NET_ASYNC_CALL_FAIL, "async call fail", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_SYNC_CALL_FAIL, "async call fail", tp::CommonTp::IntValueCallback);

    LVOS_TP_REG(SERVER_NET_RDMA_WRITE_FAIL, "rdma write fail", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVER_NET_RDMA_READ_FAIL, "rdma read fail", tp::CommonTp::IntValueCallback);
}

void tp::SdkTp::Deregister() noexcept
{
    LVOS_TP_UNREG(SDK_MIRROR_PT_VIEW_FIND_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_CHECK_PT_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_PUT_MEMORY_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_SEND_PUT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_AGENT_CREAT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_LOG_CREAT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_LOG_INIT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_AGENT_START_OP_FAIL);
    LVOS_TP_UNREG(SDK_BIO_AGENT_LOAD_FUNC_FAIL);
    LVOS_TP_UNREG(SDK_BIO_NET_PRE_CREAT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_NET_PRE_START_PRE_FAIL);
    LVOS_TP_UNREG(SDK_BIO_MIRROR_CREAT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_MIRROR_SEND_SYNC_FAIL);
    LVOS_TP_UNREG(SDK_BIO_CLUSTER_CHANGE_MODE);
    LVOS_TP_UNREG(SDK_BIO_AGENT_PT_VIEW_CHANGE_MODE);
    LVOS_TP_UNREG(SDK_BIO_AGENT_NODE_CHANGE_MODE);
    LVOS_TP_UNREG(SDK_BIO_AGENT_RESOURCE_CHANGE_MODE);
    LVOS_TP_UNREG(SDK_BIO_NET_START_RPC_FAIL);
    LVOS_TP_UNREG(SDK_BIO_START_POST_CHANGE_MODE);
    LVOS_TP_UNREG(SDK_BIO_START_POST_IGNORE_CHECK);
    LVOS_TP_UNREG(SDK_BIO_NET_START_CONNECT_FAIL);
    LVOS_TP_UNREG(SDK_BIO_START_WORK_ALLOC_FAIL);
    LVOS_TP_UNREG(SDK_BIO_CREAT_FLOW_MASTER_FAIL);

    LVOS_TP_UNREG(SDK_MIRROR_PUT_SEND_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_PUT_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_GET_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_DELETE_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_LOAD_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_STAT_RECV_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_LIST_RECV_FAIL);

    LVOS_TP_UNREG(SERVER_NET_ASYNC_CALL_FAIL);
    LVOS_TP_UNREG(SERVER_NET_SYNC_CALL_FAIL);

    LVOS_TP_UNREG(SERVER_NET_RDMA_WRITE_FAIL);
    LVOS_TP_UNREG(SERVER_NET_RDMA_READ_FAIL);
}
#else
void tp::SdkTp::Register() noexcept {}

void tp::SdkTp::Deregister() noexcept {}
#endif
