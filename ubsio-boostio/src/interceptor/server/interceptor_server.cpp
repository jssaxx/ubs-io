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

#include <ctime>
#include <atomic>
#include "bio_c.h"
#include "bio_trace.h"
#include "bio_client_log.h"
#include "message.h"
#include "message_op.h"
#include "interceptor_server.h"

using namespace ock::bio;
using namespace ock::bio::net;

BResult InterceptorServer::RegisterOpcode()
{
    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    auto ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_READ,
        std::bind(&InterceptorServer::HandleInterceptorRead, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor read message handle failed, ret:" << ret << ".");
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_WRITE,
        std::bind(&InterceptorServer::HandleInterceptorWrite, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor write message handle failed, ret:" << ret << ".");
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_ALLOC_BUFF,
        std::bind(&InterceptorServer::HandleInterceptorAllocPage, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor alloc buffer message handle failed, ret:" << ret << ".");
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_LARGE_WRITE,
        std::bind(&InterceptorServer::HandleInterceptorLargeWrite, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor large write message handle failed, ret:" << ret << ".");
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_LARGE_READ,
        std::bind(&InterceptorServer::HandleInterceptorLargeRead, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor large read message handle failed, ret:" << ret << ".");
    }
    return ret;
}

bool InterceptorServer::CheckInterceptorReadReq(InterceptorPreadIn *req)
{
    if (req->nbytes > IO_SIZE_4M || req->nbytes == 0) {
        return false;
    }
    return true;
}

BResult InterceptorServer::HandleInterceptorRead(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorPreadIn)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive interceptor read message len:" << ctx.MessageDataLen() << " or message is invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorPreadIn *>(ctx.MessageData());
    if (!CheckInterceptorReadReq(req)) {
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_ASYNC_BEGIN(MIRROR_TRACE_INTERCEPTOR_READ_START);
    BIO_TRACE_ASYNC_END(MIRROR_TRACE_INTERCEPTOR_READ_START, 0, req->startTime);

    CLIENT_LOG_DEBUG("Receive interceptor read message inode:" << req->inode << " offset:" << req->offset << " len:" <<
        req->nbytes << " fd:" << req->fd);

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_READ);
    auto resp = static_cast<InterceptorPreadOut *>(malloc(sizeof(InterceptorPreadOut) + req->nbytes));
    if (UNLIKELY(resp == nullptr)) {
        CLIENT_LOG_ERROR("Alloc memory failed, inode:" << req->inode << " offset:" << req->offset << " len:" <<
            req->nbytes << " fd:" << req->fd << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_READ, BIO_ALLOC_FAIL);
        return BIO_OK;
    }

    int readLen = static_cast<int>(req->nbytes);
    auto ret = BioReadHook(req->inode, resp->data, req->nbytes, req->offset, &readLen);
    if (UNLIKELY(ret != 0)) {
        CLIENT_LOG_ERROR("Read hook failed, inode:" << req->inode << " offset:" << req->offset << " len:" <<
            req->nbytes << " fd:" << req->fd << ", readLen:" << readLen << ", ret:" << ret << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_READ, BIO_ALLOC_FAIL);
        free(resp);
        resp = nullptr;
        return BIO_OK;
    }

    resp->dataLen = static_cast<uint64_t>(readLen);
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_READ, 0);

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_READ_REPLY);
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(resp),
        sizeof(InterceptorPreadOut) + readLen);
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_READ_REPLY, 0);
    free(resp);
    resp = nullptr;
    return BIO_OK;
}

bool InterceptorServer::CheckInterceptorWriteReq(InterceptorPwriteIn *req)
{
    if (req->nbytes > IO_SIZE_4M || req->nbytes == 0) {
        return false;
    }
    return true;
}

