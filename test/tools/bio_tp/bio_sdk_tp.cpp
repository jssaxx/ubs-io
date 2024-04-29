/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include "bio_sdk_tp.h"

using namespace ock::bio;
#ifdef __aarch64__
static uint32_t MY_PID = 102;
void tp::SdkTp::Register() noexcept
{
    LVOS_TP_REG(SDK_MIRROR_PT_VIEW_FIND_FAIL, "sdk mirror mPtView find failed, rturn BIO_INVALID_PARAM", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_CHECK_PT_FAIL, "sdk mirror check pt failed, rturn BIO_CHECK_PT_FAIL", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_PUT_MEMORY_FAIL, "sdk mirror Alloc put memory failed, rturn BIO_INNER_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_SEND_PUT_FAIL, "sdk mirror send put request failed, rturn BIO_INNER_ERR", tp::CommonTp::IntValueCallback);

    LVOS_TP_REG(SDK_MIRROR_PUT_SEND_FAIL, "sdk mirror send put request failed, rturn BIO_NET_RETRY", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_MIRROR_PUT_RECV_FAIL, "sdk mirror recv put request failed, rturn BIO_INNER_RETRY", tp::CommonTp::IntValueCallback);

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

    LVOS_TP_UNREG(SDK_MIRROR_PUT_SEND_FAIL);
    LVOS_TP_UNREG(SDK_MIRROR_PUT_RECV_FAIL);

    LVOS_TP_UNREG(SERVER_NET_ASYNC_CALL_FAIL);
    LVOS_TP_UNREG(SERVER_NET_SYNC_CALL_FAIL);

    LVOS_TP_UNREG(SERVER_NET_RDMA_WRITE_FAIL);
    LVOS_TP_UNREG(SERVER_NET_RDMA_READ_FAIL);
}
#else
void tp::SdkTp::Register() noexcept {}

void tp::SdkTp::Deregister() noexcept {}
#endif
