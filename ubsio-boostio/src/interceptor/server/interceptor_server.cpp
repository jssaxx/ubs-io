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

#include <string>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>
#include "bio_client.h"
#include "bio_client_agent.h"
#include "bio_client_log.h"
#include "bio_client_net.h"
#include "bio_c.h"
#include "bio_monotonic.h"
#include "bio_trace.h"
#include "flow_type.h"
#include "message.h"
#include "message_op.h"
#include "interceptor_server.h"

using namespace ock::bio;
using namespace ock::bio::net;

bool FillReadRespFromBuffer(uint64_t addrOffset, uint64_t dataLen, uint64_t windowOffset, uint64_t windowDataLen,
    uint64_t windowAddrOffset, uint64_t windowAddrLen, InterceptorPreadOut &resp)
{
    if (dataLen == 0) {
        return false;
    }
    resp = {};
    resp.ret = 0;
    resp.dataLen = dataLen;
    resp.addrNum = 1;
    resp.dataSource = INTERCEPTOR_PREAD_DATA_READ_SHM;
    resp.addrOffset[0] = addrOffset;
    resp.addrLen[0] = dataLen;
    resp.windowOffset = windowOffset;
    resp.windowDataLen = windowDataLen;
    resp.windowAddrOffset = windowAddrOffset;
    resp.windowAddrLen = windowAddrLen;
    return true;
}

bool IsReadIndexSameEntry(const InterceptorReadIndexEntry &entry, uint64_t inode, uint64_t offset,
    const InterceptorPreadOut &resp)
{
    return entry.inode == inode && entry.fileOffset == offset && entry.dataLen == resp.dataLen &&
        entry.addrNum == resp.addrNum;
}

bool IsReadIndexEmptyEntry(const InterceptorReadIndexEntry &entry)
{
    uint64_t seq = __atomic_load_n(&entry.seq, __ATOMIC_RELAXED);
    return (seq & 1U) == 0 && (seq == 0 || entry.dataLen == 0 || entry.addrNum == 0);
}

void PublishReadIndexEntry(InterceptorReadIndexEntry &entry, uint64_t inode, uint64_t offset,
    const InterceptorPreadOut &resp)
{
    uint64_t oldSeq = __atomic_load_n(&entry.seq, __ATOMIC_RELAXED);
    uint64_t oddSeq = (oldSeq + 1U) | 1U;
    uint64_t evenSeq = oddSeq + 1U;

    __atomic_store_n(&entry.seq, oddSeq, __ATOMIC_RELEASE);
    entry.inode = inode;
    entry.fileOffset = offset;
    entry.dataLen = resp.dataLen;
    entry.addrNum = resp.addrNum;
    entry.reserved = 0;
    for (uint32_t idx = 0; idx < SLICE_ADDR_SIZE; ++idx) {
        entry.addrOffset[idx] = (idx < resp.addrNum) ? resp.addrOffset[idx] : 0;
        entry.addrLen[idx] = (idx < resp.addrNum) ? resp.addrLen[idx] : 0;
    }
    __atomic_store_n(&entry.seq, evenSeq, __ATOMIC_RELEASE);
}

void ClearReadIndexEntry(InterceptorReadIndexEntry &entry, uint64_t seq)
{
    uint64_t oddSeq = (seq + 1U) | 1U;
    uint64_t evenSeq = oddSeq + 1U;

    __atomic_store_n(&entry.seq, oddSeq, __ATOMIC_RELEASE);
    entry.inode = 0;
    entry.fileOffset = 0;
    entry.dataLen = 0;
    entry.addrNum = 0;
    entry.reserved = 0;
    for (uint32_t idx = 0; idx < SLICE_ADDR_SIZE; ++idx) {
        entry.addrOffset[idx] = 0;
        entry.addrLen[idx] = 0;
    }
    __atomic_store_n(&entry.seq, evenSeq, __ATOMIC_RELEASE);
}

InterceptorReadIndexEntry &SelectReadIndexEntry(InterceptorReadIndexEntry *entries, uint64_t hash, uint64_t inode,
    uint64_t offset, const InterceptorPreadOut &resp)
{
    uint32_t base = InterceptorReadIndexBucketBase(hash);
    InterceptorReadIndexEntry *emptyEntry = nullptr;
    for (uint32_t way = 0; way < INTERCEPTOR_READ_INDEX_BUCKET_WAY; ++way) {
        auto &entry = entries[base + way];
        uint64_t seq = __atomic_load_n(&entry.seq, __ATOMIC_RELAXED);
        if ((seq & 1U) == 0 && IsReadIndexSameEntry(entry, inode, offset, resp)) {
            return entry;
        }
        if (emptyEntry == nullptr && IsReadIndexEmptyEntry(entry)) {
            emptyEntry = &entry;
        }
    }

    if (emptyEntry != nullptr) {
        return *emptyEntry;
    }
    return entries[base + static_cast<uint32_t>((hash >> INTERCEPTOR_HASH_HIGH_SHIFT) %
        INTERCEPTOR_READ_INDEX_BUCKET_WAY)];
}

}

InterceptorServer::~InterceptorServer()
{
    AbortPreparedDirectWriteRecords(TakeAllPreparedDirectWrites());
    CleanupReadBuffers();
    CleanupReadIndex();
}

