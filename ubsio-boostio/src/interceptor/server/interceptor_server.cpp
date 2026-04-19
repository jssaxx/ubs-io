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
#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#ifdef __linux__
#include <linux/perf_event.h>
#endif
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
struct ThreadPerfSnapshot {
    int cpu = -1;
    int node = -1;
    int64_t minflt = 0;
    int64_t majflt = 0;
    int64_t nvcsw = 0;
    int64_t nivcsw = 0;
    uint64_t cacheMisses = 0;
    bool cacheMissesValid = false;
};

static uint64_t PerfDelta(uint64_t begin, uint64_t end)
{
    return end >= begin ? (end - begin) : 0;
}

static int64_t PerfDelta(int64_t begin, int64_t end)
{
    return end >= begin ? (end - begin) : 0;
}

static pid_t GetCurrentTid()
{
#ifdef SYS_gettid
    return static_cast<pid_t>(syscall(SYS_gettid));
#else
    return getpid();
#endif
}

static bool ReadThreadCacheMisses(uint64_t &value)
{
#if defined(__linux__) && defined(__NR_perf_event_open)
    static thread_local int perfFd = -2;
    if (perfFd == -2) {
        perf_event_attr attr {};
        attr.size = sizeof(attr);
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_CACHE_MISSES;
        attr.disabled = 0;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        perfFd = static_cast<int>(syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0));
    }
    if (perfFd < 0) {
        return false;
    }
    return read(perfFd, &value, sizeof(value)) == static_cast<ssize_t>(sizeof(value));
#else
    (void)value;
    return false;
#endif
}

