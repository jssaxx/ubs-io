/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 * Description: net_stub.cpp
 * date: 2024-04-18
 */

#include <atomic>
#include "securec.h"
#include "bio_log.h"
#include "bio_err.h"
#include "net_stub.h"

namespace ock {
namespace hcom {
NetChannel *NetServiceDefaultImp::MakeChannel()
{
    NetServiceConnectOptions options(0, 0, CHANNEL_FUNC_CB, 0, 0, 0);
    return new NetChannel(idGen.fetch_add(1UL), true, true, options);
}

std::atomic<uint64_t> NetServiceDefaultImp::idGen{ 0x1100UL };
}
}

namespace ock {
namespace bio {
using namespace ock::hcom;

std::atomic<uint64_t> NetStub::connectCount{ 0UL };

int32_t NetStub::Connect(const std::string &oobIpOrName, uint16_t oobPort, const std::string &payload, ChannelPtr &ch,
    NetServiceConnectOptions options)
{
    LOG_INFO("Connect stub.");
    ch.Set(NetServiceDefaultImp::MakeChannel());
    connectCount.fetch_add(1L);
    return BIO_OK;
}

int32_t NetStub::SyncCall(const NetServiceOpInfo &reqOpInfo, const NetServiceMessage &req, NetServiceOpInfo &rspOpInfo,
    NetServiceMessage &rsp)
{
    LOG_INFO("SyncCall stub.");
    if (rsp.data == nullptr && rsp.size == 0) {
        uint32_t rspDataSize = 64U;
        rsp.data = malloc(rspDataSize);
        memset_s(rsp.data, rspDataSize, 0, rspDataSize);
        rsp.size = rspDataSize;
    } else {
        memset_s(rsp.data, rsp.size, 0, rsp.size);
    }
    return BIO_OK;
}

int32_t NetStub::AsyncCall(const NetServiceOpInfo &reqOpInfo, const NetServiceMessage &req, Callback callback)
{
    LOG_INFO("AsyncCall stub.");
    uint32_t rspDataSize = 64U;
    uint8_t *rspData = static_cast<uint8_t *>(malloc(rspDataSize));
    memset_s(rspData, rspDataSize, 0, rspDataSize);
    callback.cb(callback.cbCtx, static_cast<void *>(rspData), rspDataSize, BIO_OK);
    return BIO_OK;
}

int32_t NetStub::SyncRead(const NetRequest &request)
{
    LOG_INFO("SyncRead stub.");
    return BIO_OK;
}

int32_t NetStub::SyncWrite(const NetRequest &request)
{
    LOG_INFO("SyncWrite stub.");
    return BIO_OK;
}

int32_t NetStub::SendFds(int fds[], uint32_t len)
{
    LOG_INFO("SendFds stub.");
    return BIO_OK;
}
}
}