BResult InterceptorServer::EnsureReadIndex()
{
    std::lock_guard<std::mutex> lock(mReadIndexLock);
    if (mReadIndex != nullptr && mReadIndexFd >= 0 && mReadIndexLength > 0) {
        return BIO_OK;
    }

    std::string shmName = "/interceptor_read_index_" + std::to_string(getpid());
    shm_unlink(shmName.c_str());

    int fd = shm_open(shmName.c_str(), O_CREAT | O_RDWR | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        CLIENT_LOG_ERROR("Create read index shm failed, name:" << shmName << ", error:" << strerror(errno) << ".");
        return BIO_INNER_ERR;
    }

    uint64_t indexLength = InterceptorReadIndexLength();
    if (ftruncate(fd, static_cast<off_t>(indexLength)) < 0) {
        CLIENT_LOG_ERROR("Resize read index shm failed, name:" << shmName << ", size:" << indexLength <<
            ", error:" << strerror(errno) << ".");
        shm_unlink(shmName.c_str());
        close(fd);
        return BIO_INNER_ERR;
    }

    auto *addr = static_cast<InterceptorReadIndexHeader *>(mmap(nullptr, indexLength, PROT_READ | PROT_WRITE,
        MAP_SHARED, fd, 0));
    if (addr == MAP_FAILED) {
        CLIENT_LOG_ERROR("Map read index shm failed, name:" << shmName << ", size:" << indexLength <<
            ", error:" << strerror(errno) << ".");
        shm_unlink(shmName.c_str());
        close(fd);
        return BIO_ERR;
    }

    (void)memset_s(addr, indexLength, 0, indexLength);
    addr->magic = INTERCEPTOR_READ_INDEX_MAGIC;
    addr->version = INTERCEPTOR_READ_INDEX_VERSION;
    addr->entryCount = INTERCEPTOR_READ_INDEX_ENTRY_COUNT;
    addr->entrySize = sizeof(InterceptorReadIndexEntry);

    mReadIndexFd = fd;
    mReadIndexLength = indexLength;
    mReadIndex = addr;
    if (shm_unlink(shmName.c_str()) != 0) {
        CLIENT_LOG_WARN("Unlink read index shm name failed, name:" << shmName << ", error:" << strerror(errno) << ".");
    }
    CLIENT_LOG_INFO("Create interceptor read index shm success, fd:" << fd << ", size:" << indexLength <<
        ", entryCount:" << INTERCEPTOR_READ_INDEX_ENTRY_COUNT << ", bucketWay:" <<
        INTERCEPTOR_READ_INDEX_BUCKET_WAY << ".");
    return BIO_OK;
}

void InterceptorServer::CleanupReadIndex()
{
    std::lock_guard<std::mutex> lock(mReadIndexLock);
    CleanupReadIndexLocked();
}

void InterceptorServer::CleanupReadIndexLocked()
{
    if (mReadIndex != nullptr && mReadIndexLength > 0) {
        if (munmap(mReadIndex, mReadIndexLength) == -1) {
            CLIENT_LOG_ERROR("Munmap read index shm failed, error:" << strerror(errno) << ".");
        }
        mReadIndex = nullptr;
    }
    if (mReadIndexFd >= 0) {
        close(mReadIndexFd);
        mReadIndexFd = -1;
    }
    mReadIndexLength = 0;
    std::string shmName = "/interceptor_read_index_" + std::to_string(getpid());
    shm_unlink(shmName.c_str());
}

void InterceptorServer::PublishReadIndex(uint64_t inode, uint64_t offset, const InterceptorPreadOut &resp)
{
    BIO_TRACE_START(INTERCEPTOR_READ_INDEX_PUBLISH);
    std::lock_guard<std::mutex> lock(mReadIndexLock);
    if (mReadIndex == nullptr || resp.ret != 0 || resp.dataLen == 0 || resp.addrNum == 0 ||
        resp.addrNum > SLICE_ADDR_SIZE) {
        BIO_TRACE_END(INTERCEPTOR_READ_INDEX_PUBLISH, BIO_NOT_READY);
        return;
    }

    auto *entries = reinterpret_cast<InterceptorReadIndexEntry *>(mReadIndex + 1);
    uint64_t startBlock = InterceptorReadIndexBlockOffset(offset);
    uint64_t lastOffset = offset + resp.dataLen - 1U;
    if (lastOffset < offset) {
        lastOffset = UINT64_MAX;
    }
    uint64_t endBlock = InterceptorReadIndexBlockOffset(lastOffset);

    for (uint64_t blockOffset = startBlock; blockOffset <= endBlock; blockOffset += MAX_INTERCEPTOR_IO_SIZE) {
        uint64_t hash = InterceptorReadIndexHash(inode, blockOffset);
        auto &entry = SelectReadIndexEntry(entries, hash, inode, offset, resp);
        PublishReadIndexEntry(entry, inode, offset, resp);

        if (blockOffset > UINT64_MAX - MAX_INTERCEPTOR_IO_SIZE) {
            break;
        }
    }
    BIO_TRACE_END(INTERCEPTOR_READ_INDEX_PUBLISH, BIO_OK);
}

void InterceptorServer::InvalidateReadIndex(uint64_t inode, uint64_t offset, uint64_t length)
{
    BIO_TRACE_START(INTERCEPTOR_WRITE_INDEX_INVALIDATE);
    std::lock_guard<std::mutex> lock(mReadIndexLock);
    if (mReadIndex == nullptr || length == 0) {
        BIO_TRACE_END(INTERCEPTOR_WRITE_INDEX_INVALIDATE, BIO_NOT_READY);
        return;
    }

    uint64_t startBlock = InterceptorReadIndexBlockOffset(offset);
    uint64_t lastOffset = offset + length - 1U;
    if (lastOffset < offset) {
        lastOffset = UINT64_MAX;
    }
    uint64_t endBlock = InterceptorReadIndexBlockOffset(lastOffset);
    uint64_t writeEnd = offset + length;
    if (writeEnd < offset) {
        writeEnd = UINT64_MAX;
    }
    auto *entries = reinterpret_cast<InterceptorReadIndexEntry *>(mReadIndex + 1);

    for (uint64_t blockOffset = startBlock; blockOffset <= endBlock; blockOffset += MAX_INTERCEPTOR_IO_SIZE) {
        uint64_t hash = InterceptorReadIndexHash(inode, blockOffset);
        uint32_t base = InterceptorReadIndexBucketBase(hash);
        for (uint32_t way = 0; way < INTERCEPTOR_READ_INDEX_BUCKET_WAY; ++way) {
            auto &entry = entries[base + way];
            uint64_t seq = __atomic_load_n(&entry.seq, __ATOMIC_ACQUIRE);
            if ((seq & 1U) != 0 || entry.inode != inode || entry.dataLen == 0) {
                continue;
            }

            uint64_t entryStart = entry.fileOffset;
            uint64_t entryEnd = entryStart + entry.dataLen;
            if (entryEnd < entryStart) {
                entryEnd = UINT64_MAX;
            }
            if (entryEnd > offset && writeEnd > entryStart) {
                ClearReadIndexEntry(entry, seq);
            }
        }

        if (blockOffset > UINT64_MAX - MAX_INTERCEPTOR_IO_SIZE) {
            break;
        }
    }
    BIO_TRACE_END(INTERCEPTOR_WRITE_INDEX_INVALIDATE, BIO_OK);
}

