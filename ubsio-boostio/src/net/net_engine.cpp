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

#include <iostream>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <climits>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <sys/syscall.h>
#include <linux/version.h>

#include "securec.h"
#include "bio_file_util.h"
#include "bio_ip_util.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"
#include "net_log.h"
#include "net_executor_pool.h"
#include "net_engine.h"

namespace ock {
namespace bio {
using namespace ock::hcom;

constexpr uint16_t WKR_GRP_INDEX_CTRL = 0L;
constexpr uint16_t WKR_GRP_INDEX_DATA = 1L;

BResult NetEngine::Initialize(int16_t timeoutSec, uint32_t coreThreadNum, uint32_t queueSize, NetLogFunc func)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        NET_LOG_WARN("Net engine has been already initialized.");
        return BIO_OK;
    }

    mUsedBlock = 0;

    auto netLog = NetLog::Instance();
    if (netLog == nullptr) {
        NET_LOG_ERROR("Make net log failed.");
        return BIO_ALLOC_FAIL;
    }
    netLog->SetLogFuncFunc(func);
    mTimeout = timeoutSec;

    mCtrlChannelMgr = MakeRef<NetChannelMgr>();
    if (mCtrlChannelMgr == nullptr) {
        NET_LOG_ERROR("Make ctrl channel manager failed.");
        return BIO_ALLOC_FAIL;
    }
    mCtrlChannelMgr->Initialize();

    mDataChannelMgr = MakeRef<NetChannelMgr>();
    if (mDataChannelMgr == nullptr) {
        NET_LOG_ERROR("Make data channel manager failed.");
        return BIO_ALLOC_FAIL;
    }
    mDataChannelMgr->Initialize();

    mConnector = MakeRef<NetConnector>(this);
    if (mConnector == nullptr) {
        NET_LOG_ERROR("Make net connector failed.");
        return BIO_ALLOC_FAIL;
    }
    BResult ret = mConnector->Start();
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to start net connector, ret:" << ret << ".");
        return ret;
    }

    reqExecutorNum = coreThreadNum;
    if (reqExecutorNum != 0) {
        mRequestExecutor = MakeRef<NetExecutorPool>("NetExecutor");
        if (mRequestExecutor == nullptr) {
            NET_LOG_ERROR("Make net request executor failed.");
            return BIO_ALLOC_FAIL;
        }

        ret = mRequestExecutor->Start(reqExecutorNum, queueSize);
        if (ret != BIO_OK) {
            NET_LOG_ERROR("Failed to start request executor, ret:" << ret << ".");
            return ret;
        }
    }
    auto serviceLog = UBSHcomNetOutLogger::Instance();
    if (serviceLog == nullptr) {
        NET_LOG_ERROR("Make net service log fail.");
        return BIO_ALLOC_FAIL;
    }
    serviceLog->SetExternalLogFunction(func);
    mStarted = true;
    return BIO_OK;
}

BResult NetEngine::Start(const NetOptions &opt)
{
    int32_t result = BIO_INNER_ERR;
    if (opt.protocol == ServiceProtocol::SHM || opt.protocol == ServiceProtocol::UDS) {
        result = StartIpcService(opt);
        if (result != BIO_OK) {
            return result;
        }
    }
    if (opt.protocol == ServiceProtocol::TCP || opt.protocol == ServiceProtocol::RDMA) {
        BIO_TP_START(SDK_BIO_NET_START_RPC_FAIL, &result, BIO_INNER_ERR);
        result = StartRpcService(opt);
        BIO_TP_END;
        if (result != BIO_OK) {
            return result;
        }
    }
    return result;
}

void NetEngine::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }
    StopInner();
    mStarted = false;
}

