/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#include <utility>
#include <cerrno>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
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

BResult BioClientNet::StartPre(WorkerMode mode, const NetOptions netConf)
{
    mMode = mode;
    BResult ret = BIO_OK;
    if (mode == CONVERGENCE) {
        mNetEngine = BioClientAgent::Instance()->GetNetService();
        if (mNetEngine == nullptr) {
            ret = BIO_INNER_ERR;
        }
    } else {
        ret = StartIpcService(netConf);
    }
    return ret;
}

BResult BioClientNet::StartPost(uint16_t localNid, std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> &nodeView,
    uint16_t protocol, const NetOptions netConf)
{
    LVOS_TP_START(SDK_BIO_START_POST_CHANGE_MODE, &mMode, SEPARATES);
    LVOS_TP_END;
    if (mMode == CONVERGENCE) {
        return BIO_OK;
    }
    mLocalNid = localNid;
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
        CLIENT_LOG_ERROR("Not found local node info.");
        return BIO_OK;
    }

    auto ret = StartRpcService((ipMask + "/24"), port, static_cast<ServiceProtocol>(protocol), NO_4, netConf);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Bio client start rpc service failed, result:" << ret << ".");
        return ret;
    }

    for (auto &node : nodeView) {
        LVOS_TP_START(SDK_BIO_START_POST_IGNORE_CHECK, 0);
        if (node.second.id.VNodeId() == localNid) {
            continue;
        }
        LVOS_TP_END;
        ConnectInfo info(localNid, static_cast<uint32_t>(getpid()), node.second.id.VNodeId(), node.second.ip,
            node.second.port, NO_1);
        CLIENT_LOG_INFO("Connect to remote node:" << info.peerId.nid << ", ip:" << info.ip << ", port:" << info.port <<
            ".");
        LVOS_TP_START(SDK_BIO_NET_START_CONNECT_FAIL, &ret, BIO_INNER_ERR);
        ret = mNetEngine->SyncConnect(info);
        LVOS_TP_END;
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Connect to bio server failed, result:" << ret << ".");
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
    auto result = mNetEngine->ReceiveFds(INVALID_NID, &realFd, 1U);
    if (result != BIO_OK) {
        CLIENT_LOG_ERROR("receive file mem fd failed, ret:" << result << ".");
        return BIO_ERR;
    }
    mShmFd = realFd;
    return BIO_OK;
}

BResult BioClientNet::CheckShmFd()
{
    struct stat buffer {};
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
    ShmInitRequest req = { { MESSAGE_MAGIC, 0, 0, INVALID_NID, getpid() } };
    ShmInitResponse rsp;
    BResult ret = mNetEngine->SyncCall<ShmInitRequest, ShmInitResponse>(INVALID_NID, BIO_OP_SDK_SHM_INIT, req, rsp);
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Send shm init request failed, ret:" << ret << ".");
        return ret;
    }

    mScene = rsp.scene;
    mShmFd = rsp.memFd;
    mServerPid = rsp.serverPid;
    mShmOffset = rsp.offset;
    mShmLength = rsp.length;
    mShmKey = rsp.mKey;
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
        mShmFd << ".");
    return BIO_OK;
}

BResult BioClientNet::StartIpcService(const NetOptions netConf)
{
    mNetEngine = MakeRef<NetEngine>();
    if (mNetEngine == nullptr) {
        CLIENT_LOG_ERROR("Make net engine failed.");
        return BIO_ALLOC_FAIL;
    }

    // 1. Initialize net engine
    int16_t timeoutSec = NO_32; // 32s
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
    netOptions.enableTls = netConf.enableTls;
    netOptions.certificationPath = netConf.certificationPath;    /* certification path */
    netOptions.caCerPath = netConf.caCerPath;                    /* caCer path */
    netOptions.caCrlPath = netConf.caCrlPath;                    /* caCrl path */
    netOptions.privateKeyPath = netConf.privateKeyPath;          /* private key path */
    netOptions.privateKeyPassword = netConf.privateKeyPassword;  /* private key password */
    netOptions.hseKfsMasterPath = netConf.hseKfsMasterPath;      /* hseceasy kfs master path */
    netOptions.hseKfsStandbyPath = netConf.hseKfsStandbyPath;    /* hseceasy kfs standby path */
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
    }
    return ret;
}

