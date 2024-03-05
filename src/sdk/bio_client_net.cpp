/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include <utility>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <sys/syscall.h>
#include <linux/version.h>

#include "message.h"
#include "message_op.h"
#include "bio_client_log.h"
#include "bio_client_agent.h"
#include "bio_client_net.h"

using namespace ock::bio;
using namespace ock::bio::agent;
using namespace ock::bio::net;

static void Log(int level, const char *msg)
{
    if (level == 0) {
        CLIENT_LOG_DEBUG(msg);
    } else if (level == 1U) {
        CLIENT_LOG_INFO(msg);
    } else if (level == 2U) {
        CLIENT_LOG_WARN(msg);
    } else if (level == 3U) {
        CLIENT_LOG_ERROR(msg);
    }
}

BResult BioClientNet::StartPre(WorkerMode mode)
{
    mMode = mode;
    BResult ret = BIO_OK;
    if (mode == CONVERGENCE) {
        mNetEngine = BioClientAgent::Instance()->GetNetService();
        if (mNetEngine == nullptr) {
            ret = BIO_ERR;
        }
    } else {
        ret = StartIpcService();
    }
    return ret;
}

BResult BioClientNet::StartPost(uint16_t localNid, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView,
    uint16_t protocol)
{
    if (mMode == CONVERGENCE) {
        return BIO_OK;
    }
    mNetEngine->SetLocalNodeId(localNid);

    std::string ipMask;
    uint16_t port = UINT16_MAX;
    for (auto &node : nodeView) {
        if (node.second.id.VNodeId() == localNid) {
            ipMask = node.second.ip;
            port = node.second.port;
            break;
        }
    }
    if (port == UINT16_MAX) {
        CLIENT_LOG_WARN("Not found local node info.");
        return BIO_OK;
    }

    auto ret = StartRpcService((ipMask + "/24"), port, static_cast<ServiceProtocol>(protocol), NO_4);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Bio client start rpc service failed, result:" << ret << ".");
        return ret;
    }

    for (auto &node : nodeView) {
        if (node.second.id.VNodeId() == localNid) {
            continue;
        }
        ConnectInfo info(node.second.id.VNodeId(), node.second.ip, node.second.port, NO_3);
        LOG_INFO("Connect to remote node:" << info.peerId << ", ip:" << info.ip << ", port:" << info.port << ".");
        ret = mNetEngine->SyncConnect(info);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Connect to local bio server failed, result:" << ret << ".");
            return ret;
        }
    }
    return BIO_OK;
}

void BioClientNet::Stop()
{
    if (mMode == SEPARATES) {
        StopInner();
    }
}

BResult BioClientNet::CorrectFd()
{
    int32_t realFd = -1;
    auto result = mNetEngine->ReceiveFds(static_cast<int32_t>(getpid()), &realFd, 1U);
    if (result != BIO_OK) {
        CLIENT_LOG_ERROR("receive file mem fd failed, ret:" << result << ".");
        return BIO_ERR;
    }
    mShmFd = realFd;
    return BIO_OK;
}