void NetEngine::StopInner()
{
    if (mRequestExecutor != nullptr) {
        mRequestExecutor->Stop();
        mRequestExecutor = nullptr;
    }

    if (mConnector != nullptr) {
        mConnector->Stop();
        mConnector = nullptr;
    }

    mCtrlChannelMgr->UnInitialize();
    mDataChannelMgr->UnInitialize();

    if (mMrBlockPool != nullptr) {
        mMrBlockPool->Stop();
        mMrBlockPool = nullptr;
    }

    if (mRpcService != nullptr) {
        if (!mLocalMr.GetHcomMrs().empty()) {
            mRpcService->DestroyMemoryRegion(mLocalMr);
        }
        mRpcService->Destroy(RPC_SERVICE_NAME);
        mRpcService = nullptr;
    }

    if (mIpcService != nullptr) {
        mIpcService->Destroy(IPC_SERVICE_NAME);
        mIpcService = nullptr;
    }

    mDataChannelMgr = nullptr;
    mCtrlChannelMgr = nullptr;
}

BResult NetEngine::CreateShmFdWithName(int32_t &shmFd, uint64_t size, std::string &name)
{
    int fd = -1;
    BIO_TP_START(SERVER_NET_FAIL_TO_CREATE_MEMORY_FILE, &fd, -1);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
    fd = shm_open(name.c_str(), O_CREAT | O_RDWR | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
#else
    fd = syscall(SYS_memfd_create, name.c_str(), 0);
#endif
    BIO_TP_END;
    if (fd < 0) {
        NET_LOG_ERROR("create memory file " << name << ", failed, error:" << strerror(errno));
        return BIO_INNER_ERR;
    }

    int ret = -1;
    BIO_TP_START(SERVER_NET_FAIL_TO_TRUNCATE_FILE_WITH_SIZE, &ret, -1);
    ret = ftruncate(fd, static_cast<off_t>(size));
    BIO_TP_END;
    if (ret < 0) {
        NET_LOG_ERROR("truncate file " << name << " with size " << size << " failed, error:" << strerror(errno));
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
    shm_unlink(name.c_str());
#endif
        close(fd);
        return BIO_INNER_ERR;
    }

    shmFd = fd;
    return BIO_OK;
}

BResult NetEngine::InitCommMemAllocator()
{
    auto result = RegisterMemoryRegion(mOptions.memorySize, mLocalMr);
    if (result != BIO_OK || mLocalMr.GetHcomMrs().empty()) {
        NET_LOG_ERROR("Failed to register mr by size " << mOptions.memorySize);
        return result;
    }

    mMrBlockPool = MakeRef<NetBlockPool>();
    if (mMrBlockPool == nullptr) {
        NET_LOG_ERROR("Make block pool ptr failed.");
        return BIO_ALLOC_FAIL;
    }

    SetDataPageKb(NO_4 * NO_1024);
    result = mMrBlockPool->Start(mLocalMr.GetAddress(), mDataPageBytes, mOptions.memorySize / mDataPageBytes);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start block pool " << mOptions.memorySize << ".");
    } else {
        NET_LOG_INFO("Succeed to start comm memory pool success, size:" << mOptions.memorySize << ", key:" <<
            mLocalMr.GetHcomMrs()[0]->GetLKey() << ".");
    }
    LOG_INFO("Register common memory success, size:" << mOptions.memorySize << ", Key:"
        << mLocalMr.GetHcomMrs()[0]->GetLKey());
    return result;
}