BResult BioClientNet::StartRpcService(std::string ipMask, uint16_t port, ServiceProtocol protocol, uint16_t workerNum,
    const NetOptions netConf)
{
    const uint64_t defaultMemorySize = (128UL * 1024UL * 1024UL); // 128M
    NetOptions netOptions;
    netOptions.ipMask = std::move(ipMask);
    netOptions.port = port;
    netOptions.role = NET_CLIENT;
    netOptions.isBusyLoop = false;
    netOptions.memorySize = defaultMemorySize;
    netOptions.protocol = protocol;
    netOptions.connCount = workerNum;
    netOptions.handlerCount = workerNum;
    netOptions.enableTls = netConf.enableTls;
    netOptions.certificationPath = netConf.certificationPath;    /* certification path */
    netOptions.caCerPath = netConf.caCerPath;                    /* caCer path */
    netOptions.caCrlPath = netConf.caCrlPath;                    /* caCrl path */
    netOptions.privateKeyPath = netConf.privateKeyPath;          /* private key path */
    netOptions.privateKeyPassword = netConf.privateKeyPassword;  /* private key password */
    netOptions.hseKfsMasterPath = netConf.hseKfsMasterPath;      /* hseceasy kfs master path */
    netOptions.hseKfsStandbyPath = netConf.hseKfsStandbyPath;    /* hseceasy kfs standby path */
    return mNetEngine->Start(netOptions);
}

BResult BioClientNet::ListenEvent()
{
    auto channelBroken = [this](uint32_t nodeId, uint32_t pid) -> void {
        std::thread t([this, nodeId]() {
            if (nodeId == INVALID_NID) {
                RecoverIpc();
            } else {
                RecoverRpc(nodeId);
            }
        });
        t.detach();
    };
    mNetEngine->RegisterChannelBrokenHandler(channelBroken);

    return BIO_OK;
}

void BioClientNet::RecoverIpc()
{
    constexpr uint16_t RECOVER_INTERAL = 2;
    uint32_t retryCnt = 0;
    BResult ret;
    do {
        ret = RecoverIpcService();
        if (ret != BIO_OK) {
            CLIENT_LOG_WARN("Delay retry connect, retry cnt:" << retryCnt++);
            sleep(RECOVER_INTERAL);
        }
    } while (ret != BIO_OK);
}

BResult BioClientNet::RecoverIpcService()
{
    // 1. connection to local bio server
    ConnectInfo info(INVALID_NID, static_cast<uint32_t>(getpid()), INVALID_NID);
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
    mLocalNid = localNid;
    for (auto &node : nodeView) {
        if (node.second.id.VNodeId() == localNid) {
            continue;
        }
        if (node.second.status != CM_NODE_NORMAL) {
            continue;
        }
        ConnectInfo info(localNid, static_cast<uint32_t>(getpid()), node.second.id.VNodeId(), node.second.ip,
            node.second.port, NO_1);
        CLIENT_LOG_INFO("Connect to remote node:" << info.peerId.nid << ", ip:" << info.ip << ", port:" << info.port <<
            ".");
        auto handler = [this](uintptr_t userCtx, int32_t ret, ConnectInfo &info) -> void {
            if (ret != BIO_OK) {
                RecoverRpc(info.peerId.nid);
            }
        };
        auto ret = mNetEngine->AsyncConnect(info, handler, 0);
        if (ret != BIO_OK) {
            CLIENT_LOG_ERROR("Connect to local bio server failed, result:" << ret << ".");
            return ret;
        }
    }
    return BIO_OK;
}

void BioClientNet::RecoverRpc(uint32_t peerId)
{
    if (mCheckOnLine == nullptr) {
        CLIENT_LOG_WARN("Unable check node isOnline, peer id:" << peerId << ".");
        return;
    }
    std::string ip;
    uint16_t port;
    if (!mCheckOnLine(peerId, ip, port)) {
        CLIENT_LOG_WARN("Target peer id:" << peerId << " is offline.");
        return;
    }
    CLIENT_LOG_INFO("ReConnect to remote node:" << peerId << ", ip:" << ip << ", port:" << port << ".");
    sleep(NO_2);
    ConnectInfo info(mLocalNid, static_cast<uint32_t>(getpid()), peerId, ip, port, NO_1);
    auto handler = [this](uintptr_t userCtx, int32_t ret, ConnectInfo &info) -> void {
        if (ret != BIO_OK) {
            RecoverRpc(info.peerId.nid);
        }
    };
    BResult result = mNetEngine->AsyncConnect(info, handler, 0);
    if (result != BIO_OK) {
        CLIENT_LOG_ERROR("Connect to " << info.peerId.nid << " failed, ret: " << result << ".");
    }
    return;
}