BResult InterceptorServer::HandleInterceptorWrite(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() < sizeof(InterceptorPwriteIn)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive interceptor write message len:" << ctx.MessageDataLen() << " or message is invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE);
    auto *req = static_cast<InterceptorPwriteIn *>(ctx.MessageData());
    if ((ctx.MessageDataLen() < (sizeof(InterceptorPwriteIn) + req->nbytes)) || !CheckInterceptorWriteReq(req)) {
        CLIENT_LOG_ERROR("Invalid request message.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_ASYNC_BEGIN(MIRROR_TRACE_INTERCEPTOR_WRITE_START);
    BIO_TRACE_ASYNC_END(MIRROR_TRACE_INTERCEPTOR_WRITE_START, 0, req->startTime);

    CLIENT_LOG_DEBUG("Receive interceptor write message inode:" << req->inode << " offset:" << req->offset << " len:" <<
        req->nbytes << " fd:" << req->fd);

    InterceptorPwriteOut resp;
    auto ret = BioWriteHook(req->inode, req->data, req->nbytes, req->offset, 0ULL);
    if (UNLIKELY(ret != 0)) {
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, BIO_ERR);
        return BIO_OK;
    }
    resp.dataLen = req->nbytes;
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, 0);

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE_REPLY);
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPwriteOut));
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE_REPLY, 0);
    return BIO_OK;
}

bool InterceptorServer::CheckInterceptorAllocPageReq(InterceptorAllocPageReq *req)
{
    return (req->length <= IO_SIZE_4M && req->length > 0);
}