BResult NetEngine::InitShmMemAllocator()
{
    if (mOptions.memorySize == 0) {
        NET_LOG_INFO("mOption memorySize :" << mOptions.memorySize << ", not need init shm.");
        return BIO_OK;
    }
    std::string shmName = "bio_shm";
    auto result = CreateShmFdWithName(mShmFd, mOptions.memorySize, shmName);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to create shm fd, size:" << mOptions.memorySize << ".");
        return result;
    }

    auto offset = static_cast<off_t>(mShareOffset);
    auto address = MAP_FAILED;
    BIO_TP_START(SERVER_NET_FAIL_TO_MMAP_SHM_SIZE, &address, MAP_FAILED);
    address = mmap(nullptr, mOptions.memorySize, PROT_READ | PROT_WRITE, MAP_SHARED, mShmFd, offset);
    BIO_TP_END;
    if (address == MAP_FAILED) {
        NET_LOG_ERROR("Mmap bio_shm size " << mOptions.memorySize << " offset " << offset << " failed, error:" <<
            strerror(errno));
        close(mShmFd);
        mShmFd = -1;
        return BIO_ERR;
    }
    mShareAddress = static_cast<uint8_t *>(address);
    mShmSize = mOptions.memorySize;

    result = RegisterMemoryRegion(mShareAddress, mOptions.memorySize, mLocalMr);
    if (result != BIO_OK || mLocalMr.GetHcomMrs().empty()) {
        close(mShmFd);
        mShmFd = -1;
        return result;
    }

    mMrBlockPool = MakeRef<NetBlockPool>();
    if (mMrBlockPool == nullptr) {
        NET_LOG_ERROR("Make block pool ptr failed.");
        close(mShmFd);
        mShmFd = -1;
        return BIO_ALLOC_FAIL;
    }
    result = mMrBlockPool->Start(mLocalMr.GetAddress(), mDataPageBytes, mOptions.memorySize / mDataPageBytes);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start block pool " << mOptions.memorySize << ".");
        close(mShmFd);
        mShmFd = -1;
    } else {
        NET_LOG_INFO("Succeed to start share memory pool success, size:" << mOptions.memorySize << ", shmOffset:" <<
            mShareOffset << ", key:" << mLocalMr.GetHcomMrs()[0]->GetLKey() << ".");
    }
    return result;
}

BResult NetEngine::InitMemoryAllocator()
{
    if (mOptions.regShmMem) {
        return InitShmMemAllocator();
    }
    return InitCommMemAllocator();
}

BResult ValidateTlsCert(const NetOptions &opt)
{
    if (opt.caCerPath.empty() || opt.certificationPath.empty() || opt.privateKeyPath.empty()) {
        NET_LOG_ERROR("The path or password for TLS cert is empty");
        return BIO_ERR;
    }
    std::string checkCerPath = opt.caCerPath;
    if (!CanonicalPath(checkCerPath)) {
        NET_LOG_ERROR("TLS cacert path check failed ");
        return BIO_ERR;
    }
    NET_LOG_INFO("TLS cacert path check success");
    checkCerPath = opt.certificationPath;
    if (!CanonicalPath(checkCerPath)) {
        NET_LOG_ERROR("TLS cert path check failed ");
        return BIO_ERR;
    }
    NET_LOG_INFO("TLS cert path check success ");
    checkCerPath = opt.privateKeyPath;
    if (!CanonicalPath(checkCerPath)) {
        NET_LOG_ERROR("TLS privateKey path check failed ");
        return BIO_ERR;
    }
    NET_LOG_INFO("TLS privateKey path check success");
    return BIO_OK;
}