void InterceptorServer::BroadcastReadIndexInvalidate(uint64_t inode, uint64_t offset, uint64_t length)
{
    auto mirror = BioClient::Instance()->GetMirror();
    if (mirror == nullptr || length == 0) {
        return;
    }

    uint16_t localNid = mirror->GetLocalNodeInfo().VNodeId();
    InterceptorReadIndexInvalidateIn req{};
    req.inode = inode;
    req.offset = offset;
    req.length = length;
    req.broadcastRemote = 0;

    auto nodeView = mirror->GetNodeView();
    for (const auto &node : nodeView) {
        uint16_t nodeId = node.first.nodeId;
        if (nodeId == localNid || node.second.status == CM_NODE_FAULT) {
            continue;
        }
        BResult ret = BioClientNet::Instance()->SendAsync<InterceptorReadIndexInvalidateIn>(
            static_cast<BioNodeId>(nodeId), BIO_OP_INTERCEPTOR_READ_INDEX_INVALIDATE, req);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Send read-index invalidate failed, dstNid:" << nodeId << ", inode:" << inode <<
                ", offset:" << offset << ", length:" << length << ", ret:" << ret << ".");
        }
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
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_WRITE_PREPARE_SPACE,
        std::bind(&InterceptorServer::HandleInterceptorWritePrepareSpace, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor write prepare space message handle failed, ret:" << ret << ".");
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_WRITE_COMMIT_SPACE,
        std::bind(&InterceptorServer::HandleInterceptorWriteCommitSpace, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor write commit space message handle failed, ret:" << ret << ".");
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_BIO_SHM_INIT,
        std::bind(&InterceptorServer::HandleInterceptorBioShmInit, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor bio shm init message handle failed, ret:" << ret << ".");
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_READ_INDEX_INVALIDATE,
        std::bind(&InterceptorServer::HandleInterceptorReadIndexInvalidate, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor read-index invalidate message handle failed, ret:" << ret << ".");
        return ret;
    }
    ret = netEngine->RegisterNewRequestHandler(BIO_OP_INTERCEPTOR_READ_BUFFER_RELEASE,
        std::bind(&InterceptorServer::HandleInterceptorReadBufferRelease, this, std::placeholders::_1));
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Register interceptor read buffer release message handle failed, ret:" << ret << ".");
        return ret;
    }
    return ret;
}

bool InterceptorServer::CheckInterceptorReadReq(InterceptorPreadIn *req)
{
    if (req->pid == 0) {
        return false;
    }
    if (req->nbytes > MAX_INTERCEPTOR_IO_SIZE || req->nbytes == 0) {
        return false;
    }
    constexpr uint32_t validFlags = INTERCEPTOR_PREAD_FLAG_BIO_FALLBACK | INTERCEPTOR_PREAD_FLAG_PREFETCH;
    if ((req->flags & ~validFlags) != 0) {
        return false;
    }
    return true;
}

void InterceptorServer::HandleProcBroken(uint32_t pid)
{
    AbortPreparedDirectWriteRecords(TakePreparedDirectWritesByPid(pid));
    ReleaseReadBuffersByPid(pid);
}

void InterceptorServer::GetReadBufferPoolState(uint64_t &allocated, uint64_t &freeCount)
{
    std::lock_guard<std::mutex> lock(mReadBufferLock);
    allocated = mReadBufferAllocatedCount;
    freeCount = 0;
}

void InterceptorServer::TrackReadBufferLocked(uint32_t pid, uintptr_t address)
{
    InterceptorReadBufferSlot slot;
    slot.address = address;
    mReadBuffers[pid].push_back(slot);
}

bool InterceptorServer::UntrackReadBufferLocked(uint32_t pid, uintptr_t address)
{
    auto iter = mReadBuffers.find(pid);
    if (iter == mReadBuffers.end()) {
        return false;
    }
    auto &buffers = iter->second;
    for (auto slot = buffers.begin(); slot != buffers.end(); ++slot) {
        if (slot->address != address) {
            continue;
        }
        buffers.erase(slot);
        if (buffers.empty()) {
            mReadBuffers.erase(iter);
        }
        return true;
    }
    return false;
}

void InterceptorServer::TryReleaseReadBufferFromRequest(const InterceptorPreadIn *req)
{
    if (req == nullptr || req->releaseLength == 0) {
        return;
    }
    BIO_TRACE_START(INTERCEPTOR_READ_PIGGYBACK_RELEASE);
    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    if (netEngine == nullptr) {
        BIO_TRACE_END(INTERCEPTOR_READ_PIGGYBACK_RELEASE, BIO_NOT_READY);
        return;
    }
    uint8_t *address = netEngine->GetShmAddress(req->releaseAddrOffset, req->releaseLength);
    if (UNLIKELY(address == nullptr)) {
        CLIENT_LOG_ERROR("Invalid piggyback read shm release, pid:" << req->pid << ", offset:" <<
            req->releaseAddrOffset << ", length:" << req->releaseLength << ".");
        BIO_TRACE_END(INTERCEPTOR_READ_PIGGYBACK_RELEASE, BIO_INVALID_PARAM);
        return;
    }
    BResult ret = BIO_OK;
    bool released = false;
    {
        std::lock_guard<std::mutex> lock(mReadBufferLock);
        released = UntrackReadBufferLocked(req->pid, reinterpret_cast<uintptr_t>(address));
        if (released) {
            --mReadBufferAllocatedCount;
        }
    }
    if (released) {
        netEngine->FreeLocalMrSingle(reinterpret_cast<uintptr_t>(address));
    } else {
        CLIENT_LOG_ERROR("Piggyback read shm release missed buffer, pid:" << req->pid << ", address:" <<
            reinterpret_cast<uintptr_t>(address) << ", offset:" << req->releaseAddrOffset << ", length:" <<
            req->releaseLength << ".");
        ret = BIO_ERR;
    }
    BIO_TRACE_END(INTERCEPTOR_READ_PIGGYBACK_RELEASE, ret);
}

BResult InterceptorServer::AcquireReadBuffer(uint32_t pid, uintptr_t &address)
{
    address = 0;
    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    if (UNLIKELY(netEngine == nullptr)) {
        return BIO_NOT_READY;
    }

    for (uint32_t retry = 0; retry < INTERCEPTOR_READ_BUFFER_ALLOC_RETRY; ++retry) {
        {
            std::lock_guard<std::mutex> lock(mReadBufferLock);
            ++mReadBufferAllocatedCount;
        }

        uint64_t mrKey = UINT64_MAX;
        BIO_TRACE_START(INTERCEPTOR_READ_BUFFER_ALLOC);
        BResult allocRet = netEngine->AllocLocalMrSingle(address, mrKey);
        BIO_TRACE_END(INTERCEPTOR_READ_BUFFER_ALLOC, allocRet);
        if (UNLIKELY(allocRet != BIO_OK || address == 0)) {
            if (address != 0) {
                netEngine->FreeLocalMrSingle(address);
                address = 0;
            }
            std::lock_guard<std::mutex> lock(mReadBufferLock);
            --mReadBufferAllocatedCount;
            if (retry + 1U == INTERCEPTOR_READ_BUFFER_ALLOC_RETRY) {
                return allocRet != BIO_OK ? allocRet : BIO_ALLOC_FAIL;
            }
            continue;
        }

        std::lock_guard<std::mutex> lock(mReadBufferLock);
        TrackReadBufferLocked(pid, address);
        return BIO_OK;
    }

    return BIO_ALLOC_FAIL;
}

bool InterceptorServer::ReleaseReadBuffer(uint32_t pid, uintptr_t address)
{
    if (address == 0) {
        return false;
    }
    bool released = false;
    {
        std::lock_guard<std::mutex> lock(mReadBufferLock);
        if (!UntrackReadBufferLocked(pid, address)) {
            return false;
        }
        --mReadBufferAllocatedCount;
        released = true;
    }
    if (released) {
        auto netEngine = BioClientNet::Instance()->GetNetEngine();
        if (netEngine != nullptr) {
            netEngine->FreeLocalMrSingle(address);
        }
    }
    return released;
}

void InterceptorServer::ReleaseReadBufferFromResp(uint32_t pid, const InterceptorPreadOut &resp)
{
    if (resp.dataSource != INTERCEPTOR_PREAD_DATA_READ_SHM || resp.addrNum == 0) {
        return;
    }
    uint64_t addrOffset = resp.windowAddrLen != 0 ? resp.windowAddrOffset : resp.addrOffset[0];
    uint64_t addrLen = resp.windowAddrLen != 0 ? resp.windowAddrLen : resp.addrLen[0];
    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    if (netEngine == nullptr || addrLen == 0) {
        return;
    }
    uint8_t *address = netEngine->GetShmAddress(addrOffset, addrLen);
    if (address != nullptr) {
        ReleaseReadBuffer(pid, reinterpret_cast<uintptr_t>(address));
    }
}

void InterceptorServer::ReleaseReadBuffersByPid(uint32_t pid)
{
    std::vector<uintptr_t> buffers;
    {
        std::lock_guard<std::mutex> lock(mReadBufferLock);
        auto iter = mReadBuffers.find(pid);
        if (iter == mReadBuffers.end()) {
            return;
        }
        for (const auto &slot : iter->second) {
            auto address = slot.address;
            if (address == 0) {
                continue;
            }
            buffers.push_back(address);
        }
        mReadBufferAllocatedCount -= buffers.size();
        mReadBuffers.erase(iter);
    }
    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    if (netEngine == nullptr) {
        return;
    }
    for (auto address : buffers) {
        netEngine->FreeLocalMrSingle(address);
    }
}

void InterceptorServer::CleanupReadBuffers()
{
    std::vector<uintptr_t> buffers;
    {
        std::lock_guard<std::mutex> lock(mReadBufferLock);
        for (auto &item : mReadBuffers) {
            for (const auto &slot : item.second) {
                auto address = slot.address;
                if (address != 0) {
                    buffers.push_back(address);
                }
            }
        }
        mReadBuffers.clear();
        mReadBufferAllocatedCount = 0;
    }
    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    if (netEngine == nullptr) {
        return;
    }
    for (auto address : buffers) {
        netEngine->FreeLocalMrSingle(address);
    }
}

int InterceptorServer::ReadDataToReadBuffer(uint32_t pid, uint64_t inode, uint64_t offset, uint64_t nbytes,
    bool prefetch,
    InterceptorPreadOut &resp)
{
    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    uint64_t windowOffset = InterceptorWindowId(offset) * INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE;
    uint64_t inWindow = offset - windowOffset;
    bool singleWindowReq = nbytes <= INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE - inWindow;
    uint64_t readOffset = singleWindowReq ? windowOffset : offset;
    uint64_t readRequestLen = singleWindowReq ? INTERCEPTOR_DIRECT_SPACE_WINDOW_SIZE : nbytes;
    if (UNLIKELY(netEngine == nullptr || netEngine->GetDataPage() < readRequestLen)) {
        return RET_CACHE_NOT_SUPPORTED;
    }

    uintptr_t address = 0;
    BIO_TRACE_START(INTERCEPTOR_READ_BUFFER_ACQUIRE);
    BResult allocRet = AcquireReadBuffer(pid, address);
    BIO_TRACE_END(INTERCEPTOR_READ_BUFFER_ACQUIRE, allocRet);
    if (UNLIKELY(allocRet != BIO_OK || address == 0)) {
        uint64_t allocated = 0;
        uint64_t freeCount = 0;
        GetReadBufferPoolState(allocated, freeCount);
        if (prefetch) {
            CLIENT_LOG_DEBUG("Skip read prefetch because read shm buffer is not available, inode:" << inode <<
                ", offset:" << offset << ", nbytes:" << nbytes << ", ret:" << allocRet << ", allocated:" <<
                allocated << ", free:" << freeCount << ".");
        } else {
            CLIENT_LOG_ERROR("Alloc read shm buffer failed, inode:" << inode << ", offset:" << offset <<
                ", nbytes:" << nbytes << ", ret:" << allocRet << ", allocated:" << allocated << ", free:" <<
                freeCount << ".");
        }
        return allocRet != BIO_OK ? allocRet : RET_CACHE_NOT_SUPPORTED;
    }

    CacheSpaceDesc space{};
    space.allocLoc = 1;
    space.addressNum = 1;
    space.address[0].address = static_cast<uint64_t>(address);
    space.address[0].size = netEngine->GetDataPage();

    int readLen = 0;
    BIO_TRACE_START(INTERCEPTOR_READ_HOOK);
    BIO_TRACE_START(INTERCEPTOR_READ_TO_SPACE_HOOK);
    int readRet = BioReadToSpaceHook(inode, readRequestLen, readOffset, &space, &readLen);
    BIO_TRACE_END(INTERCEPTOR_READ_TO_SPACE_HOOK, readRet);
    BIO_TRACE_END(INTERCEPTOR_READ_HOOK, readRet);
    if (UNLIKELY(readRet != 0)) {
        ReleaseReadBuffer(pid, address);
        CLIENT_LOG_ERROR("Read data to read shm buffer failed, pid:" << pid << ", inode:" << inode <<
            ", offset:" << offset << ", nbytes:" << nbytes << ", readLen:" << readLen << ", ret:" << readRet <<
            ".");
        return readRet;
    }
    uint64_t readDataLen = static_cast<uint64_t>(readLen);
    uint64_t dataSkip = singleWindowReq ? inWindow : 0;
    if (readLen == 0 || readDataLen <= dataSkip) {
        ReleaseReadBuffer(pid, address);
        resp = {};
        resp.ret = 0;
        resp.dataLen = 0;
        return BIO_OK;
    }

    BIO_TRACE_START(INTERCEPTOR_READ_RESPONSE_FILL);
    uint64_t addrOffset = netEngine->GetAddressOffset(address);
    uint64_t dataLen = std::min<uint64_t>(nbytes, readDataLen - dataSkip);
    uint64_t respWindowOffset = singleWindowReq ? windowOffset : 0;
    uint64_t respWindowDataLen = singleWindowReq ? readDataLen : 0;
    uint64_t respWindowAddrOffset = singleWindowReq ? addrOffset : 0;
    uint64_t respWindowAddrLen = singleWindowReq ? readRequestLen : 0;
    if (UNLIKELY(dataLen == 0 || addrOffset == UINT64_MAX ||
        !FillReadRespFromBuffer(addrOffset + dataSkip, dataLen, respWindowOffset, respWindowDataLen,
            respWindowAddrOffset, respWindowAddrLen, resp))) {
        BIO_TRACE_END(INTERCEPTOR_READ_RESPONSE_FILL, RET_CACHE_NOT_SUPPORTED);
        ReleaseReadBuffer(pid, address);
        CLIENT_LOG_ERROR("Fill read shm buffer response failed, inode:" << inode << ", offset:" << offset <<
            ", nbytes:" << nbytes << ", readLen:" << readLen << ".");
        return RET_CACHE_NOT_SUPPORTED;
    }
    BIO_TRACE_END(INTERCEPTOR_READ_RESPONSE_FILL, BIO_OK);
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorBioShmInit(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(ShmInitRequest)) || UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Invalid interceptor bio shm init request, msgLen:" << ctx.MessageDataLen() <<
            ", expectLen:" << sizeof(ShmInitRequest) << ", hasData:" <<
            (ctx.MessageData() == nullptr ? "false" : "true") << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<ShmInitRequest *>(ctx.MessageData());
    BIO_TRACE_START(INTERCEPTOR_SERVER_SHM_INIT_HANDLE);
    ShmInitResponse rsp{};
    uint64_t shmKey = 0;
    auto netEngine = BioClientNet::Instance()->GetNetEngine();
    auto ret = netEngine->QueryShmInfo(rsp.memFd, rsp.offset, rsp.length, shmKey);
    if (UNLIKELY(ret != BIO_OK || rsp.memFd < 0 || rsp.length == 0)) {
        CLIENT_LOG_ERROR("Query bio shm info failed, ret:" << ret << ", fd:" << rsp.memFd <<
            ", offset:" << rsp.offset << ", length:" << rsp.length << ".");
        netEngine->Reply(ctx, BIO_NOT_READY, nullptr, 0);
        BIO_TRACE_END(INTERCEPTOR_SERVER_SHM_INIT_HANDLE, BIO_NOT_READY);
        return BIO_OK;
    }

    rsp.serverPid = getpid();
    rsp.readIndexFd = -1;
    rsp.readIndexEntryCount = 0;
    rsp.readIndexLength = 0;
    bool hasReadIndex = EnsureReadIndex() == BIO_OK;
    if (hasReadIndex) {
        rsp.readIndexFd = mReadIndexFd;
        rsp.readIndexEntryCount = INTERCEPTOR_READ_INDEX_ENTRY_COUNT;
        rsp.readIndexLength = mReadIndexLength;
    }

    int32_t shmFds[INTERCEPTOR_SHM_FD_MAX_COUNT] = { rsp.memFd, rsp.readIndexFd };
    uint32_t fdCount = hasReadIndex ? INTERCEPTOR_SHM_FD_WITH_READ_INDEX_COUNT :
        INTERCEPTOR_SHM_FD_WITHOUT_READ_INDEX_COUNT;
    ret = netEngine->SendFds(ctx.Channel(), shmFds, fdCount);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send bio shm fd failed, ret:" << ret << ", fdCount:" << fdCount << ".");
        netEngine->Reply(ctx, BIO_ERR, nullptr, 0);
        BIO_TRACE_END(INTERCEPTOR_SERVER_SHM_INIT_HANDLE, BIO_ERR);
        return BIO_OK;
    }
    netEngine->Reply(ctx, BIO_OK, &rsp, sizeof(ShmInitResponse));
    BIO_TRACE_END(INTERCEPTOR_SERVER_SHM_INIT_HANDLE, BIO_OK);
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorWritePrepareSpace(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorPwritePrepareSpaceIn)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Invalid interceptor write prepare space request, msgLen:" << ctx.MessageDataLen() <<
            ", expectLen:" << sizeof(InterceptorPwritePrepareSpaceIn) << ", hasData:" <<
            (ctx.MessageData() == nullptr ? "false" : "true") << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorPwritePrepareSpaceIn *>(ctx.MessageData());
    BIO_TRACE_START(INTERCEPTOR_SERVER_WRITE_PREPARE_HANDLE);
    InterceptorPwritePrepareSpaceOut resp{};
    if (UNLIKELY(req->pid == 0 || req->offset < 0 ||
        !SplitDirectSpaceRange(static_cast<uint64_t>(req->offset), req->nbytes, resp.segs, resp.segNum))) {
        resp.ret = RET_CACHE_EPERM;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp), sizeof(resp));
        BIO_TRACE_END(INTERCEPTOR_SERVER_WRITE_PREPARE_HANDLE, resp.ret);
        return BIO_OK;
    }

    for (uint32_t segIdx = 0; segIdx < resp.segNum; ++segIdx) {
        auto &seg = resp.segs[segIdx];
        CResult prepareRet = PrepareDirectSpaceSegment(req->inode, seg);
        if (UNLIKELY(prepareRet != RET_CACHE_OK)) {
            resp.ret = prepareRet;
            seg.ret = resp.ret;
            AbortPreparedDirectSpaceSegments(resp.segs, segIdx);
            CLIENT_LOG_DEBUG("Prepare direct write segment failed, inode:" << req->inode << ", fd:" << req->fd <<
                ", segIdx:" << segIdx << ", offset:" << seg.offset << ", nbytes:" << seg.nbytes <<
                ", ret:" << resp.ret << ".");
            BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp), sizeof(resp));
            BIO_TRACE_END(INTERCEPTOR_SERVER_WRITE_PREPARE_HANDLE, resp.ret);
            return BIO_OK;
        }
    }

    TrackPreparedDirectWrite(req->pid, req->fd, req->inode, req->offset, req->nbytes, resp.segs, resp.segNum);
    resp.ret = 0;
    resp.addrNum = resp.segs[0].addrNum;
    resp.space = resp.segs[0].space;
    for (uint32_t idx = 0; idx < resp.addrNum; ++idx) {
        resp.addrOffset[idx] = resp.segs[0].addrOffset[idx];
        resp.addrLen[idx] = resp.segs[0].addrLen[idx];
    }
    CLIENT_LOG_DEBUG("Prepare direct write space success, inode:" << req->inode << ", fd:" << req->fd <<
        ", offset:" << req->offset << ", nbytes:" << req->nbytes << ", segNum:" << resp.segNum << ".");
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp), sizeof(resp));
    BIO_TRACE_END(INTERCEPTOR_SERVER_WRITE_PREPARE_HANDLE, BIO_OK);
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorWriteCommitSpace(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorPwriteCommitSpaceIn)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Invalid interceptor write commit space request, msgLen:" << ctx.MessageDataLen() <<
            ", expectLen:" << sizeof(InterceptorPwriteCommitSpaceIn) << ", hasData:" <<
            (ctx.MessageData() == nullptr ? "false" : "true") << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorPwriteCommitSpaceIn *>(ctx.MessageData());
    BIO_TRACE_START(INTERCEPTOR_SERVER_WRITE_COMMIT_HANDLE);
    InterceptorPwriteCommitSpaceOut resp{};
    if (req->abortOnly != 0) {
        PreparedDirectWriteRecord record;
        if (TakePreparedDirectWrite(req->pid, req->fd, req->inode, req->offset, req->nbytes, req->segs, req->segNum,
            false, record)) {
            AbortPreparedDirectSpaceSegments(record.segs.data(), static_cast<uint32_t>(record.segs.size()));
        }
        resp.ret = 0;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp), sizeof(resp));
        BIO_TRACE_END(INTERCEPTOR_SERVER_WRITE_COMMIT_HANDLE, BIO_OK);
        return BIO_OK;
    }

    if (UNLIKELY(req->offset < 0 || !ValidateDirectSpaceSegments(static_cast<uint64_t>(req->offset), req->nbytes,
        req->segs, req->segNum))) {
        PreparedDirectWriteRecord record;
        if (TakePreparedDirectWrite(req->pid, req->fd, req->inode, req->offset, req->nbytes, req->segs, req->segNum,
            false, record)) {
            AbortPreparedDirectSpaceSegments(record.segs.data(), static_cast<uint32_t>(record.segs.size()));
        }
        resp.ret = RET_CACHE_EPERM;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp), sizeof(resp));
        BIO_TRACE_END(INTERCEPTOR_SERVER_WRITE_COMMIT_HANDLE, resp.ret);
        return BIO_OK;
    }

    PreparedDirectWriteRecord record;
    if (UNLIKELY(!TakePreparedDirectWrite(req->pid, req->fd, req->inode, req->offset, req->nbytes, req->segs,
        req->segNum, false, record))) {
        resp.ret = RET_CACHE_NOT_FOUND;
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp), sizeof(resp));
        BIO_TRACE_END(INTERCEPTOR_SERVER_WRITE_COMMIT_HANDLE, resp.ret);
        return BIO_OK;
    }

    if (req->startTime != 0) {
        BIO_TRACE_ASYNC_BEGIN(INTERCEPTOR_WRITE_START);
        BIO_TRACE_ASYNC_END(INTERCEPTOR_WRITE_START, 0, req->startTime);
    }
    InvalidateReadIndex(req->inode, static_cast<uint64_t>(req->offset), req->nbytes);
    if (req->invalidateRemoteReadIndex != 0) {
        BroadcastReadIndexInvalidate(req->inode, static_cast<uint64_t>(req->offset), req->nbytes);
    }
    uint64_t committedBytes = 0;
    for (uint32_t segIdx = 0; segIdx < req->segNum; ++segIdx) {
        auto &seg = req->segs[segIdx];
        BIO_TRACE_START(INTERCEPTOR_WRITE_HOOK);
        bool localCommitted = false;
        int32_t writeRet = CommitDirectSpaceSegment(req->inode, seg, localCommitted);
        BIO_TRACE_END(INTERCEPTOR_WRITE_HOOK, writeRet);
        resp.ret = writeRet;
        if (UNLIKELY(writeRet != 0)) {
            CLIENT_LOG_ERROR("Direct write copy-free hook failed, inode:" << req->inode << ", fd:" << req->fd <<
                ", segIdx:" << segIdx << ", offset:" << seg.offset << ", nbytes:" << seg.nbytes <<
                ", ret:" << writeRet << ".");
            AbortPreparedDirectSpaceSegment(seg);
            AbortPreparedDirectSpaceSegments(req->segs + segIdx + 1, req->segNum - segIdx - 1);
            resp.committedBytes = committedBytes;
            BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp), sizeof(resp));
            BIO_TRACE_END(INTERCEPTOR_SERVER_WRITE_COMMIT_HANDLE, writeRet);
            return BIO_OK;
        }

        InterceptorPreadOut readResp{};
        if (seg.mode == INTERCEPTOR_WRITE_MODE_LOCAL && localCommitted &&
            FillReadRespFromSpace(seg.space, seg.nbytes, readResp)) {
            PublishReadIndex(req->inode, seg.offset, readResp);
        } else if (!localCommitted || seg.mode == INTERCEPTOR_WRITE_MODE_REMOTE) {
            ReleasePreparedDirectSpaceSegment(seg);
        }
        committedBytes += seg.nbytes;
    }
    resp.committedBytes = committedBytes;
    CLIENT_LOG_DEBUG("Direct write commit success, inode:" << req->inode << ", fd:" << req->fd << ", offset:" <<
        req->offset << ", nbytes:" << req->nbytes << ", segNum:" << req->segNum << ".");
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp), sizeof(resp));
    BIO_TRACE_END(INTERCEPTOR_SERVER_WRITE_COMMIT_HANDLE, BIO_OK);
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorReadIndexInvalidate(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorReadIndexInvalidateIn)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Invalid interceptor read-index invalidate request, msgLen:" << ctx.MessageDataLen() <<
            ", expectLen:" << sizeof(InterceptorReadIndexInvalidateIn) << ", hasData:" <<
            (ctx.MessageData() == nullptr ? "false" : "true") << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorReadIndexInvalidateIn *>(ctx.MessageData());
    BIO_TRACE_START(INTERCEPTOR_SERVER_READ_INDEX_INVALIDATE_HANDLE);
    InvalidateReadIndex(req->inode, req->offset, req->length);
    if (req->broadcastRemote != 0) {
        BroadcastReadIndexInvalidate(req->inode, req->offset, req->length);
    }
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, nullptr, 0);
    BIO_TRACE_END(INTERCEPTOR_SERVER_READ_INDEX_INVALIDATE_HANDLE, BIO_OK);
    return BIO_OK;
}

