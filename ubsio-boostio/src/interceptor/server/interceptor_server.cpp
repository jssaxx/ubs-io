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

#include <vector>
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

namespace {
void CleanupDataMsgMemItem(uint32_t pid, DataMsgMemItem &item)
{
    if (item.address != nullptr && item.size > 0) {
        if (munmap(item.address, item.size) == -1) {
            CLIENT_LOG_ERROR("Munmap data msg mem failed, pid:" << pid << ", error:" << strerror(errno) << ".");
        }
        item.address = nullptr;
    }

    if (item.shmFd >= 0) {
        close(item.shmFd);
        item.shmFd = -1;
    }

    if (pid != 0) {
        std::string shmName = "/interceptor_mem_pool_" + std::to_string(pid);
        shm_unlink(shmName.c_str());
    }
}

BResult CreateDataMsgMemItem(uint32_t pid, uint64_t poolSize, DataMsgMemItem &item)
{
    std::string shmName = "/interceptor_mem_pool_" + std::to_string(pid);
    shm_unlink(shmName.c_str());

    int fd = shm_open(shmName.c_str(), O_CREAT | O_RDWR | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        CLIENT_LOG_ERROR("shm_open failed, pid:" << pid << ", name:" << shmName << ", error:" << strerror(errno));
        return BIO_INNER_ERR;
    }

    if (ftruncate(fd, static_cast<off_t>(poolSize)) < 0) {
        CLIENT_LOG_ERROR("ftruncate failed, pid:" << pid << ", size:" << poolSize << ", error:" << strerror(errno));
        shm_unlink(shmName.c_str());
        close(fd);
        return BIO_INNER_ERR;
    }

    auto *address = static_cast<uint8_t *>(mmap(nullptr, poolSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (address == MAP_FAILED) {
        CLIENT_LOG_ERROR("mmap failed, pid:" << pid << ", size:" << poolSize << ", error:" << strerror(errno));
        shm_unlink(shmName.c_str());
        close(fd);
        return BIO_ERR;
    }

    (void)memset_s(address, poolSize, 0, poolSize);
    item = DataMsgMemItem(fd, 0, poolSize, address);
    return BIO_OK;
}
}

InterceptorServer::~InterceptorServer()
{
    ReleaseAllDataMsgMemItems();
}

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
        CLIENT_LOG_ERROR("Invalid interceptor read request, msgLen:" << ctx.MessageDataLen() <<
            ", expectLen:" << sizeof(InterceptorPreadIn) << ", hasData:" <<
            (ctx.MessageData() == nullptr ? "false" : "true") << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorPreadIn *>(ctx.MessageData());
    if (!CheckInterceptorReadReq(req)) {
        CLIENT_LOG_ERROR("Reject interceptor read request, inode:" << req->inode << ", fd:" << req->fd <<
            ", offset:" << req->offset << ", nbytes:" << req->nbytes << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_ASYNC_BEGIN(INTERCEPTOR_READ_START);
    BIO_TRACE_ASYNC_END(INTERCEPTOR_READ_START, 0, req->startTime);
    CLIENT_LOG_DEBUG("Receive interceptor read request, inode:" << req->inode << ", fd:" << req->fd <<
        ", offset:" << req->offset << ", nbytes:" << req->nbytes << ".");

    BIO_TRACE_START(INTERCEPTOR_SMALL_READ);
    uint8_t respBuf[sizeof(InterceptorPreadOut) + MAX_SMALL_WRITE_SIZE] = {};
    auto resp = reinterpret_cast<InterceptorPreadOut *>(respBuf);

    int readLen = static_cast<int>(req->nbytes);
    BIO_TRACE_START(INTERCEPTOR_READ_HOOK);
    auto ret = BioReadHook(req->inode, resp->data, req->nbytes, req->offset, &readLen);
    BIO_TRACE_END(INTERCEPTOR_READ_HOOK, ret);
    if (UNLIKELY(ret != 0)) {
        CLIENT_LOG_ERROR("Read hook failed, inode:" << req->inode << " offset:" << req->offset << " len:" <<
            req->nbytes << " fd:" << req->fd << ", readLen:" << readLen << ", ret:" << ret << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(INTERCEPTOR_SMALL_READ, BIO_ALLOC_FAIL);
        return BIO_OK;
    }

    resp->dataLen = static_cast<uint64_t>(readLen);
    BIO_TRACE_END(INTERCEPTOR_SMALL_READ, 0);

    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(resp),
        sizeof(InterceptorPreadOut) + readLen);
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
        CLIENT_LOG_ERROR("Invalid interceptor write request, msgLen:" << ctx.MessageDataLen() <<
            ", minLen:" << sizeof(InterceptorPwriteIn) << ", hasData:" <<
            (ctx.MessageData() == nullptr ? "false" : "true") << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorPwriteIn *>(ctx.MessageData());
    if ((ctx.MessageDataLen() < (sizeof(InterceptorPwriteIn) + req->nbytes)) || !CheckInterceptorWriteReq(req)) {
        CLIENT_LOG_ERROR("Reject interceptor write request, msgLen:" << ctx.MessageDataLen() << ", inode:" <<
            req->inode << ", fd:" << req->fd << ", offset:" << req->offset << ", nbytes:" << req->nbytes << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_ASYNC_BEGIN(INTERCEPTOR_WRITE_START);
    BIO_TRACE_ASYNC_END(INTERCEPTOR_WRITE_START, 0, req->startTime);
    CLIENT_LOG_DEBUG("Receive interceptor write request, inode:" << req->inode << ", fd:" << req->fd <<
        ", offset:" << req->offset << ", nbytes:" << req->nbytes << ".");

    BIO_TRACE_START(INTERCEPTOR_SMALL_WRITE);
    BIO_TRACE_START(INTERCEPTOR_WRITE_HOOK);
    auto ret = BioWriteHook(req->inode, req->data, req->nbytes, req->offset, 0ULL);
    BIO_TRACE_END(INTERCEPTOR_WRITE_HOOK, ret);
    if (UNLIKELY(ret != 0)) {
        CLIENT_LOG_ERROR("Write hook failed, inode:" << req->inode << ", fd:" << req->fd << ", offset:" <<
            req->offset << ", nbytes:" << req->nbytes << ", ret:" << ret << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(INTERCEPTOR_SMALL_WRITE, BIO_ERR);
        return BIO_OK;
    }
    BIO_TRACE_END(INTERCEPTOR_SMALL_WRITE, 0);

    InterceptorPwriteOut resp;
    resp.ret = 0;

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

void InterceptorServer::ReleaseDataMsgMemItem(uint32_t pid)
{
    DataMsgMemItem item;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mDataMsgMemLock);
        auto iter = mDataMsgMemMgr.find(pid);
        if (iter == mDataMsgMemMgr.end()) {
            return;
        }
        item = iter->second;
        mDataMsgMemMgr.erase(iter);
        found = true;
    }

    if (found) {
        CleanupDataMsgMemItem(pid, item);
        CLIENT_LOG_INFO("Succeed to recycle interceptor data message memory, holder:" << pid << ".");
    }
}

void InterceptorServer::ReleaseAllDataMsgMemItems()
{
    std::vector<std::pair<uint32_t, DataMsgMemItem>> items;
    {
        std::lock_guard<std::mutex> lock(mDataMsgMemLock);
        items.reserve(mDataMsgMemMgr.size());
        for (auto &iter : mDataMsgMemMgr) {
            items.emplace_back(iter.first, iter.second);
        }
        mDataMsgMemMgr.clear();
    }

    for (auto &iter : items) {
        CleanupDataMsgMemItem(iter.first, iter.second);
    }
}

void InterceptorServer::HandleProcBroken(uint32_t pid)
{
    ReleaseDataMsgMemItem(pid);
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
    ReleaseDataMsgMemItem(req->comm.pid);
    DataMsgMemItem item;
    auto ret = CreateDataMsgMemItem(req->comm.pid, poolSize, item);
    if (ret != BIO_OK) {
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
        return BIO_OK;
    }

    auto shmFd = item.shmFd;
    ret = BioClientNet::Instance()->GetNetEngine()->SendFds(ctx.Channel(), &shmFd, NO_1);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send fds failed, ret:" << ret << ", pid:" << req->comm.pid << ".");
        CleanupDataMsgMemItem(req->comm.pid, item);
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ERR, nullptr, 0);
        return BIO_OK;
    }

    {
        std::lock_guard<std::mutex> lock(mDataMsgMemLock);
        mDataMsgMemMgr[req->comm.pid] = item;
    }

    CLIENT_LOG_INFO("Succeed to create interceptor data message memory pool, size:" << poolSize <<
        ", blockSize:" << blockSize << ", holder:" << req->comm.pid << ".");

    InterceptorCreateDataMsgMemPoolResponse rsp;
    rsp.memFd = item.shmFd;
    rsp.offset = item.offset;
    rsp.poolSize = poolSize;
    rsp.blockSize = blockSize;
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &rsp, sizeof(InterceptorCreateDataMsgMemPoolResponse));
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorLargeWrite(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorLargePwriteIn)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Invalid interceptor large write request, msgLen:" << ctx.MessageDataLen() <<
            ", expectLen:" << sizeof(InterceptorLargePwriteIn) << ", hasData:" <<
            (ctx.MessageData() == nullptr ? "false" : "true") << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorLargePwriteIn *>(ctx.MessageData());
    if (!CheckInterceptorLargeWriteReq(req)) {
        CLIENT_LOG_ERROR("Reject interceptor large write request, inode:" << req->inode << ", fd:" << req->fd <<
            ", offset:" << req->offset << ", nbytes:" << req->nbytes << ", mrOffset:" << req->mrOffset <<
            ", pid:" << req->pid << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_ASYNC_BEGIN(INTERCEPTOR_WRITE_START);
    BIO_TRACE_ASYNC_END(INTERCEPTOR_WRITE_START, 0, req->startTime);
    CLIENT_LOG_DEBUG("Receive interceptor large write request, inode:" << req->inode << ", fd:" << req->fd <<
        ", offset:" << req->offset << ", nbytes:" << req->nbytes << ", mrOffset:" << req->mrOffset <<
        ", pid:" << req->pid << ".");

    uint8_t *srcAddr = TransDataMsgMemAddr(req->pid, req->mrOffset);
    if (UNLIKELY(srcAddr == nullptr)) {
        CLIENT_LOG_ERROR("Get data msg mem address failed, pid:" << req->pid << ", mrOffset:" << req->mrOffset << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(INTERCEPTOR_WRITE_HOOK);
    int32_t writeRet = static_cast<int32_t>(BioWriteHook(req->inode, reinterpret_cast<char *>(srcAddr), req->nbytes,
                                                         req->offset, 0ULL));
    BIO_TRACE_END(INTERCEPTOR_WRITE_HOOK, writeRet);
    if (UNLIKELY(writeRet != 0)) {
        CLIENT_LOG_ERROR("Large write hook failed, inode:" << req->inode << ", fd:" << req->fd << ", offset:" <<
            req->offset << ", nbytes:" << req->nbytes << ", mrOffset:" << req->mrOffset << ", pid:" << req->pid <<
            ", ret:" << writeRet << ".");
        InterceptorPwriteOut resp;
        resp.ret = writeRet;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        return BIO_OK;
    }

    InterceptorPwriteOut resp;
    resp.ret = 0;
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPwriteOut));
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorLargeRead(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorLargePreadIn)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Invalid interceptor large read request, msgLen:" << ctx.MessageDataLen() <<
            ", expectLen:" << sizeof(InterceptorLargePreadIn) << ", hasData:" <<
            (ctx.MessageData() == nullptr ? "false" : "true") << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorLargePreadIn *>(ctx.MessageData());
    if (!CheckInterceptorLargeReadReq(req)) {
        CLIENT_LOG_ERROR("Reject interceptor large read request, inode:" << req->inode << ", fd:" << req->fd <<
            ", offset:" << req->offset << ", nbytes:" << req->nbytes << ", mrOffset:" << req->mrOffset <<
            ", pid:" << req->pid << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_ASYNC_BEGIN(INTERCEPTOR_READ_START);
    BIO_TRACE_ASYNC_END(INTERCEPTOR_READ_START, 0, req->startTime);
    CLIENT_LOG_DEBUG("Receive interceptor large read request, inode:" << req->inode << ", fd:" << req->fd <<
        ", offset:" << req->offset << ", nbytes:" << req->nbytes << ", mrOffset:" << req->mrOffset <<
        ", pid:" << req->pid << ".");
    uint8_t *shmAddr = TransDataMsgMemAddr(req->pid, req->mrOffset);
    if (UNLIKELY(shmAddr == nullptr)) {
        CLIENT_LOG_ERROR("Get data msg mem address failed, pid:" << req->pid << ", mrOffset:" << req->mrOffset << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }

    int readLen = 0;
    BIO_TRACE_START(INTERCEPTOR_READ_HOOK);
    int ret = BioReadHook(req->inode, reinterpret_cast<char *>(shmAddr), req->nbytes, req->offset, &readLen);
    BIO_TRACE_END(INTERCEPTOR_READ_HOOK, ret);
    if (UNLIKELY(ret != 0)) {
        CLIENT_LOG_ERROR("Large read hook failed, inode:" << req->inode << ", fd:" << req->fd << ", offset:" <<
            req->offset << ", nbytes:" << req->nbytes << ", mrOffset:" << req->mrOffset << ", pid:" << req->pid <<
            ", readLen:" << readLen << ", ret:" << ret << ".");
        InterceptorLargePreadOut resp;
        resp.ret = ret;
        resp.dataLen = 0;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
            sizeof(InterceptorLargePreadOut));
        return BIO_OK;
    }

    InterceptorLargePreadOut resp;
    resp.ret = 0;
    resp.dataLen = static_cast<uint64_t>(readLen);
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorLargePreadOut));
    return BIO_OK;
}

BResult InterceptorServer::Initialize()
{
    auto ret = RegisterOpcode();
    if (ret != BIO_OK) {
        return ret;
    }

    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    if (netEngine == nullptr) {
        CLIENT_LOG_ERROR("Get net engine failed when register interceptor broken handler.");
        return BIO_NOT_READY;
    }

    ret = netEngine->RegisterInnerChannelBrokenHandler(
        [this](uint32_t, uint32_t pid) {
            if (pid != 0) {
                HandleProcBroken(pid);
            }
        });
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor broken handler failed, ret:" << ret << ".");
        return ret;
    }
    CLIENT_LOG_INFO("Initialize interceptor server success.");
    return BIO_OK;
}