void NetEngine::SetDriverTlsCallback(const NetOptions &options, ock::hcom::UBSHcomTlsOptions &tlsOpt)
{
    auto tlsCertificationCallback = ([&options](const std::string &name, std::string &path) {
        path = options.certificationPath;
        NET_LOG_INFO("Get client cert success.");
        return true;
    });
    tlsOpt.cfCb = tlsCertificationCallback;

    auto tlsCaCallback= ([&options](const std::string &name, std::string &capath, std::string &crlPath,
        UBSHcomPeerCertVerifyType &verifyPeerCert, UBSHcomTLSCertVerifyCallback &cb) {
        capath = options.caCerPath;
        if (!options.caCrlPath.empty()) {
            crlPath = options.caCrlPath;
            NET_LOG_INFO("Get cacrl cert path success.");
        }
        NET_LOG_INFO("Get CA cert success.");
        verifyPeerCert = UBSHcomPeerCertVerifyType::VERIFY_BY_DEFAULT;
        cb = [](void *, const char *) { return 0; };
        return true;
    });
    tlsOpt.caCb = tlsCaCallback;

    auto tlsPrivateKeyCallback = (
            [this, &options](const std::string &name, std::string &path, void *&pwd, int &len,
                             UBSHcomTLSEraseKeypass &erase) {
                std::vector<char> encryptedKeyPass(KEYPASS_MAX_LEN, 0);
                std::ifstream fileStream(options.privateKeyPassword);
                if (!fileStream.is_open()) {
                    LOG_ERROR("Failed to open keyPassFile: " << options.privateKeyPassword);
                    return false;
                }

                if (!fileStream.getline(encryptedKeyPass.data(), KEYPASS_MAX_LEN)) {
                    LOG_ERROR("Failed to read keyPassFile");
                    return false;
                }

                size_t actualLen = strlen(encryptedKeyPass.data());
                std::vector<char> plainTextBuffer(KEYPASS_MAX_LEN, 0);
                size_t plainTextLen = KEYPASS_MAX_LEN;
                auto ret = mDecryptHandler(encryptedKeyPass.data(), actualLen, plainTextBuffer.data(), &plainTextLen);
                if (ret != 0) {
                    std::fill(plainTextBuffer.begin(), plainTextBuffer.end(), 0);
                    LOG_ERROR("Decrypt failed with error: " << ret);
                    return false;
                }

                path = options.privateKeyPath;
                pwd = malloc(plainTextLen);
                len = static_cast<int>(plainTextLen);
                if (!pwd) {
                    std::fill(plainTextBuffer.begin(), plainTextBuffer.end(), 0);
                    LOG_ERROR("Memory allocation failed.");
                    return false;
                }

                ret = memcpy_s(pwd, plainTextLen, plainTextBuffer.data(), plainTextLen);
                if (ret != 0) {
                    std::fill(plainTextBuffer.begin(), plainTextBuffer.end(), 0);
                    free(pwd);
                    pwd = nullptr;
                    LOG_ERROR("Memory copy failed.");
                    return false;
                }

                erase = [](void *pass, int len) {
                    if (pass && len > 0) {
                        (void)memset_s(pass, len, 0, len);
                        free(pass);
                        pass = nullptr;
                    }
                };

                std::fill(plainTextBuffer.begin(), plainTextBuffer.end(), 0);
                return true;
            });
    tlsOpt.pkCb = tlsPrivateKeyCallback;
}

