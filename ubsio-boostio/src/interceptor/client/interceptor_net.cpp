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

#include <utility>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <sys/syscall.h>
#include <linux/version.h>

#include <new>
#include "bio_err.h"
#include "bio_ref.h"
#include "interceptor_log.h"
#include "message_op.h"
#include "message.h"
#include "interceptor_net.h"

using namespace ock::bio;

static void Log(int level, const char *msg)
{
    if (level == 0) {
        CLOG_DEBUG(msg);
    } else if (level == 1U) {
        CLOG_INFO(msg);
    } else if (level == 2U) {
        CLOG_WARN(msg);
    } else if (level == 3U) {
        CLOG_ERROR(msg);
    }
}

int32_t InterceptorClientNetService::StartNetService()
{
    mNetEngine = MakeRef<NetEngine>();
    if (mNetEngine == nullptr) {
        return BIO_ALLOC_FAIL;
    }

    int16_t timeoutSec = NO_60;
    uint32_t coreThreadNum = 1;
    uint32_t queueSize = NO_128;
    auto ret = mNetEngine->Initialize(timeoutSec, coreThreadNum, queueSize, Log);
    if (UNLIKELY(ret != BIO_OK)) {
        return ret;
    }

    NetOptions netOptions;
    netOptions.isBusyLoop = false;
    netOptions.role = Role::NET_CLIENT;
    netOptions.protocol = ServiceProtocol::SHM;
    netOptions.connCount = NO_4;
    netOptions.handlerCount = 1;
    ret = mNetEngine->Start(netOptions);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Start ipc engine failed:" << ret << ".");
        return ret;
    }

    mPid = static_cast<uint32_t>(getpid());
    ConnectInfo info(INVALID_NID, mPid, INVALID_NID);
    info.isSelfPoll = true;
    ret = mNetEngine->SyncConnect(info);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Start connect ipc engine failed:" << ret << ".");
        return ret;
    }

    ret = CreateDataMessageMem();
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Create data message memory failed:" << ret << ".");
        return ret;
    }

    ret = InitBioShm();
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_WARN("Init bio shm failed:" << ret << ", copy-free write disabled.");
    }

    CLOG_DEBUG("Start net service success.");
    mReady.store(true);
    return 0;
}

BResult InterceptorClientNetService::CreateDataMessageMem()
{
    InterceptorCreateDataMsgMemPoolRequest req;
    req.comm.magic = MESSAGE_MAGIC;
    req.comm.pid = mPid;

    InterceptorCreateDataMsgMemPoolResponse rsp;
    auto ret = mNetEngine->SyncCall<InterceptorCreateDataMsgMemPoolRequest, InterceptorCreateDataMsgMemPoolResponse>(
        INVALID_NID, BIO_OP_INTERCEPTOR_CREATE_DATA_MSG_MEM_POOL, req, rsp);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Send create data message mem pool request failed:" << ret << ".");
        return ret;
    }

    int32_t realFd = -1;
    ret = mNetEngine->ReceiveFds(INVALID_NID, &realFd, 1U);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Receive file mem fd failed, ret:" << ret << ".");
        return BIO_ERR;
    }

    mShmFd = realFd;
    mShmOffset = rsp.offset;
    mShmLength = rsp.poolSize;
    mDataMsgMemBlockSize = rsp.blockSize;

    if (mShmLength == 0 || mDataMsgMemBlockSize == 0) {
        CLOG_ERROR("Invalid pool size:" << mShmLength << " or block size:" << mDataMsgMemBlockSize << ".");
        close(mShmFd);
        mShmFd = -1;
        return BIO_ERR;
    }

    off_t offset = static_cast<off_t>(mShmOffset);
    auto address = mmap(nullptr, mShmLength, PROT_READ | PROT_WRITE, MAP_SHARED, mShmFd, offset);
    if (UNLIKELY(address == MAP_FAILED)) {
        CLOG_ERROR("Mmap shm size " << mShmLength << " offset " << offset << " failed, error:" << strerror(errno));
        close(mShmFd);
        mShmFd = -1;
        return BIO_ERR;
    }

    mShmAddr = static_cast<uint8_t *>(address);
    mDataMsgMemAddr = mShmAddr;

    uint64_t blockCount = mShmLength / mDataMsgMemBlockSize;
    mDataMsgMemPool = MakeRef<NetBlockPool>();
    if (mDataMsgMemPool == nullptr) {
        CLOG_ERROR("Alloc net block pool failed.");
        if (munmap(mShmAddr, mShmLength) == -1) {
            CLOG_ERROR("Munmap address failed.");
        }
        close(mShmFd);
        mShmFd = -1;
        mShmAddr = nullptr;
        mDataMsgMemAddr = nullptr;
        return BIO_ALLOC_FAIL;
    }

    ret = mDataMsgMemPool->Start(reinterpret_cast<uintptr_t>(mDataMsgMemAddr), mDataMsgMemBlockSize, blockCount);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Start net block pool failed, ret:" << ret << ".");
        mDataMsgMemPool = nullptr;
        if (munmap(mShmAddr, mShmLength) == -1) {
            CLOG_ERROR("Munmap address failed.");
        }
        close(mShmFd);
        mShmFd = -1;
        mShmAddr = nullptr;
        mDataMsgMemAddr = nullptr;
        return ret;
    }

    mNetEngine->SetShmInfo(mShmFd, mShmAddr, mShmOffset, mShmLength);
    CLOG_DEBUG("Interceptor create data message memory success, poolSize:" << mShmLength <<
        ", blockSize:" << mDataMsgMemBlockSize << ", blockCount:" << blockCount << ".");
    return BIO_OK;
}

BResult InterceptorClientNetService::InitBioShm()
{
    InterceptorInitBioShmReq req;
    req.pid = mPid;

    InterceptorInitBioShmResp rsp;
    auto ret = mNetEngine->SyncCall<InterceptorInitBioShmReq, InterceptorInitBioShmResp>(
        INVALID_NID, BIO_OP_INTERCEPTOR_INIT_BIO_SHM, req, rsp);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Send init bio shm request failed:" << ret << ".");
        return ret;
    }
    if (rsp.ret != 0) {
        CLOG_ERROR("Init bio shm response ret:" << rsp.ret << ".");
        return BIO_ERR;
    }

    int32_t realFd = -1;
    ret = mNetEngine->ReceiveFds(INVALID_NID, &realFd, 1U);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Receive bio shm fd failed, ret:" << ret << ".");
        return BIO_ERR;
    }

    mBioShmFd = realFd;
    mBioShmOffset = rsp.offset;
    mBioShmLength = rsp.length;

    if (mBioShmLength == 0) {
        CLOG_ERROR("Invalid bio shm length:" << mBioShmLength << ".");
        close(mBioShmFd);
        mBioShmFd = -1;
        return BIO_ERR;
    }

    off_t offset = static_cast<off_t>(mBioShmOffset);
    auto address = mmap(nullptr, mBioShmLength, PROT_READ | PROT_WRITE, MAP_SHARED, mBioShmFd, offset);
    if (UNLIKELY(address == MAP_FAILED)) {
        CLOG_ERROR("Mmap bio shm size " << mBioShmLength << " offset " << offset << " failed, error:" <<
            strerror(errno));
        close(mBioShmFd);
        mBioShmFd = -1;
        return BIO_ERR;
    }

    mBioShmAddr = static_cast<uint8_t *>(address);
    CLOG_DEBUG("Init bio shm success, offset:" << mBioShmOffset << ", length:" << mBioShmLength << ".");
    return BIO_OK;
}
