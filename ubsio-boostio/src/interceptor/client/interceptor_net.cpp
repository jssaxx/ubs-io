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

    ret = ShmInit();
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Init share memory failed:" << ret << ".");
        return ret;
    }

    CLOG_DEBUG("Start net service success.");
    mReady.store(true);
    return 0;
}

BResult InterceptorClientNetService::CorrectFd()
{
    int32_t realFd = -1;
    auto result = mNetEngine->ReceiveFds(INVALID_NID, &realFd, 1U);
    if (UNLIKELY(result != BIO_OK)) {
        CLOG_ERROR("receive file mem fd failed, ret:" << result << ".");
        return BIO_ERR;
    }
    mShmFd = realFd;
    return BIO_OK;
}

BResult InterceptorClientNetService::CheckShmFd()
{
    struct stat buffer {};
    auto ret = fstat(mShmFd, &buffer);
    if (UNLIKELY(ret < 0)) {
        CLOG_ERROR("Read file failed, ret:" << ret << ".");
        return BIO_ERR;
    }

    if (UNLIKELY(mShmOffset + mShmLength > static_cast<uint64_t>(buffer.st_size))) {
        CLOG_ERROR("Share memory size:" << mShmOffset + mShmLength << " not equal to file size:" << buffer.st_size);
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult InterceptorClientNetService::ShmInitInner()
{
    if (UNLIKELY(CheckShmFd() != BIO_OK)) {
        mShmFd = -1;
        return BIO_ERR;
    }
    auto offset = static_cast<off_t>(mShmOffset);
    auto address = mmap(nullptr, mShmLength, PROT_READ | PROT_WRITE, MAP_SHARED, mShmFd, offset);
    if (UNLIKELY(address == MAP_FAILED)) {
        CLOG_ERROR("Map share memory offset:" << offset << ", size:" << mShmLength << " failed.");
        close(mShmFd);
        mShmFd = -1;
        return BIO_ERR;
    }
    mShmAddr = static_cast<uint8_t *>(address);
    return BIO_OK;
}

BResult InterceptorClientNetService::ShmInit()
{
    uint64_t defaultMaxShmSize = (300UL * 1024UL * 1024UL * 1024UL); // 300G
    ShmInitRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() } };
    ShmInitResponse rsp;
    BResult ret = mNetEngine->SyncCall<ShmInitRequest, ShmInitResponse>(INVALID_NID, BIO_OP_SDK_SHM_INIT, req, rsp);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Send share memory init message failed:" << ret << ".");
        return ret;
    }

    mShmFd = rsp.memFd;
    mShmOffset = rsp.offset;
    mShmLength = rsp.length;
    if (UNLIKELY(mShmOffset != 0 || mShmLength > defaultMaxShmSize)) {
        CLOG_ERROR("Get share memory offset:" << mShmOffset << ", length:" << mShmLength << " wrong.");
        return BIO_ERR;
    }

    if (UNLIKELY(CorrectFd() != BIO_OK)) {
        return BIO_ERR;
    }

    if (UNLIKELY((ret = ShmInitInner()) != BIO_OK)) {
        return ret;
    }

    mNetEngine->SetShmInfo(mShmFd, mShmAddr, mShmOffset, mShmLength);
    CLOG_DEBUG("Interceptor init share memory success, offset:" << mShmOffset << ", length:" << mShmLength);
    return BIO_OK;
}