BResult NetEngine::AssignIpcServiceOptions(const NetOptions &opt, bool isOobSvr)
{
    std::pair<uint32_t, uint32_t> workerGroupCpuIdsRange = {UINT32_MAX, UINT32_MAX};
    mIpcService->AddWorkerGroup(1, opt.handlerCount, workerGroupCpuIdsRange);

    UBSHcomHeartBeatOptions hbOpt;
    hbOpt.heartBeatIdleSec = NO_5;
    hbOpt.heartBeatProbeIntervalSec = 1;
    mIpcService->SetHeartBeatOptions(hbOpt);
    mIpcService->SetMaxSendRecvDataCount(NO_1024);
    mIpcService->SetSendQueueSize(NO_128);
    mIpcService->SetCompletionQueueDepth(NO_8192);
    mIpcService->SetPollingBatchSize(NO_16);
    mIpcService->SetQueuePrePostSize(NO_64);
    mIpcService->SetEventPollingTimeOutUs(NO_1000);
    mIpcService->SetMaxConnectionCount(NO_4096 * NO_1024);
    if (isOobSvr) {
        const std::string listenerUrl = "uds://" + UDS_NAME;
        auto ret = mIpcService->Bind(listenerUrl, std::bind(&NetEngine::NewChannel, this, std::placeholders::_1,
                                                          std::placeholders::_2, std::placeholders::_3));
        if (UNLIKELY(ret != BIO_OK)) {
            NET_LOG_ERROR("Net tls callback has created.");
            return ret;
        }
    }

    UBSHcomTlsOptions tlsOpt;
    tlsOpt.enableTls = opt.enableTls;
    if (opt.enableTls) {
        if (ValidateTlsCert(mOptions) != BIO_OK) {
            NET_LOG_ERROR("Failed to enable Ipc TLS service , enableTls:" << opt.enableTls << ".");
            return BIO_ERR;
        }
        SetDriverTlsCallback(mOptions, tlsOpt);
        NET_LOG_INFO("Net tls callback has created.");
    }
    mIpcService->SetTlsOptions(tlsOpt);

    mIpcService->RegisterChannelBrokenHandler(std::bind(&NetEngine::ChannelBroken, this, std::placeholders::_1),
                                              UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    if (reqExecutorNum > NO_32) {
        mIpcService->RegisterRecvHandler(std::bind(&NetEngine::RequestReceived, this, std::placeholders::_1));
    } else {
        mIpcService->RegisterRecvHandler(std::bind(&NetEngine::RequestIPCReceived, this, std::placeholders::_1));
    }
    mIpcService->RegisterSendHandler(std::bind(&NetEngine::RequestPosted, this, std::placeholders::_1));
    mIpcService->RegisterOneSideHandler(std::bind(&NetEngine::OneSideDone, this, std::placeholders::_1));
    return BIO_OK;
}

BResult NetEngine::StartIpcService(const NetOptions &opt)
{
    int result = BIO_ERR;

    if (mIpcService != nullptr) {
        NET_LOG_INFO("Net ipc service has already created.");
        return BIO_OK;
    }
    bool isOobSvr = opt.role != Role::NET_CLIENT;
    if (!isOobSvr) {
        mOptions = opt;
    }

    UBSHcomServiceOptions options;
    options.maxSendRecvDataSize = (NO_64 * NO_1024);
    options.workerGroupId = 0;
    options.workerGroupThreadCount = opt.handlerCount;
    options.workerThreadPriority = 0;
    options.workerGroupMode = opt.isBusyLoop ? UBSHcomNetDriverWorkingMode::NET_BUSY_POLLING :
                              UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    mIpcService = UBSHcomService::Create(opt.protocol, IPC_SERVICE_NAME, options);
    if (mIpcService == nullptr) {
        NET_LOG_ERROR("Failed to create ipc service instance, protocol:" << opt.protocol << ".");
        return BIO_ERR;
    }

    if (opt.enableTls) {
        result = PrepareTlsDecrypter(opt);
        if (result != BIO_OK) {
            NET_LOG_ERROR("Failed to prepare tls decrypter, result:" << result << ".");
            return result;
        }
    }

    result = AssignIpcServiceOptions(opt, isOobSvr);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to assign ipc service options, result:" << UBSHcomNetErrStr(result) << ".");
        return result;
    }

    result = mIpcService->Start();
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start ipc service, result:" << UBSHcomNetErrStr(result) << ".");
        return BIO_ERR;
    }

    NET_LOG_INFO("Bio server Start ipc service success, protocol:" << opt.protocol << ".");
    return BIO_OK;
}

