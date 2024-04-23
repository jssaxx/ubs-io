/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include "bio_sdk_tp.h"

using namespace ock::bio;
#ifdef __aarch64__
static uint32_t MY_PID = 102;
void tp::SdkTp::Register() noexcept
{
    LVOS_TP_REG(SDK_SET_RET_FAIL, "set sdk ret failed, return BIO_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_SET_RET_FAIL_CONFLICT, "set sdk ret failed, return BIO_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_SET_BIO_CLIENT_NET_PRE_INIT_FAIL, "set sdk BioClientNetPreInit ret failed, return BIO_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_SET_BIO_CLIENT_NET_PRE_INIT_FAIL_CONFLICT, "set sdk BioClientNetPreInit ret failed, return BIO_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_SET_BIO_CLIENT_MIRROR_INIT_FAIL, "set sdk BioClientMirrorInit ret failed, return BIO_ERR", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SDK_SET_BIO_CLIENT_MIRROR_INIT_FAIL_CONFLICT, "set sdk BioClientMirrorInit ret failed, return BIO_ERR", tp::CommonTp::IntValueCallback);
}

void tp::SdkTp::Deregister() noexcept
{
    LVOS_TP_UNREG(SDK_SET_RET_FAIL);
    LVOS_TP_UNREG(SDK_SET_RET_FAIL_CONFLICT);
    LVOS_TP_UNREG(SDK_SET_BIO_CLIENT_NET_PRE_INIT_FAIL);
    LVOS_TP_UNREG(SDK_SET_BIO_CLIENT_NET_PRE_INIT_FAIL_CONFLICT);
    LVOS_TP_UNREG(SDK_SET_BIO_CLIENT_MIRROR_INIT_FAIL);
    LVOS_TP_UNREG(SDK_SET_BIO_CLIENT_MIRROR_INIT_FAIL_CONFLICT);
}
#else
void tp::SdkTp::Register() noexcept {}

void tp::SdkTp::Deregister() noexcept {}
#endif