static ThreadPerfSnapshot CaptureThreadPerfSnapshot()
{
    ThreadPerfSnapshot snapshot;
#if defined(__linux__) && defined(SYS_getcpu)
    unsigned cpu = 0;
    unsigned node = 0;
    if (syscall(SYS_getcpu, &cpu, &node, nullptr) == 0) {
        snapshot.cpu = static_cast<int>(cpu);
        snapshot.node = static_cast<int>(node);
    } else {
        snapshot.cpu = sched_getcpu();
    }
#else
    snapshot.cpu = sched_getcpu();
#endif

#ifdef RUSAGE_THREAD
    rusage usage {};
    if (getrusage(RUSAGE_THREAD, &usage) == 0) {
        snapshot.minflt = usage.ru_minflt;
        snapshot.majflt = usage.ru_majflt;
        snapshot.nvcsw = usage.ru_nvcsw;
        snapshot.nivcsw = usage.ru_nivcsw;
    }
#endif

    snapshot.cacheMissesValid = ReadThreadCacheMisses(snapshot.cacheMisses);
    return snapshot;
}
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
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_INIT_BIO_SHM,
        std::bind(&InterceptorServer::HandleInterceptorInitBioShm, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor init bio shm message handle failed, ret:" << ret << ".");
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_ALLOC_CACHE_SPACE,
        std::bind(&InterceptorServer::HandleInterceptorAllocCacheSpace, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor alloc cache space message handle failed, ret:" << ret << ".");
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

    BIO_TRACE_ASYNC_BEGIN(INTERCEPTOR_READ_START);
    BIO_TRACE_ASYNC_END(INTERCEPTOR_READ_START, 0, req->startTime);

    CLIENT_LOG_DEBUG("Receive interceptor read message inode:" << req->inode << " offset:" << req->offset << " len:" <<
        req->nbytes << " fd:" << req->fd);

    BIO_TRACE_START(INTERCEPTOR_SMALL_READ);
    auto resp = static_cast<InterceptorPreadOut *>(malloc(sizeof(InterceptorPreadOut) + req->nbytes));
    if (UNLIKELY(resp == nullptr)) {
        CLIENT_LOG_ERROR("Alloc memory failed, inode:" << req->inode << " offset:" << req->offset << " len:" <<
            req->nbytes << " fd:" << req->fd << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(INTERCEPTOR_SMALL_READ, BIO_ALLOC_FAIL);
        return BIO_OK;
    }

    int readLen = static_cast<int>(req->nbytes);
    BIO_TRACE_START(INTERCEPTOR_READ_HOOK);
    auto ret = BioReadHook(req->inode, resp->data, req->nbytes, req->offset, &readLen);
    BIO_TRACE_END(INTERCEPTOR_READ_HOOK, ret);
    if (UNLIKELY(ret != 0)) {
        CLIENT_LOG_ERROR("Read hook failed, inode:" << req->inode << " offset:" << req->offset << " len:" <<
            req->nbytes << " fd:" << req->fd << ", readLen:" << readLen << ", ret:" << ret << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_ALLOC_FAIL, nullptr, 0);
        BIO_TRACE_END(INTERCEPTOR_SMALL_READ, BIO_ALLOC_FAIL);
        free(resp);
        resp = nullptr;
        return BIO_OK;
    }

    resp->dataLen = static_cast<uint64_t>(readLen);
    BIO_TRACE_END(INTERCEPTOR_SMALL_READ, 0);

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

    BIO_TRACE_ASYNC_BEGIN(INTERCEPTOR_WRITE_START);
    BIO_TRACE_ASYNC_END(INTERCEPTOR_WRITE_START, 0, req->startTime);
    auto totalBegin = Monotonic::TimeNs();
    auto perf0 = CaptureThreadPerfSnapshot();

    CLIENT_LOG_DEBUG("Receive interceptor write message inode:" << req->inode << " offset:" << req->offset << " len:" <<
        req->nbytes << " fd:" << req->fd);

    BIO_TRACE_START(INTERCEPTOR_SMALL_WRITE);
    BIO_TRACE_START(INTERCEPTOR_WRITE_HOOK);
    auto ret = BioWriteHook(req->inode, req->data, req->nbytes, req->offset, 0ULL);
    BIO_TRACE_END(INTERCEPTOR_WRITE_HOOK, ret);
    if (UNLIKELY(ret != 0)) {
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

static bool TranslateCopyFreeSpace(CacheSpaceDesc &spaceInfo, uint8_t *bioShmBase, uint64_t bioShmLength,
    uint64_t expectedLen)
{
    if (bioShmBase == nullptr || spaceInfo.addressNum == 0 || spaceInfo.addressNum > CACHE_SPACE_ADDRESS_SIZE) {
        return false;
    }

    uint64_t totalLen = 0;
    for (uint32_t i = 0; i < spaceInfo.addressNum; i++) {
        uint64_t offset = spaceInfo.address[i].address;
        uint64_t size = spaceInfo.address[i].size;
        if (size == 0 || offset >= bioShmLength || size > bioShmLength - offset) {
            return false;
        }
        totalLen += size;
        spaceInfo.address[i].address = reinterpret_cast<uint64_t>(bioShmBase + offset);
    }

    return totalLen == expectedLen;
}

uint8_t *InterceptorServer::TransDataMsgMemAddr(uint32_t pid, uint64_t mrOffset, uint64_t length)
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

    if (UNLIKELY(length == 0 || length > iter->second.size - mrOffset)) {
        CLIENT_LOG_ERROR("Trans data msg mem addr failed, mrOffset:" << mrOffset << ", length:" << length <<
            ", size:" << iter->second.size << ".");
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
    shm_unlink(shmName.c_str());

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

    BIO_TRACE_ASYNC_BEGIN(INTERCEPTOR_WRITE_START);
    BIO_TRACE_ASYNC_END(INTERCEPTOR_WRITE_START, 0, req->startTime);
    auto totalBegin = Monotonic::TimeNs();
    auto perf0 = CaptureThreadPerfSnapshot();

    CLIENT_LOG_DEBUG("Receive interceptor large write message inode:" << req->inode << " offset:" << req->offset <<
        " len:" << req->nbytes << " fd:" << req->fd);

    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    int32_t tmpFd = -1;
    uint64_t tmpOff = 0;
    uint64_t tmpLen = 0;
    uint64_t tmpKey = 0;
    uint8_t *bioShmBase = nullptr;
    if (netEngine->QueryShmInfo(tmpFd, tmpOff, tmpLen, tmpKey) == BIO_OK && tmpLen > 0) {
        bioShmBase = netEngine->GetShmAddress(tmpOff, tmpLen);
    }
    if (UNLIKELY(bioShmBase == nullptr)) {
        CLIENT_LOG_ERROR("Bio shm base address is null.");
        netEngine->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(INTERCEPTOR_WRITE_HOOK);
    CacheSpaceDesc addressInfo = req->spaceInfo;
    if (UNLIKELY(!TranslateCopyFreeSpace(addressInfo, bioShmBase, tmpLen, req->nbytes))) {
        CLIENT_LOG_ERROR("Invalid copy-free address info, inode:" << req->inode << ", offset:" << req->offset <<
            ", len:" << req->nbytes << ", addressNum:" << addressInfo.addressNum << ".");
        netEngine->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_START(INTERCEPTOR_WRITE_COPYFREE);
    auto hookBegin = Monotonic::TimeNs();
    int32_t writeRet = static_cast<int32_t>(BioWriteCopyFreeHook(req->inode, req->offset, req->nbytes, &addressInfo));
    auto hookEnd = Monotonic::TimeNs();
    BIO_TRACE_END(INTERCEPTOR_WRITE_COPYFREE, writeRet);
    BIO_TRACE_END(INTERCEPTOR_WRITE_HOOK, writeRet);
    auto totalEnd = Monotonic::TimeNs();
    auto perf4 = CaptureThreadPerfSnapshot();
    static thread_local uint64_t sCount = 0;
    static thread_local uint64_t sHookUs = 0;
    static thread_local uint64_t sTotalUs = 0;
    static thread_local uint64_t sAddressNum = 0;
    static thread_local uint64_t sMinflt = 0;
    static thread_local uint64_t sMajflt = 0;
    static thread_local uint64_t sNvcsw = 0;
    static thread_local uint64_t sNivcsw = 0;
    static thread_local uint64_t sCacheMisses = 0;
    static thread_local uint64_t sCpuMigrate = 0;
    static thread_local uint64_t sNodeMigrate = 0;
    static thread_local int sLastStartCpu = -1;
    static thread_local int sLastStartNode = -1;
    static thread_local int sLastEndCpu = -1;
    static thread_local int sLastEndNode = -1;
    sCount++;
    sHookUs += (hookEnd - hookBegin) / 1000;
    sTotalUs += (totalEnd - totalBegin) / 1000;
    sAddressNum += req->spaceInfo.addressNum;
    sMinflt += static_cast<uint64_t>(PerfDelta(perf0.minflt, perf4.minflt));
    sMajflt += static_cast<uint64_t>(PerfDelta(perf0.majflt, perf4.majflt));
    sNvcsw += static_cast<uint64_t>(PerfDelta(perf0.nvcsw, perf4.nvcsw));
    sNivcsw += static_cast<uint64_t>(PerfDelta(perf0.nivcsw, perf4.nivcsw));
    if (perf0.cacheMissesValid && perf4.cacheMissesValid) {
        sCacheMisses += PerfDelta(perf0.cacheMisses, perf4.cacheMisses);
    }
    sCpuMigrate += (perf0.cpu >= 0 && perf4.cpu >= 0 && perf0.cpu != perf4.cpu) ? 1 : 0;
    sNodeMigrate += (perf0.node >= 0 && perf4.node >= 0 && perf0.node != perf4.node) ? 1 : 0;
    sLastStartCpu = perf0.cpu;
    sLastStartNode = perf0.node;
    sLastEndCpu = perf4.cpu;
    sLastEndNode = perf4.node;
    if (sCount >= 1000) {
        CLIENT_LOG_INFO("HandleInterceptorLargeWrite avg latency(us) over " << sCount <<
            " io: hook=" << sHookUs / sCount <<
            " total=" << sTotalUs / sCount <<
            " addressNum=" << sAddressNum / sCount <<
            " minflt=" << sMinflt / sCount <<
            " majflt=" << sMajflt / sCount <<
            " nvcsw=" << sNvcsw / sCount <<
            " nivcsw=" << sNivcsw / sCount <<
            " cacheMisses=" << sCacheMisses / sCount <<
            " cpuMigrate=" << sCpuMigrate <<
            " nodeMigrate=" << sNodeMigrate <<
            " lastCpu=" << sLastStartCpu << "->" << sLastEndCpu <<
            " lastNode=" << sLastStartNode << "->" << sLastEndNode <<
            " tid=" << GetCurrentTid());
        sCount = 0;
        sHookUs = 0;
        sTotalUs = 0;
        sAddressNum = 0;
        sMinflt = 0;
        sMajflt = 0;
        sNvcsw = 0;
        sNivcsw = 0;
        sCacheMisses = 0;
        sCpuMigrate = 0;
        sNodeMigrate = 0;
    }
    if (UNLIKELY(writeRet != 0)) {
        InterceptorPwriteOut resp;
        resp.ret = writeRet;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
            sizeof(InterceptorPwriteOut));
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

    uint8_t *shmAddr = TransDataMsgMemAddr(req->pid, req->mrOffset, req->nbytes);
    if (UNLIKELY(shmAddr == nullptr)) {
        CLIENT_LOG_ERROR("Get data msg mem address failed, pid:" << req->pid << ", mrOffset:" << req->mrOffset << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INNER_ERR, nullptr, 0);
        return BIO_OK;
    }

    int readLen = 0;
    BIO_TRACE_START(INTERCEPTOR_READ_HOOK);
    CacheSpaceDesc spaceInfo {};
    spaceInfo.addressNum = 1;
    spaceInfo.address[0].address = reinterpret_cast<uint64_t>(shmAddr);
    spaceInfo.address[0].size = static_cast<uint32_t>(req->nbytes);

    int ret = BioReadCopyFreeHook(req->inode, req->offset, req->nbytes, &spaceInfo, &readLen);
    if (ret == RET_CACHE_NOT_SUPPORTED) {
        ret = BioReadHook(req->inode, reinterpret_cast<char *>(shmAddr), req->nbytes, req->offset, &readLen);
    }
    BIO_TRACE_END(INTERCEPTOR_READ_HOOK, ret);
    if (UNLIKELY(ret != 0 || readLen < 0)) {
        CLIENT_LOG_ERROR("Read hook failed, ret:" << ret << ", readLen:" << readLen << ".");
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
    return RegisterOpcode();
}

BResult InterceptorServer::HandleInterceptorInitBioShm(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorInitBioShmReq)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive init bio shm message len:" << ctx.MessageDataLen() << " or message data invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorInitBioShmReq *>(ctx.MessageData());

    int32_t bioShmFd = -1;
    uint64_t bioShmOffset = 0;
    uint64_t bioShmLength = 0;
    uint64_t bioShmKey = 0;
    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    auto queryRet = netEngine->QueryShmInfo(bioShmFd, bioShmOffset, bioShmLength, bioShmKey);
    if (UNLIKELY(queryRet != BIO_OK || bioShmFd < 0 || bioShmLength == 0)) {
        CLIENT_LOG_ERROR("Bio shm not available, queryRet:" << queryRet << ", fd:" << bioShmFd <<
            ", length:" << bioShmLength << ".");
        InterceptorInitBioShmResp resp;
        resp.ret = -1;
        resp.offset = 0;
        resp.length = 0;
        netEngine->Reply(ctx, BIO_OK, &resp, sizeof(InterceptorInitBioShmResp));
        return BIO_OK;
    }

    auto ret = netEngine->SendFds(ctx.Channel(), &bioShmFd, NO_1);
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Send bio shm fd failed, ret:" << ret << ".");
        netEngine->Reply(ctx, BIO_ERR, nullptr, 0);
        return BIO_OK;
    }

    InterceptorInitBioShmResp resp;
    resp.ret = 0;
    resp.offset = bioShmOffset;
    resp.length = bioShmLength;
    netEngine->Reply(ctx, BIO_OK, &resp, sizeof(InterceptorInitBioShmResp));
    CLIENT_LOG_INFO("Init bio shm for pid:" << req->pid << ", offset:" << bioShmOffset <<
        ", length:" << bioShmLength << ".");
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorAllocCacheSpace(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorAllocCacheSpaceReq)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Receive alloc cache space message len:" << ctx.MessageDataLen() <<
            " or message data invalid.");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorAllocCacheSpaceReq *>(ctx.MessageData());

    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    int32_t tmpFd = -1;
    uint64_t bioShmOffset = 0;
    uint64_t bioShmLength = 0;
    uint64_t tmpKey = 0;
    if (netEngine->QueryShmInfo(tmpFd, bioShmOffset, bioShmLength, tmpKey) != BIO_OK || bioShmLength == 0) {
        CLIENT_LOG_ERROR("Bio shm not available for alloc cache space.");
        InterceptorAllocCacheSpaceResp resp;
        resp.ret = -1;
        netEngine->Reply(ctx, BIO_OK, &resp, sizeof(InterceptorAllocCacheSpaceResp));
        return BIO_OK;
    }

    uint64_t tenantId = 1;
    static std::atomic<uint64_t> objectId{1};
    CacheSpaceDesc spaceInfo{};
    spaceInfo.allocLoc = 1;
    auto allocRet = BioAllocCacheSpace(tenantId, objectId.fetch_add(1), req->nbytes, &spaceInfo);
    if (UNLIKELY(allocRet != RET_CACHE_OK)) {
        CLIENT_LOG_ERROR("Alloc cache space failed, ret:" << allocRet << ".");
        InterceptorAllocCacheSpaceResp resp;
        resp.ret = -1;
        netEngine->Reply(ctx, BIO_OK, &resp, sizeof(InterceptorAllocCacheSpaceResp));
        return BIO_OK;
    }

    uint8_t *bioShmBase = netEngine->GetShmAddress(bioShmOffset, bioShmLength);
    if (UNLIKELY(bioShmBase == nullptr)) {
        CLIENT_LOG_ERROR("Bio shm base address is null.");
        InterceptorAllocCacheSpaceResp resp;
        resp.ret = -1;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &resp, sizeof(InterceptorAllocCacheSpaceResp));
        return BIO_OK;
    }

    for (uint32_t i = 0; i < spaceInfo.addressNum; i++) {
        spaceInfo.address[i].address = spaceInfo.address[i].address - reinterpret_cast<uint64_t>(bioShmBase);
    }
    for (uint32_t i = spaceInfo.addressNum; i < CACHE_SPACE_ADDRESS_SIZE; i++) {
        spaceInfo.address[i].address = 0;
        spaceInfo.address[i].size = 0;
    }

    InterceptorAllocCacheSpaceResp resp;
    resp.ret = 0;
    resp.spaceInfo = spaceInfo;
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &resp, sizeof(InterceptorAllocCacheSpaceResp));
    return BIO_OK;
}