BResult NetEngine::AssignRpcServiceOptions(const NetOptions &opt, bool isOobSvr)
{
    std::string ipMask = mOptions.ipMask;
    auto port = mOptions.port;
    std::vector<std::string> goodIps;
    if (!IpUtil::FilterIpByMask(ipMask, goodIps) || goodIps.empty()) {
        NET_LOG_ERROR("Failed to find ip with ip mask " << ipMask);
        return BIO_ERR;
    }

    std::pair<uint32_t, uint32_t> workerGroupCpuIdsRange = {UINT32_MAX, UINT32_MAX};
    mRpcService->AddWorkerGroup(1, opt.handlerCount, workerGroupCpuIdsRange);

    UBSHcomHeartBeatOptions hbOpt;
    hbOpt.heartBeatIdleSec = NO_1;
    hbOpt.heartBeatProbeTimes = NO_1;
    hbOpt.heartBeatProbeIntervalSec = NO_1;
    mRpcService->SetHeartBeatOptions(hbOpt);
    mRpcService->SetMaxSendRecvDataCount(NO_1024);
    mRpcService->SetSendQueueSize(NO_4096);
    mRpcService->SetRecvQueueSize(NO_2048);
    mRpcService->SetCompletionQueueDepth(NO_2048);
    mRpcService->SetPollingBatchSize(NO_1024);
    mRpcService->SetQueuePrePostSize(NO_1024);
    mRpcService->SetTcpUserTimeOutSec(NO_3);
    mRpcService->SetDeviceIpMask({ipMask});

    if (isOobSvr) {
        const std::string listenerUrl = "tcp://" + goodIps.at(0) + ":" + std::to_string(port);
        auto ret = mRpcService->Bind(listenerUrl, std::bind(&NetEngine::NewChannel, this, std::placeholders::_1,
                                                            std::placeholders::_2, std::placeholders::_3));
        if (UNLIKELY(ret != BIO_OK)) {
            NET_LOG_ERROR("Net tls callback has created.");
            return ret;
        }
    }

    UBSHcomTlsOptions tlsOpt;
    tlsOpt.enableTls = opt.enableTls;
    if (opt.enableTls) {
        if (ValidateTlsCert(mOptions) != BIO_OK) {
            NET_LOG_ERROR("Failed to enable Ipc TLS service.");
            return BIO_ERR;
        }
        SetDriverTlsCallback(mOptions, tlsOpt);
        NET_LOG_INFO("Net tls callback has created.");
    }
    mRpcService->SetTlsOptions(tlsOpt);
    mRpcService->RegisterChannelBrokenHandler(std::bind(&NetEngine::ChannelBroken, this, std::placeholders::_1),
                                              UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    mRpcService->RegisterRecvHandler(std::bind(&NetEngine::RequestReceived, this, std::placeholders::_1));
    mRpcService->RegisterSendHandler(std::bind(&NetEngine::RequestPosted, this, std::placeholders::_1));
    mRpcService->RegisterOneSideHandler(std::bind(&NetEngine::OneSideDone, this, std::placeholders::_1));
    return BIO_OK;
}

BResult NetEngine::StartRpcService(const NetOptions &opt)
{
    int result = BIO_ERR;

    if (mRpcService != nullptr) {
        NET_LOG_INFO("Net rpc service has already created.");
        return BIO_OK;
    }
    mOptions = opt;
    bool isOobSvr = opt.role != Role::NET_CLIENT;
    UBSHcomServiceOptions options;
    options.maxSendRecvDataSize = isOobSvr ? (NO_256 * NO_1024) : (NO_16 * NO_1024);
    options.workerGroupId = 0;
    options.workerGroupThreadCount = opt.handlerCount;
    options.workerThreadPriority = 0;
    options.workerGroupMode = opt.isBusyLoop ? UBSHcomNetDriverWorkingMode::NET_BUSY_POLLING :
                              UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    mRpcService = UBSHcomService::Create(opt.protocol, RPC_SERVICE_NAME, options);
    if (mRpcService == nullptr) {
        NET_LOG_ERROR("Failed to create rpc service instance, protocol:" << opt.protocol << ".");
        return BIO_ERR;
    }

    if (opt.enableTls) {
        result = PrepareTlsDecrypter(opt);
        if (result != BIO_OK) {
            NET_LOG_ERROR("Failed to prepare tls decrypter, result:" << result << ".");
            return result;
        }
    }

    result = AssignRpcServiceOptions(opt, isOobSvr);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to assign rpc service options, result:" << UBSHcomNetErrStr(result) << ".");
        return BIO_ERR;
    }

    result = mRpcService->Start();
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start rpc service, result:" << UBSHcomNetErrStr(result) << ".");
        return BIO_ERR;
    }

    result = InitMemoryAllocator();
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to init mr allocator, result:" << UBSHcomNetErrStr(result) << ".");
        return BIO_ERR;
    }

    NET_LOG_INFO("Bio server start rpc service success, protocol:" << opt.protocol << ".");
    return BIO_OK;
}