BResult InterceptorServer::HandleInterceptorAllocPage(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorAllocPageReq)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive interceptor alloc message len:" << ctx.MessageDataLen() << " or message is invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }
    auto *req = static_cast<InterceptorAllocPageReq *>(ctx.MessageData());
    if (!CheckInterceptorAllocPageReq(req)) {
        CLIENT_LOG_DEBUG("Invalid request message.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }
    CLIENT_LOG_DEBUG("Receive interceptor alloc message pid:" << req->pid << " length:" << req->length);

    uint64_t tenantId = 1;
    static uint64_t objectId = 1;
    CacheSpaceDesc addressInfo;
    addressInfo.allocLoc = 1;
    auto ret = BioAllocCacheSpace(tenantId, objectId++, req->length, &addressInfo);
    if (UNLIKELY(ret != 0)) {
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        return BIO_OK;
    }

    if (UNLIKELY(addressInfo.addressNum > CACHE_SPACE_ADDRESS_SIZE)) {
        CLIENT_LOG_ERROR("addressNum: " << addressInfo.addressNum << " is invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }

    if (addressInfo.addressNum == 1) {
        addressInfo.address[1].size = 0;
    }

    CLIENT_LOG_DEBUG("Alloc put value with space length:" << req->length << ", location0:" <<
        addressInfo.loc.location[0] << ", location1:" << addressInfo.loc.location[1] << ", address0 size:" <<
        addressInfo.address[0].size << ", address1 size:" << addressInfo.address[1].size << ", address num:" <<
        addressInfo.addressNum << ".");

    InterceptorAllocPageRsp rsp;
    rsp.address = addressInfo;
    for (uint16_t idx = 0; idx < addressInfo.addressNum; idx++) {
        rsp.addrOffset[idx] =
            BioClientNet::Instance()->GetNetEngine()->GetAddressOffset(addressInfo.address[idx].address);
    }
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&rsp),
        sizeof(InterceptorAllocPageRsp));
    return BIO_OK;
}

bool InterceptorServer::CheckInterceptorLargeWriteReq(InterceptorLargePwriteIn *req)
{
    if (req->nbytes > IO_SIZE_4M || req->nbytes == 0) {
        return false;
    }
    return true;
}

bool InterceptorServer::CheckInterceptorLargeReadReq(InterceptorLargePreadIn *req)
{
    if (req->nbytes > IO_SIZE_4M || req->nbytes == 0) {
        return false;
    }
    return true;
}

BResult InterceptorServer::HandleInterceptorLargeWrite(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorLargePwriteIn)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive interceptor large write message len:" << ctx.MessageDataLen() <<
            " or message data invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE);
    auto *req = static_cast<InterceptorLargePwriteIn *>(ctx.MessageData());
    if (!CheckInterceptorLargeWriteReq(req)) {
        CLIENT_LOG_ERROR("Invalid request message.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_ASYNC_BEGIN(MIRROR_TRACE_INTERCEPTOR_WRITE_START);
    BIO_TRACE_ASYNC_END(MIRROR_TRACE_INTERCEPTOR_WRITE_START, 0, req->startTime);

    CLIENT_LOG_DEBUG("Receive interceptor large write message inode:" << req->inode << " offset:" << req->offset <<
        " len:" << req->nbytes << " fd:" << req->fd << " shmOffset:" << req->shmOffset);

    uint8_t *srcAddr = BioClientNet::Instance()->GetNetEngine()->GetShmAddress(req->shmOffset, req->nbytes);
    if (UNLIKELY(srcAddr == nullptr)) {
        CLIENT_LOG_ERROR("Get shm address failed, shmOffset:" << req->shmOffset << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, BIO_ERR);
        return BIO_OK;
    }

    uint64_t tenantId = 1;
    static std::atomic<uint64_t> objectId{1};
    CacheSpaceDesc addressInfo;
    addressInfo.allocLoc = 1;
    auto ret = BioAllocCacheSpace(tenantId, objectId.fetch_add(1), req->nbytes, &addressInfo);
    if (UNLIKELY(ret != 0)) {
        CLIENT_LOG_ERROR("Alloc cache space failed, ret:" << ret << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, BIO_ERR);
        return BIO_OK;
    }

    for (uint32_t i = 0; i < addressInfo.addressNum; i++) {
        uint8_t *dstAddr = reinterpret_cast<uint8_t *>(addressInfo.address[i].address);
        uint32_t copyLen = addressInfo.address[i].size;
        errno_t cpRet = memcpy_s(dstAddr, copyLen, srcAddr, copyLen);
        if (UNLIKELY(cpRet != 0)) {
            CLIENT_LOG_ERROR("memcpy_s failed, ret:" << cpRet << ", idx:" << i << ".");
            BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
            BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, BIO_ERR);
            return BIO_OK;
        }
        srcAddr += copyLen;
    }

    InterceptorPwriteOut resp;
    resp.ret = static_cast<int32_t>(BioWriteCopyFreeHook(req->inode, req->offset, req->nbytes, &addressInfo));
    if (UNLIKELY(resp.ret < 0)) {
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, BIO_ERR);
        return BIO_OK;
    }
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, 0);

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE_REPLY);
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPwriteOut));
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE_REPLY, 0);
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorLargeRead(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorLargePreadIn)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive interceptor large read message len:" << ctx.MessageDataLen() <<
            " or message data invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorLargePreadIn *>(ctx.MessageData());
    if (!CheckInterceptorLargeReadReq(req)) {
        CLIENT_LOG_ERROR("Invalid request message.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    CLIENT_LOG_DEBUG("Receive interceptor large read message inode:" << req->inode << " offset:" << req->offset <<
        " len:" << req->nbytes << " fd:" << req->fd << " shmOffset:" << req->shmOffset);

    uint8_t *shmAddr = BioClientNet::Instance()->GetNetEngine()->GetShmAddress(req->shmOffset, req->nbytes);
    if (UNLIKELY(shmAddr == nullptr)) {
        CLIENT_LOG_ERROR("Get shm address failed, shmOffset:" << req->shmOffset << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }

    int readLen = 0;
    int ret = BioReadHook(req->inode, reinterpret_cast<char *>(shmAddr), req->nbytes, req->offset, &readLen);
    if (UNLIKELY(ret != 0 || readLen <= 0)) {
        CLIENT_LOG_ERROR("BioReadHook failed, ret:" << ret << ", readLen:" << readLen << ".");
        InterceptorLargePreadOut resp;
        resp.ret = ret;
        resp.dataLen = 0;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
            sizeof(InterceptorLargePreadOut));
        return BIO_OK;
    }

    InterceptorLargePreadOut resp;
    resp.ret = 0;
    resp.dataLen = static_cast<int64_t>(readLen);
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorLargePreadOut));
    return BIO_OK;
}

BResult InterceptorServer::Initialize()
{
    return RegisterOpcode();
}