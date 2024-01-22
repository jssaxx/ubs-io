/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include "bio_log.h"
#include "bio_config_instance.h"
#include "bio_server.h"
#include "bio_client.h"

using namespace ock::bio;

BResult BioClient::StartRpcService()
{
    if (mConfig->GetCmConfig().deployType == 1) {
        mRpcService = BioServer::Instance()->GetRpcEngine();
        return BIO_OK;
    } else {
        LOG_WARN("Not support separated deployment");
        return BIO_ERR;
    }
}

BResult BioClient::Start()
{
    std::lock_guard<std::mutex> lock(mStartLock);
    if (mStarted) {
        return BIO_OK;
    }

    // load configuration instance
    mConfig = BioConfig::Instance();
    if (UNLIKELY(mConfig == nullptr)) {
        LOG_ERROR("Create configure instance failed.");
        return BIO_ERR;
    }
    if (mConfig->GetCmConfig().deployType != 1) {
        LOG_WARN("Not support separated deployment");
        return BIO_ERR;
    }

    // start rpc service
    BResult ret = StartRpcService();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to start rpc server, ret" << ret << ".");
        return ret;
    }

    // initialize mirror client
    mMirror = MakeRef<MirrorClient>(mConfig->GetCmConfig().deployType);
    if (UNLIKELY(mMirror == nullptr)) {
        LOG_ERROR("Create mirror client instance failed.");
        return BIO_ERR;
    }
    ret = mMirror->Initialize();
    if (UNLIKELY(ret != BIO_OK)) {
        LOG_ERROR("Failed to init mirror client, ret:" << ret << ".");
        return ret;
    }

    mStarted = true;
    LOG_INFO("Boostio client started.");
    return BIO_OK;
}

void BioClient::Stop()
{
    mStarted = false;
    LOG_INFO("Boostio client Stopped!");
}