int32_t NetEngine::NewChannel(const std::string &ipPort, const ChannelPtr &newChannel, const std::string &payload)
{
    if (newChannel == nullptr) {
        NET_LOG_ERROR("Invalid input parameter, newChannel is nullptr.");
        return BIO_ERR;
    }

    NetConnPayload netPayload;
    bool isCtrl = true;
    if (netPayload.FromPayloadStr(payload, isCtrl) != BIO_OK) {
        NET_LOG_ERROR("Failed to parse payload:" << payload << ".");
        return BIO_ERR;
    }

    NetChannelUpCtx ctx(netPayload.srcNodeId, isCtrl, true);
    newChannel->SetUpCtx(ctx.whole);
#ifndef DEBUG_UT
    newChannel->SetChannelTimeOut(mTimeout, mTimeout);
#endif
    if (netPayload.srcNodeId.pid == 0) {
        NET_LOG_INFO("Receive new channel, not needed add, channel: " << newChannel->GetId() << ", peer connected nid: "
            << netPayload.srcNodeId.nid << " pid: " << netPayload.srcNodeId.pid << ", ip: " << ipPort << ", payload " <<
            payload << ".");
        return BIO_OK;
    }

    if (isCtrl) {
        mCtrlChannelMgr->AddChannel(netPayload.srcNodeId, const_cast<ChannelPtr &>(newChannel), 0);
    } else {
        mDataChannelMgr->AddChannel(netPayload.srcNodeId, const_cast<ChannelPtr &>(newChannel), 1);
    }
    NET_LOG_INFO("Receive new channel " << newChannel->GetId() << ", nodeId:" << netPayload.srcNodeId.nid << ", pid:" <<
        netPayload.srcNodeId.pid << ", ip:" << ipPort << ", payload " << payload << ".");
    return BIO_OK;
}

void NetEngine::ChannelBroken(const ChannelPtr &ch)
{
    if (ch == nullptr) {
        NET_LOG_ERROR("Invalid input parameter, ch is nullptr.");
        return;
    }

    NetChannelUpCtx ctx(ch->GetUpCtx());
    NetNode dstNid(static_cast<uint32_t>(ctx.peerId), ctx.procId);
    NET_LOG_WARN("Receive broken channel: " << ch->GetId() << ", nodeId: " << dstNid.nid << ", pid: " << dstNid.pid <<
        ", panel: " << ctx.IsCtrlPanel() << ".");

    NetChannelMgrPtr mgr = ctx.IsCtrlPanel() ? mCtrlChannelMgr : mDataChannelMgr;
    if (mgr != nullptr) {
        mgr->RemoveChannel(dstNid, ch);
    }

    if (mHandlerBroken != nullptr) {
        mHandlerBroken(dstNid.nid, dstNid.pid);
    }
}

int32_t NetEngine::RequestReceived(ServiceContext &ctx)
{
    if (UNLIKELY(mRequestExecutor == nullptr)) {
        NET_LOG_ERROR("Net request executor not ready.");
        return BIO_NOT_READY;
    }
    if (UNLIKELY(ctx.OpCode() >= MAX_NEW_REQ_HANDLER)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << ctx.OpCode());
        return BIO_ERR;
    }
    auto &handler = mHandlers[ctx.OpCode()];
    if (UNLIKELY(handler == nullptr)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << ctx.OpCode() <<
            " as no handler registered");
        return BIO_ERR;
    }
    BIO_TRACE_START(NET_TRACE_SHEDULE);
    auto ret = mRequestExecutor->AddTask(handler, ctx);
    BIO_TRACE_END(NET_TRACE_SHEDULE, ret);
    return ret;
}

int32_t NetEngine::RequestIPCReceived(ServiceContext &ctx)
{
    uint16_t opCode = ctx.OpCode();
    BIO_TP_START(SDK_REQUEST_IPC_OPCODE_EXCEED, &opCode, MAX_NEW_REQ_HANDLER);
    BIO_TP_END;
    if (UNLIKELY(opCode >= MAX_NEW_REQ_HANDLER)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << opCode);
        return BIO_ERR;
    }
    auto &handler = mHandlers[opCode];
    if (UNLIKELY(handler == nullptr)) {
        NET_LOG_ERROR("Net engine received a message with invalid opCode " << opCode << ".");
        return BIO_ERR;
    }
    BIO_TRACE_START(NET_TRACE_IPC_HDL);
    auto ret = handler(ctx);
    BIO_TRACE_END(NET_TRACE_IPC_HDL, ret);
    return ret;
}