BResult InterceptorServer::HandleInterceptorReadBufferRelease(ServiceContext &ctx)
{
    if (UNLIKELY(ctx.MessageDataLen() != sizeof(InterceptorReadBufferReleaseIn)) ||
        UNLIKELY(ctx.MessageData() == nullptr)) {
        CLIENT_LOG_ERROR("Invalid interceptor read buffer release request, msgLen:" << ctx.MessageDataLen() <<
            ", expectLen:" << sizeof(InterceptorReadBufferReleaseIn) << ", hasData:" <<
            (ctx.MessageData() == nullptr ? "false" : "true") << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    auto *req = static_cast<InterceptorReadBufferReleaseIn *>(ctx.MessageData());
    BIO_TRACE_START(INTERCEPTOR_READ_BUFFER_RELEASE);
    uint8_t *address = BioClientNet::Instance()->GetNetEngine()->GetShmAddress(req->addrOffset, req->length);
    bool released = address != nullptr && ReleaseReadBuffer(req->pid, reinterpret_cast<uintptr_t>(address));
    BResult ret = released ? BIO_OK : BIO_ERR;
    if (UNLIKELY(ret != BIO_OK)) {
        CLIENT_LOG_ERROR("Release read shm buffer failed, pid:" << req->pid << ", offset:" << req->addrOffset <<
            ", length:" << req->length << ".");
    }
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, ret, nullptr, 0);
    BIO_TRACE_END(INTERCEPTOR_READ_BUFFER_RELEASE, ret);
    return BIO_OK;
}

bool InterceptorServer::TryReplyReadDataFallback(ServiceContext &ctx, const InterceptorPreadIn *req, bool prefetch,
    int readAddrRet, const CacheReadAddrDesc &desc, int &traceRet)
{
    bool allowDataFallback = (req->flags & INTERCEPTOR_PREAD_FLAG_BIO_FALLBACK) != 0;
    if (!allowDataFallback || req->offset < 0) {
        return false;
    }

    CLIENT_LOG_DEBUG("Read address hook failed, try read shm fallback, inode:" << req->inode << ", fd:" <<
        req->fd << ", offset:" << req->offset << ", nbytes:" << req->nbytes << ", pid:" << req->pid <<
        ", readLen:" << desc.realLen << ", ret:" << readAddrRet << ".");
    InterceptorPreadOut bioResp{};
    BIO_TRACE_START(INTERCEPTOR_READ_DATA_FALLBACK_HANDLE);
    int readRet = ReadDataToReadBuffer(req->pid, req->inode, static_cast<uint64_t>(req->offset), req->nbytes,
        prefetch, bioResp);
    if (readRet == 0) {
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, &bioResp, sizeof(InterceptorPreadOut));
        BIO_TRACE_END(INTERCEPTOR_READ_DATA_FALLBACK_HANDLE, BIO_OK);
        traceRet = BIO_OK;
        return true;
    }
    BIO_TRACE_END(INTERCEPTOR_READ_DATA_FALLBACK_HANDLE, readRet);

    InterceptorPreadOut resp{};
    resp.ret = readRet;
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPreadOut));
    traceRet = readRet;
    return true;
}

