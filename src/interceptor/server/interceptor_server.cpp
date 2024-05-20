/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#include <ctime>
#include <bio_c.h>
#include <bio_log.h>
#include <bio_err.h>
#include "bio_trace.h"
#include "message.h"
#include "message_op.h"
#include "bio_server.h"
#include "interceptor_server.h"

using namespace ock::bio;

void InterceptorServer::RegisterOpcode()
{
    auto netEngine = BioServer::Instance()->GetNetEngine();
    netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_READ,
        std::bind(&InterceptorServer::HandleInterceptorRead, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_WRITE,
        std::bind(&InterceptorServer::HandleInterceptorWrite, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_ALLOC_BUFF,
        std::bind(&InterceptorServer::HandleInterceptorAllocPage, this, std::placeholders::_1));
    netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_LARGE_WRITE,
        std::bind(&InterceptorServer::HandleInterceptorLargeWrite, this, std::placeholders::_1));
}

int32_t InterceptorServer::HandleInterceptorRead(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorPreadIn)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive interceptor read message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorPreadIn *>(ctx.MessageData());
    BIO_TRACE_ASYNC_BEGIN(MIRROR_TRACE_INTERCEPTOR_READ_START);
    BIO_TRACE_ASYNC_END(MIRROR_TRACE_INTERCEPTOR_READ_START, 0, req->startTime);

    LOG_INFO("Receive interceptor read message inode:" << req->inode << " offset:" << req->offset << " len:" <<
        req->nbytes << " fd:" << req->fd);

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_READ);
    InterceptorPreadOut *resp = static_cast<InterceptorPreadOut *>(malloc(sizeof(InterceptorPreadOut) + req->nbytes));
    if (UNLIKELY(resp == nullptr)) {
        LOG_ERROR("Alloc memory failed, inode:" << req->inode << " offset:" << req->offset << " len:" <<
            req->nbytes << " fd:" << req->fd << ".");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_READ, BIO_ALLOC_FAIL);
        return BIO_OK;
    }

    int readLen = static_cast<int>(req->nbytes);
    auto ret = BioReadHook(req->inode, resp->data, req->nbytes, req->offset, &readLen);
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Read hook failed, inode:" << req->inode << " offset:" << req->offset << " len:" <<
            req->nbytes << " fd:" << req->fd << ", readLen:" << readLen << ", ret:" << ret << ".");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_READ, BIO_ALLOC_FAIL);
        free(resp);
        resp = nullptr;
        return BIO_OK;
    }

    resp->dataLen = static_cast<uint64_t>(readLen);
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_READ, 0);

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_READ_REPLY);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(resp),
        sizeof(InterceptorPreadOut) + readLen);
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_READ_REPLY, 0);
    free(resp);
    resp = nullptr;
    return BIO_OK;
}

int32_t InterceptorServer::HandleInterceptorWrite(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive interceptor write message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE);
    auto *req = static_cast<InterceptorPwriteIn *>(ctx.MessageData());
    BIO_TRACE_ASYNC_BEGIN(MIRROR_TRACE_INTERCEPTOR_WRITE_START);
    BIO_TRACE_ASYNC_END(MIRROR_TRACE_INTERCEPTOR_WRITE_START, 0, req->startTime);

    LOG_INFO("Receive interceptor write message inode:" << req->inode << " offset:" << req->offset << " len:" <<
        req->nbytes << " fd:" << req->fd);
    InterceptorPwriteOut resp;

    resp.dataLen = static_cast<int64_t>(BioWriteHook(req->inode, req->data, req->nbytes, req->offset, 0ULL));
    if (UNLIKELY(resp.dataLen < 0)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, BIO_ERR);
        return BIO_OK;
    }

    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, 0);

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE_REPLY);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPwriteOut));
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE_REPLY, 0);
    return BIO_OK;
}

int32_t InterceptorServer::HandleInterceptorAllocPage(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorAllocPageReq)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive interceptor alloc message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorAllocPageReq *>(ctx.MessageData());
    LOG_INFO("Receive interceptor alloc message pid:" << req->pid << " length:" << req->length);

    uint64_t tenantId = 1;
    static uint64_t objectId = 1;
    CacheSpaceInfo addressInfo;
    addressInfo.allocLoc = 1;
    auto ret = BioAllocSpace(tenantId, objectId++, req->length, &addressInfo);
    if (UNLIKELY(ret != 0)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        return BIO_OK;
    }

    InterceptorAllocPageRsp rsp;
    LOG_INFO("Alloc put value with space length:" << req->length << ", location0:" << addressInfo.loc.location[0] <<
        ", location1:" << addressInfo.loc.location[1] << ", address0:" << addressInfo.address[0].address <<
        ", address0 size:" << addressInfo.address[0].size << ", address1:" << addressInfo.address[1].address <<
        ", address1 size:" << addressInfo.address[1].size << ", address num:" << addressInfo.addressNum << ".");

    rsp.address = addressInfo;
    for (uint16_t idx = 0; idx < addressInfo.addressNum; idx++) {
        rsp.addrOffset[idx] = BioServer::Instance()->GetNetEngine()->GetAddressOffset(addressInfo.address[idx].address);
    }
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&rsp),
        sizeof(InterceptorAllocPageRsp));
    return BIO_OK;
}

int32_t InterceptorServer::HandleInterceptorLargeWrite(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorLargePwriteIn)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        LOG_ERROR("Receive interceptor large write message len:" << ctx.MessageDataLen() <<
            " or message data invalid.");
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE);
    auto *req = static_cast<InterceptorLargePwriteIn *>(ctx.MessageData());
    BIO_TRACE_ASYNC_BEGIN(MIRROR_TRACE_INTERCEPTOR_WRITE_START);
    BIO_TRACE_ASYNC_END(MIRROR_TRACE_INTERCEPTOR_WRITE_START, 0, req->startTime);

    LOG_INFO("Receive interceptor large write message inode:" << req->inode << " offset:" << req->offset << " len:" <<
        req->nbytes << " fd:" << req->fd);
    InterceptorPwriteOut resp;

    CacheSpaceInfo addressInfo = req->address;
    LOG_INFO("Alloc put value with space length:" << req->nbytes << ", location0:" << addressInfo.loc.location[0] <<
        ", location1:" << addressInfo.loc.location[1] << ", address0:" << addressInfo.address[0].address <<
        ", address0 size:" << addressInfo.address[0].size << ", address1:" << addressInfo.address[1].address <<
        ", address1 size:" << addressInfo.address[1].size << ".");
    resp.ret = 0;
    resp.dataLen = static_cast<int64_t>(BioWriteCopyFreeHook(req->inode, req->offset, req->nbytes, &addressInfo));
    if (UNLIKELY(resp.dataLen < 0)) {
        BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, BIO_ERR);
        return BIO_OK;
    }

    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, 0);

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE_REPLY);
    BioServer::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPwriteOut));
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE_REPLY, 0);
    return BIO_OK;
}

int32_t InterceptorServer::StartServer()
{
    RegisterOpcode();
    return BIO_OK;
}