BResult BioClientNet::CheckShmFd()
{
    struct stat buffer{};
    auto ret = fstat(mShmFd, &buffer);
    if (ret < 0) {
        CLIENT_LOG_ERROR("Read file fd " << mShmFd << " failed, ret:" << strerror(errno) << ".");
        return BIO_ERR;
    }
    if (mShmOffset + mShmLength > static_cast<uint64_t>(buffer.st_size)) {
        CLIENT_LOG_ERROR("Check file size failed.");
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult BioClientNet::ShmInitInner()
{
    if (CheckShmFd() != BIO_OK) {
        mShmFd = -1;
        return BIO_ERR;
    }
    auto offset = static_cast<off_t>(mShmOffset);
    auto address = mmap(nullptr, mShmLength, PROT_READ | PROT_WRITE, MAP_SHARED, mShmFd, offset);
    if (address == MAP_FAILED) {
        NET_LOG_ERROR("Mmap bio_shm size " << mShmLength << " offset " << offset << " failed, error:" <<
            strerror(errno));
        close(mShmFd);
        mShmFd = -1;
        return BIO_ERR;
    }
    mShmAddr = static_cast<uint8_t *>(address);
    return BIO_OK;
}

BResult BioClientNet::ShmInit()
{
    uint64_t defaultMaxShmSize = (300UL * 1024UL * 1024UL * 1024UL); // 300G
    ShmInitRequest req = {{MESSAGE_MAGIC, 0, 0, 0, getpid()}};
    ShmInitResponse rsp;
    BResult ret = mNetEngine->SyncCall<ShmInitRequest, ShmInitResponse>(getpid(), BIO_OP_SDK_SHM_INIT, req, rsp);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send shm init request failed, ret:" << ret << ".");
        return ret;
    }

    mShmFd = rsp.memFd;
    mServerPid = rsp.serverPid;
    mShmOffset = rsp.offset;
    mShmLength = rsp.length;
    if (mShmOffset != 0 || mShmLength > defaultMaxShmSize) {
        CLIENT_LOG_ERROR("Get share memory para failed, offset:" << mShmOffset << ", length:" << mShmLength << ".");
        return BIO_ERR;
    }

    if (CorrectFd() != BIO_OK) {
        return BIO_ERR;
    }

    if ((ret = ShmInitInner()) != BIO_OK) {
        return ret;
    }

    mNetEngine->SetShmInfo(mShmFd, mShmAddr, mShmOffset, mShmLength);
    CLIENT_LOG_INFO("Bio client shm init success. offset:" << mShmOffset << ", length:" << mShmLength << ", fd:" <<
        mShmFd << ", shmAddr:" << reinterpret_cast<uintptr_t>(mShmAddr));
    return BIO_OK;
}

BResult BioClientNet::StartIpcService()
{
    mNetEngine = MakeRef<NetEngine>();
    if (mNetEngine == nullptr) {
        CLIENT_LOG_ERROR("Make net engine failed.");
        return BIO_ALLOC_FAIL;
    }

    // 1. Initialize net engine
    int16_t timeoutSec = NO_5 * NO_60; // 5min
    auto ret = mNetEngine->Initialize(timeoutSec, NO_128, NO_1024, Log);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Net engine initialize failed, result:" << ret << ".");
        return ret;
    }

    // 2. start ipc service
    NetOptions netOptions;
    netOptions.role = NET_CLIENT;
    netOptions.connCount = NO_4;
    netOptions.handlerCount = NO_4;
    netOptions.protocol = ServiceProtocol::SHM;
    ret = mNetEngine->Start(netOptions);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Start ipc service failed, result:" << ret << ".");
        return ret;
    }

    // 3. listen channel broken event
    ret = ListenEvent();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Listen event failed, result:" << ret << ".");
        return ret;
    }

    // 4. recover ipc service
    ret = RecoverIpcService();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Recover ipc failed, result:" << ret << ".");
        return ret;
    }

    return BIO_OK;
}

BResult BioClientNet::StartRpcService(std::string ipMask, uint16_t port, ServiceProtocol protocol, uint16_t workerNum)
{
    NetOptions netOptions;
    netOptions.ipMask = std::move(ipMask);
    netOptions.port = port;
    netOptions.role = NET_CLIENT;
    netOptions.isBusyLoop = false;
    netOptions.protocol = protocol;
    netOptions.connCount = workerNum;
    netOptions.handlerCount = workerNum;
    return mNetEngine->Start(netOptions);
}

BResult BioClientNet::ListenEvent()
{
    constexpr uint16_t EVENT_THREAD_NUM = 1;
    constexpr uint32_t EVENT_QUEUE_SIZE = 128;

    mEventService = ExecutorService::Create(EVENT_THREAD_NUM, EVENT_QUEUE_SIZE);
    if (UNLIKELY(mEventService == nullptr)) {
        LOG_ERROR("Failed to start event execution service");
        return BIO_ALLOC_FAIL;
    }

    mEventService->SetThreadName("sdk-event");
    auto result = mEventService->Start();
    ChkTrueNot(result, BIO_INNER_ERR);

    auto channelBroken = [this](uint32_t nodeId) -> void {
        if (nodeId == static_cast<uint32_t>(getpid())) {
            mEventService->Execute([this]() { RecoverIpcService(); });
        }
    };
    mNetEngine->RegisterChannelBrokenHandler(channelBroken);

    BIO_OK;
}

BResult BioClientNet::RecoverIpcService()
{
    // 1. connection to local bio server
    ConnectInfo info(static_cast<uint32_t>(getpid()));
    auto ret = mNetEngine->SyncConnect(info);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Connect to local bio server failed, result:" << ret << ".");
        return ret;
    }

    // 2. shm init
    ret = ShmInit();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Get shm info fail, result:" << ret << ".");
        return ret;
    }
    return BIO_OK;
}

void BioClientNet::StopInner()
{
    mNetEngine->Stop();
}

BResult BioClientNet::Rebuild(uint16_t localNid, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView)
{
    if (mMode == CONVERGENCE) {
        return BIO_OK;
    }

    for (auto &node : nodeView) {
        if (node.second.id.VNodeId() == localNid) {
            continue;
        }
        ConnectInfo info(node.second.id.VNodeId(), node.second.ip, node.second.port, NO_3);
        LOG_INFO("Connect to remote node:" << info.peerId << ", ip:" << info.ip << ", port:" << info.port << ".");
        auto ret = mNetEngine->SyncConnect(info);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Connect to local bio server failed, result:" << ret << ".");
            return ret;
        }
    }
    return BIO_OK;
}