void InterceptorServer::ReplyReadError(ServiceContext &ctx, const InterceptorPreadIn *req, int ret,
    const CacheReadAddrDesc &desc)
{
    CLIENT_LOG_ERROR("Read hook failed, inode:" << req->inode << ", fd:" << req->fd << ", offset:" <<
        req->offset << ", nbytes:" << req->nbytes << ", pid:" << req->pid << ", readLen:" << desc.realLen <<
        ", ret:" << ret << ".");
    InterceptorPreadOut resp{};
    resp.ret = ret;
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPreadOut));
}

void InterceptorServer::ReplyReadAddress(ServiceContext &ctx, const InterceptorPreadIn *req,
    const CacheReadAddrDesc &desc)
{
    InterceptorPreadOut resp{};
    FillReadRespFromDesc(desc, resp);
    if (req->offset >= 0) {
        PublishReadIndex(req->inode, static_cast<uint64_t>(req->offset), resp);
    }
    BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_OK, static_cast<void *>(&resp),
        sizeof(InterceptorPreadOut));
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
            ", offset:" << req->offset << ", nbytes:" << req->nbytes << ", pid:" << req->pid << ".");
        BioClientNet::Instance()->GetNetEngine()->Reply(ctx, BIO_INVALID_PARAM, nullptr, 0);
        return BIO_OK;
    }

    BIO_TRACE_ASYNC_BEGIN(INTERCEPTOR_READ_START);
    BIO_TRACE_ASYNC_END(INTERCEPTOR_READ_START, 0, req->startTime);
    CLIENT_LOG_DEBUG("Receive interceptor read request, inode:" << req->inode << ", fd:" << req->fd <<
        ", offset:" << req->offset << ", nbytes:" << req->nbytes << ", pid:" << req->pid << ".");
    BIO_TRACE_START(INTERCEPTOR_SERVER_READ_RELEASE_HANDLE);
    TryReleaseReadBufferFromRequest(req);
    BIO_TRACE_END(INTERCEPTOR_SERVER_READ_RELEASE_HANDLE, BIO_OK);
    BIO_TRACE_START(INTERCEPTOR_SERVER_READ_HANDLE);
    CacheReadAddrDesc desc{};
    bool prefetch = (req->flags & INTERCEPTOR_PREAD_FLAG_PREFETCH) != 0;
    int ret = RET_CACHE_NOT_SUPPORTED;
    if (!prefetch) {
        BIO_TRACE_START(INTERCEPTOR_READ_HOOK);
        BIO_TRACE_START(INTERCEPTOR_READ_ADDR_HOOK);
        ret = BioReadAddrHook(req->inode, req->nbytes, req->offset, &desc);
        BIO_TRACE_END(INTERCEPTOR_READ_ADDR_HOOK, ret);
        BIO_TRACE_END(INTERCEPTOR_READ_HOOK, ret);
    }
    if (UNLIKELY(ret != 0)) {
        int traceRet = ret;
        if (TryReplyReadDataFallback(ctx, req, prefetch, ret, desc, traceRet)) {
            BIO_TRACE_END(INTERCEPTOR_SERVER_READ_HANDLE, traceRet);
            return BIO_OK;
        }
        ReplyReadError(ctx, req, ret, desc);
        BIO_TRACE_END(INTERCEPTOR_SERVER_READ_HANDLE, ret);
        return BIO_OK;
    }

    ReplyReadAddress(ctx, req, desc);
    BIO_TRACE_END(INTERCEPTOR_SERVER_READ_HANDLE, BIO_OK);
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
