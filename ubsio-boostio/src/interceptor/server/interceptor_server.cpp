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
#include <fcntl.h>
#include <sys/mman.h>
#include "bio_c.h"
#include "bio_trace.h"
#include "bio_client_log.h"
#include "bio_client_net.h"
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
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_CREATE_DATA_MSG_MEM_POOL,
        std::bind(&InterceptorServer::HandleInterceptorCreateDataMsgMemPool, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor create data msg mem pool message handle failed, ret:" << ret << ".");
        return ret;
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

    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(resp),
        sizeof(InterceptorPreadOut) + readLen);
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

    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE);
    auto ret = BioWriteHook(req->inode, req->data, req->nbytes, req->offset, 0ULL);
    if (UNLIKELY(ret != 0)) {
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, BIO_ERR);
        return BIO_OK;
    }
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, 0);

    InterceptorPwriteOut resp;
    resp.dataLen = req->nbytes;

    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPwriteOut));
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

uint8_t *InterceptorServer::TransDataMsgMemAddr(uint32_t pid, uint64_t mrOffset)
{
    std::lock_guard<std::mutex> lock(mDataMsgMemLock);
    auto iter = mDataMsgMemMgr.find(pid);
    if (UNLIKELY(iter == mDataMsgMemMgr.end())) {
        CLIENT_LOG_ERROR("Trans data msg mem addr failed, pid:" << pid << " not found.");
        return nullptr;
    }

    if (UNLIKELY(mrOffset >= iter->second.size)) {
        CLIENT_LOG_ERROR("Trans data msg mem addr failed, mrOffset:" << mrOffset << " >= size:" << iter->second.size);
        return nullptr;
    }

    return iter->second.address + mrOffset;
}

BResult InterceptorServer::HandleInterceptorCreateDataMsgMemPool(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorCreateDataMsgMemPoolRequest)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive interceptor create data msg mem pool message len:" << ctx.MessageDataLen() <<
            " or message is invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorCreateDataMsgMemPoolRequest *>(ctx.MessageData());
    CLIENT_LOG_DEBUG("Receive interceptor create data msg mem pool request, pid:" << req->comm.pid);

    uint64_t poolSize = BioClientNet::Instance()->GetSdkPoolSize();
    uint64_t blockSize = BioClientNet::Instance()->GetSegment();
    std::string shmName = "/interceptor_mem_pool_" + std::to_string(req->comm.pid);

    int fd = shm_open(shmName.c_str(), O_CREAT | O_RDWR | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        CLIENT_LOG_ERROR("shm_open failed, name:" << shmName << ", error:" << strerror(errno));
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }

    if (ftruncate(fd, static_cast<off_t>(poolSize)) < 0) {
        CLIENT_LOG_ERROR("ftruncate failed, size:" << poolSize << ", error:" << strerror(errno));
        shm_unlink(shmName.c_str());
        close(fd);
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }

    int32_t shmFd = fd;
    off_t offset = 0;
    auto address = mmap(nullptr, poolSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, offset);
    if (address == MAP_FAILED) {
        CLIENT_LOG_ERROR("Mmap shm size " << poolSize << " offset " << offset << " failed, error:" << strerror(errno));
        shm_unlink(shmName.c_str());
        close(shmFd);
        shmFd = -1;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ERR, nullptr, 0);
        return BIO_OK;
    }

    (void)memset_s(address, poolSize, 0, poolSize);

    auto ret = BioClientNet::Instance()->GetNetEngine()->SendFds(ctx.Channel(), &shmFd, NO_1);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send fds failed, ret:" << ret << ", name:" << shmName << ".");
        if (munmap(address, poolSize) == -1) {
            CLIENT_LOG_ERROR("Munmap address failed.");
        }
        shm_unlink(shmName.c_str());
        close(shmFd);
        shmFd = -1;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ERR, nullptr, 0);
        return BIO_OK;
    }

    {
        std::lock_guard<std::mutex> lock(mDataMsgMemLock);
        mDataMsgMemMgr.emplace(req->comm.pid, DataMsgMemItem(shmFd, offset, poolSize, static_cast<uint8_t *>(address)));
    }

    CLIENT_LOG_INFO("Succeed to create interceptor data message memory pool, size:" << poolSize <<
        ", blockSize:" << blockSize << ", holder:" << req->comm.pid << ".");

    InterceptorCreateDataMsgMemPoolResponse rsp;
    rsp.memFd = shmFd;
    rsp.offset = offset;
    rsp.poolSize = poolSize;
    rsp.blockSize = blockSize;
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(InterceptorCreateDataMsgMemPoolResponse));
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorLargeWrite(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorLargePwriteIn)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive interceptor large write message len:" << ctx.MessageDataLen() <<
            " or message data invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorLargePwriteIn *>(ctx.MessageData());
    if (!CheckInterceptorLargeWriteReq(req)) {
        CLIENT_LOG_ERROR("Invalid request message.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_ASYNC_BEGIN(MIRROR_TRACE_INTERCEPTOR_WRITE_START);
    BIO_TRACE_ASYNC_END(MIRROR_TRACE_INTERCEPTOR_WRITE_START, 0, req->startTime);

    CLIENT_LOG_DEBUG("Receive interceptor large write message inode:" << req->inode << " offset:" << req->offset <<
        " len:" << req->nbytes << " fd:" << req->fd << " mrOffset:" << req->mrOffset);

    uint8_t *srcAddr = TransDataMsgMemAddr(req->pid, req->mrOffset);
    if (UNLIKELY(srcAddr == nullptr)) {
        CLIENT_LOG_ERROR("Get data msg mem address failed, pid:" << req->pid << ", mrOffset:" << req->mrOffset << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }

    InterceptorPwriteOut resp;
    BIO_TRACE_START(MIRROR_TRACE_INTERCEPTOR_WRITE);
    resp.ret = static_cast<int32_t>(BioWriteHook(req->inode, reinterpret_cast<char *>(srcAddr), req->nbytes, req->offset, 0ULL));
    if (UNLIKELY(resp.ret != 0)) {
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, BIO_ERR);
        return BIO_OK;
    }
    BIO_TRACE_END(MIRROR_TRACE_INTERCEPTOR_WRITE, 0);
    resp.dataLen = req->nbytes;

    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPwriteOut));
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
        " len:" << req->nbytes << " fd:" << req->fd << " mrOffset:" << req->mrOffset);

    uint8_t *shmAddr = TransDataMsgMemAddr(req->pid, req->mrOffset);
    if (UNLIKELY(shmAddr == nullptr)) {
        CLIENT_LOG_ERROR("Get data msg mem address failed, pid:" << req->pid << ", mrOffset:" << req->mrOffset << ".");
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
