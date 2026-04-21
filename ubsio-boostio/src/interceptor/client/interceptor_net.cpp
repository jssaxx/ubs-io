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
#include <limits>
#include <cstdlib>
#include <cstring>
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

static bool ParseBoolEnv(const char *envName, bool defaultValue)
{
    const char *envValue = std::getenv(envName);
    if (envValue == nullptr || strlen(envValue) == 0) {
        return defaultValue;
    }
    return strcasecmp(envValue, "1") == 0 || strcasecmp(envValue, "true") == 0 ||
           strcasecmp(envValue, "yes") == 0 || strcasecmp(envValue, "on") == 0;
}

static BResult ParseUInt16Env(const char *envName, uint16_t defaultValue, uint16_t &value)
{
    value = defaultValue;
    const char *envValue = std::getenv(envName);
    if (envValue == nullptr || strlen(envValue) == 0) {
        return BIO_OK;
    }

    long parsed = 0;
    if (!StrUtil::StrToLong(envValue, parsed) || parsed <= 0 ||
        parsed > static_cast<long>(std::numeric_limits<uint16_t>::max())) {
        return BIO_INVALID_PARAM;
    }
    value = static_cast<uint16_t>(parsed);
    return BIO_OK;
}

static std::string FormatWorkerGroupCpuIdsRange(const std::vector<std::pair<uint32_t, uint32_t>> &ranges)
{
    std::ostringstream oss;
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (i != 0) {
            oss << ",";
        }
        const auto &range = ranges[i];
        if (range.first == UINT32_MAX && range.second == UINT32_MAX) {
            oss << "-1";
        } else {
            oss << range.first << "-" << range.second;
        }
    }
    return oss.str();
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
    netOptions.role = Role::NET_CLIENT;
    netOptions.protocol = ServiceProtocol::SHM;
    netOptions.workerGroupCpuIdsRange = DefaultWorkerGroupCpuIdsRange(IPC_WORKER_GROUP_CPU_RANGE_COUNT);
    const char *connCountEnv = std::getenv("INTERCEPTOR_IPC_CONN_COUNT");
    ret = ParseUInt16Env("INTERCEPTOR_IPC_CONN_COUNT", NO_1, netOptions.connCount);
    if (UNLIKELY(ret != BIO_OK)) {
        CLOG_ERROR("Parse INTERCEPTOR_IPC_CONN_COUNT failed.");
        return ret;
    }
    if (connCountEnv != nullptr && strlen(connCountEnv) > 0) {
        CLOG_INFO("Apply INTERCEPTOR_IPC_CONN_COUNT success, value:" << netOptions.connCount << ".");
    }

    const char *busyLoopEnv = std::getenv("INTERCEPTOR_IPC_BUSY_LOOP");
    netOptions.isBusyLoop = ParseBoolEnv("INTERCEPTOR_IPC_BUSY_LOOP", false);
    if (busyLoopEnv != nullptr && strlen(busyLoopEnv) > 0) {
        CLOG_INFO("Apply INTERCEPTOR_IPC_BUSY_LOOP success, value:" <<
            (netOptions.isBusyLoop ? "true" : "false") << ".");
    }

    const char *cpuIdsEnv = std::getenv("INTERCEPTOR_IPC_CPUIDS");
    if (cpuIdsEnv != nullptr && strlen(cpuIdsEnv) > 0) {
        if (!ParseWorkerGroupCpuIdsRange(cpuIdsEnv, netOptions.workerGroupCpuIdsRange,
            IPC_WORKER_GROUP_CPU_RANGE_COUNT)) {
            CLOG_ERROR("Parse INTERCEPTOR_IPC_CPUIDS failed, value:" << cpuIdsEnv << ".");
            return BIO_INVALID_PARAM;
        }
        CLOG_INFO("Apply INTERCEPTOR_IPC_CPUIDS success, value:" <<
            FormatWorkerGroupCpuIdsRange(netOptions.workerGroupCpuIdsRange) << ".");
    }
    if (!CheckWorkerGroupCpuIdsRangeMatchConnCount(netOptions.workerGroupCpuIdsRange, netOptions.connCount,
        IPC_WORKER_GROUP_CPU_RANGE_COUNT)) {
        CLOG_ERROR("INTERCEPTOR_IPC_CPUIDS not match INTERCEPTOR_IPC_CONN_COUNT, connCount:" <<
            netOptions.connCount << ".");
        return BIO_INVALID_PARAM;
    }
    netOptions.handlerCount = netOptions.connCount;
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
    auto ret = mNetEngine->SyncCall<InterceptorCreateDataMsgMemPoolRequest,
        InterceptorCreateDataMsgMemPoolResponse>(INVALID_NID, BIO_OP_INTERCEPTOR_CREATE_DATA_MSG_MEM_POOL, req, rsp);
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

    auto offset = static_cast<off_t>(mShmOffset);
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