int32_t NetEngine::RequestPosted(const ServiceContext &ctx)
{
    return BIO_OK;
}

int32_t NetEngine::OneSideDone(const ServiceContext &ctx)
{
    return BIO_OK;
}

void NetEngine::FillConnectOption(ConnectInfo &info, bool isCtrl, std::string &prefix, UBSHcomConnectOptions &op)
{
    op.linkCount = mOptions.connCount;
    if (isCtrl) {
        op.clientGroupId = WKR_GRP_INDEX_CTRL;
        op.serverGroupId = WKR_GRP_INDEX_CTRL;
        prefix = CONN_PAYLOAD_PREFIX_CTRL;
    } else {
        op.clientGroupId = WKR_GRP_INDEX_DATA;
        op.serverGroupId = WKR_GRP_INDEX_DATA;
        prefix = CONN_PAYLOAD_PREFIX_DATA;
    }
    if (info.isSelfPoll) {
        op.mode = UBSHcomClientPollingMode::SELF_POLL_BUSY;
    }
    NetConnPayload payload(info.srcId);
    op.payload = payload.ToPayloadStr(prefix);
}

BResult NetEngine::ConnectToPeer(ConnectMode mode, ConnectInfo &info, bool isCtrlPanel, ChannelPtr &ch)
{
    UBSHcomService *netService = (mode == ConnectMode::CONNECT_IPC) ? mIpcService : mRpcService;
    if (netService == nullptr) {
        NET_LOG_ERROR("Net service not ready.");
        return BIO_ERR;
    }

    UBSHcomConnectOptions options;
    std::string prefix;
    FillConnectOption(info, isCtrlPanel, prefix, options);
    int32_t result = 0;
    for (uint16_t i = 0; i < info.retryTimes; ++i) {
        if (mode == ConnectMode::CONNECT_IPC) {
            const std::string serverUrl = "uds://" + UDS_NAME;
#ifndef DEBUG_UT
            result = netService->Connect(serverUrl, ch, options);
#else
            result = NetStub::Connect(serverUrl, ch, options);
#endif
        } else {
            const std::string serverUrl = "tcp://" + info.ip + ":" + std::to_string(info.port);
#ifndef DEBUG_UT
            result = netService->Connect(serverUrl, ch, options);
#else
            result = NetStub::Connect(serverUrl, ch, options);
#endif
        }
        if (result == 0) {
            NET_LOG_INFO("Connect to peer success, ip " << info.ip << ", port " << info.port << ", dstNid " <<
                info.peerId.nid << ", payload " << options.payload << ".");
            break;
        }
    }
    if (result != 0) {
        NET_LOG_ERROR("Connect to peer failed, ret:" << UBSHcomNetErrStr(result) << ", ip " << info.ip << ", port " <<
            info.port << ", nid " << info.peerId.nid << ", pid " << info.peerId.pid << ".");
        return result;
    }

    NetChannelUpCtx ctx(info.peerId, isCtrlPanel, false);
#ifndef DEBUG_UT
    ch->SetUpCtx(ctx.whole);
    ch->SetChannelTimeOut(mTimeout, mTimeout);
#endif
    return BIO_OK;
}

BResult NetEngine::PrepareTlsDecrypter(const NetOptions &config)
{
    const auto decrypter = TlsUtil::LoadDecryptFunction(config.decrypterLibPath.c_str());
    if (decrypter == nullptr) {
        LOG_ERROR("Failed to load customized decrypt function.");
        return BIO_INVALID_PARAM;
    }

    RegisterDecryptHandler(decrypter);
    return BIO_OK;
}

}
}