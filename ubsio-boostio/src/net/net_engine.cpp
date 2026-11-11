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
namespace {
std::atomic<NetEngine *> gIpcEngine{nullptr};
std::atomic<NetEngine *> gRpcEngine{nullptr};
constexpr const char *UNBOUND_CPU_RANGE = "4294967295-4294967295";
NetEngine *TlsEngine(const char *name)
{
    if (name == nullptr) {
        return nullptr;
    }
    return name == IPC_SERVICE_NAME ? gIpcEngine.load() :
        (name == RPC_SERVICE_NAME ? gRpcEngine.load() : nullptr);
}
}

constexpr uint16_t WKR_GRP_INDEX_CTRL = 0L;
constexpr uint16_t WKR_GRP_INDEX_DATA = 1L;

BResult NetEngine::Initialize(int16_t timeoutSec, uint32_t coreThreadNum, uint32_t queueSize, NetLogFunc func)
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        NET_LOG_WARN("Net engine has been already initialized.");
        return BIO_OK;
    }

    if (!HcomApi::Instance().Load()) {
        return BIO_ERR;
    }
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
    HcomApi::Instance().ubs_hcom_set_log_handler(func);
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

    if (mRpcService != 0) {
        if (mLocalMr.IsValid()) {
            DestroyMemoryRegion(mLocalMr);
        }
        HcomApi::Instance().ubs_hcom_service_destroy(mRpcService, RPC_SERVICE_NAME.c_str());
        gRpcEngine = nullptr;
        mRpcService = 0;
    }

    if (mIpcService != 0) {
        HcomApi::Instance().ubs_hcom_service_destroy(mIpcService, IPC_SERVICE_NAME.c_str());
        gIpcEngine = nullptr;
        mIpcService = 0;
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

void NetEngine::DestroyShmFdWithPid(int32_t &shmFd, uint8_t *addr, uint32_t pid, uint64_t size)
{
    if (munmap(addr, size) == -1) {
        NET_LOG_ERROR("munmap address failed.");
        return;
    }
    close(shmFd);
#if LINUX_VERSION_CODE < KERNEL_VERSION(NO_3, NO_17, 0)
    std::string shmName = "bio_data_msg_mem_pool" + std::to_string(pid);
    shm_unlink(shmName.c_str());
#endif
}

BResult NetEngine::InitCommMemAllocator()
{
    if (mLocalMr.IsValid() && mMrBlockPool != nullptr) {
        return BIO_OK;
    }
    auto result = RegisterMemoryRegion(mOptions.memorySize, mLocalMr);
    if (result != BIO_OK || !mLocalMr.IsValid()) {
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
            mLocalMr.GetKey() << ".");
    }
    LOG_INFO("Register common memory success, size:" << mOptions.memorySize << ", Key:"
        << mLocalMr.GetKey());
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
    if (result != BIO_OK || !mLocalMr.IsValid()) {
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
            mShareOffset << ", key:" << mLocalMr.GetKey() << ".");
    }
    return result;
}

BResult NetEngine::InitMemoryAllocator()
{
    if (mOptions.isCreateMemPool) {
        return InitShmMemAllocator(); // 创建server端的内存分配器
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
    if (!FileUtil::CanonicalPath(checkCerPath)) {
        NET_LOG_ERROR("TLS cacert path check failed ");
        return BIO_ERR;
    }
    NET_LOG_INFO("TLS cacert path check success");
    checkCerPath = opt.certificationPath;
    if (!FileUtil::CanonicalPath(checkCerPath)) {
        NET_LOG_ERROR("TLS cert path check failed ");
        return BIO_ERR;
    }
    NET_LOG_INFO("TLS cert path check success ");
    checkCerPath = opt.privateKeyPath;
    if (!FileUtil::CanonicalPath(checkCerPath)) {
        NET_LOG_ERROR("TLS privateKey path check failed ");
        return BIO_ERR;
    }
    NET_LOG_INFO("TLS privateKey path check success");
    return BIO_OK;
}

BResult NetEngine::RegisterMemoryRegionInner(uintptr_t address, uint64_t size, MemoryRegion &mr)
{
    auto &api = HcomApi::Instance();
    if (mr.IsValid()) {
        return BIO_INVALID_PARAM;
    }
    int ret = address == 0 ? api.ubs_hcom_service_register_memory_region(mRpcService, size, &mr.handle) :
        api.ubs_hcom_service_register_assign_memory_region(mRpcService, address, size, &mr.handle);
    if (ret != BIO_OK) {
        return ret;
    }
    ret = api.ubs_hcom_service_get_memory_region_info(mr.handle, &mr.info);
    if (ret != BIO_OK) {
        api.ubs_hcom_service_destroy_memory_region(mRpcService, mr.handle);
        mr = MemoryRegion{};
    }
    return ret;
}

int NetEngine::NewIpcChannel(ubs_hcom_channel channel, const char *ipPort, uint64_t, const char *payload)
{
    auto ch = MakeNetChannel(channel, true);
    auto *engine = gIpcEngine.load();
    return engine == nullptr ? BIO_ERR : engine->NewChannel(ipPort, ch, payload);
}

int NetEngine::NewRpcChannel(ubs_hcom_channel channel, const char *ipPort, uint64_t, const char *payload)
{
    auto ch = MakeNetChannel(channel, true);
    auto *engine = gRpcEngine.load();
    return engine == nullptr ? BIO_ERR : engine->NewChannel(ipPort, ch, payload);
}

int NetEngine::BrokenChannel(ubs_hcom_channel channel, const char *, uint64_t ctx, const char *)
{
    reinterpret_cast<NetEngine *>(ctx)->ChannelBroken(MakeNetChannel(channel));
    return BIO_OK;
}

int NetEngine::ReceiveRequest(ubs_hcom_service_context handle, uint64_t ctx)
{
    ServiceContext context(handle);
    return reinterpret_cast<NetEngine *>(ctx)->RequestReceived(context);
}

int NetEngine::ReceiveIpcRequest(ubs_hcom_service_context handle, uint64_t ctx)
{
    ServiceContext context(handle);
    return reinterpret_cast<NetEngine *>(ctx)->RequestIPCReceived(context);
}

int NetEngine::PostedRequest(ubs_hcom_service_context handle, uint64_t ctx)
{
    ServiceContext context(handle);
    return reinterpret_cast<NetEngine *>(ctx)->RequestPosted(context);
}

int NetEngine::CompletedOneSide(ubs_hcom_service_context handle, uint64_t ctx)
{
    ServiceContext context(handle);
    return reinterpret_cast<NetEngine *>(ctx)->OneSideDone(context);
}

int NetEngine::TlsCertificate(const char *name, char **path)
{
    auto *engine = TlsEngine(name);
    if (engine == nullptr || path == nullptr) {
        return BIO_ERR;
    }
    *path = const_cast<char *>(engine->mOptions.certificationPath.c_str());
    return BIO_OK;
}

int NetEngine::TlsCa(const char *name, char **path, char **crl, ubs_hcom_peer_cert_verify_type *type,
    ubs_hcom_tls_cert_verify *verify)
{
    auto *engine = TlsEngine(name);
    if (engine == nullptr || path == nullptr || crl == nullptr || type == nullptr || verify == nullptr) {
        return BIO_ERR;
    }
    *path = const_cast<char *>(engine->mOptions.caCerPath.c_str());
    *crl = engine->mOptions.caCrlPath.empty() ? nullptr :
        const_cast<char *>(engine->mOptions.caCrlPath.c_str());
    *type = C_VERIFY_BY_DEFAULT;
    *verify = nullptr;
    return BIO_OK;
}

void NetEngine::ErasePassword(char *password, int length)
{
    if (password != nullptr) {
        // Empty passwords still own one byte for the terminating NUL.
        size_t size = length > 0 ? static_cast<size_t>(length) : 1;
        (void)memset_s(password, size, 0, size);
        free(password);
    }
}

int NetEngine::TlsPrivateKey(const char *name, char **path, char **password, int *length,
    ubs_hcom_tls_keypass_erase *erase)
{
    auto *engine = TlsEngine(name);
    if (engine == nullptr || path == nullptr || password == nullptr || length == nullptr || erase == nullptr) {
        return BIO_ERR;
    }
    *path = nullptr;
    *password = nullptr;
    *length = 0;
    *erase = ErasePassword;
    const auto &options = engine->mOptions;
    if (options.privateKeyPassword.empty()) {
        *password = static_cast<char *>(calloc(1, 1));
        if (*password == nullptr) {
            return BIO_ALLOC_FAIL;
        }
        *path = const_cast<char *>(options.privateKeyPath.c_str());
        return BIO_OK;
    }
    std::vector<char> encrypted(KEYPASS_MAX_LEN, 0);
    std::ifstream stream(options.privateKeyPassword);
    if (!stream.is_open() || !stream.getline(encrypted.data(), KEYPASS_MAX_LEN)) {
        return BIO_ERR;
    }
    std::vector<char> plain(KEYPASS_MAX_LEN, 0);
    size_t plainLen = plain.size();
    int ret = engine->mDecryptHandler(encrypted.data(), strlen(encrypted.data()), plain.data(), &plainLen);
    if (ret != BIO_OK || plainLen > plain.size()) {
        (void)memset_s(plain.data(), plain.size(), 0, plain.size());
        return BIO_ERR;
    }
    size_t allocation = plainLen == 0 ? 1 : plainLen;
    *password = static_cast<char *>(calloc(allocation, 1));
    if (*password != nullptr && plainLen != 0) {
        ret = memcpy_s(*password, allocation, plain.data(), plainLen);
    }
    (void)memset_s(plain.data(), plain.size(), 0, plain.size());
    if (*password == nullptr || ret != BIO_OK) {
        ErasePassword(*password, static_cast<int>(allocation));
        *password = nullptr;
        return BIO_ERR;
    }
    *path = const_cast<char *>(options.privateKeyPath.c_str());
    *length = static_cast<int>(plainLen);
    return BIO_OK;
}

BResult NetEngine::ConfigureTls(ubs_hcom_service service, const NetOptions &options)
{
    if (options.enableTls && ValidateTlsCert(options) != BIO_OK) {
        return BIO_ERR;
    }
    HcomApi::Instance().ubs_hcom_service_set_tls_opt(service, options.enableTls,
        C_SERVICE_TLS_1_3, C_SERVICE_AES_GCM_128, TlsCertificate, TlsPrivateKey, TlsCa, nullptr, nullptr);
    return BIO_OK;
}

BResult NetEngine::AssignIpcServiceOptions(const NetOptions &opt, bool isOobSvr)
{
    HcomApi::Instance().ubs_hcom_service_add_workergroup(mIpcService, 0, WKR_GRP_INDEX_DATA,
        opt.handlerCount, UNBOUND_CPU_RANGE, 0);

    constexpr uint16_t IPC_HEARTBEAT_PROBES = 7;
    HcomApi::Instance().ubs_hcom_service_set_heartbeat_opt(mIpcService, NO_5, IPC_HEARTBEAT_PROBES, NO_1);
    HcomApi::Instance().ubs_hcom_service_set_max_send_recv_data_cnt(mIpcService, NO_1024);
    HcomApi::Instance().ubs_hcom_service_set_sq_size(mIpcService, NO_128);
    HcomApi::Instance().ubs_hcom_service_set_cq_depth(mIpcService, NO_8192);
    HcomApi::Instance().ubs_hcom_service_set_polling_batchsize(mIpcService, NO_16);
    HcomApi::Instance().ubs_hcom_service_set_prepost_size(mIpcService, NO_64);
    HcomApi::Instance().ubs_hcom_service_set_polling_timeoutus(mIpcService, NO_1000);
    HcomApi::Instance().ubs_hcom_service_set_max_connection_cnt(mIpcService, NO_4096 * NO_1024);
    if (isOobSvr) {
        const std::string listenerUrl = "uds://" + UDS_NAME;
        auto ret = HcomApi::Instance().ubs_hcom_service_bind(mIpcService, listenerUrl.c_str(), NewIpcChannel);
        if (UNLIKELY(ret != BIO_OK)) {
            NET_LOG_ERROR("Net tls callback has created.");
            return ret;
        }
    }

    if (ConfigureTls(mIpcService, mOptions) != BIO_OK) {
        return BIO_ERR;
    }
    auto &api = HcomApi::Instance();
    auto owner = reinterpret_cast<uint64_t>(this);
    api.ubs_hcom_service_register_broken_handler(mIpcService, BrokenChannel, C_CHANNEL_BROKEN_ALL, owner);
    api.ubs_hcom_service_register_handler(mIpcService, C_SERVICE_REQUEST_RECEIVED, (reqExecutorNum > NO_32 ? ReceiveRequest : ReceiveIpcRequest), owner);
    api.ubs_hcom_service_register_handler(mIpcService, C_SERVICE_REQUEST_POSTED, PostedRequest, owner);
    api.ubs_hcom_service_register_handler(mIpcService, C_SERVICE_READWRITE_DONE, CompletedOneSide, owner);
    return BIO_OK;
}

BResult NetEngine::StartIpcService(const NetOptions &opt)
{
    int result = BIO_ERR;

    if (mIpcService != 0) {
        NET_LOG_INFO("Net ipc service has already created.");
        return BIO_OK;
    }
    bool isOobSvr = opt.role != Role::NET_CLIENT;
    if (!isOobSvr) {
        mOptions = opt;
    }

    ubs_hcom_service_options options{};
    options.maxSendRecvDataSize = (NO_64 * NO_1024);
    options.workerGroupId = 0;
    options.workerGroupThreadCount = opt.handlerCount;
    options.workerThreadPriority = 0;
    options.workerGroupMode = opt.isBusyLoop ? C_SERVICE_BUSY_POLLING :
                              C_SERVICE_EVENT_POLLING;
    result = HcomApi::Instance().ubs_hcom_service_create(static_cast<ubs_hcom_service_type>(opt.protocol),
        IPC_SERVICE_NAME.c_str(), options, &mIpcService);
    if (result != BIO_OK || mIpcService == 0) {
        NET_LOG_ERROR("Failed to create ipc service instance, protocol:" << opt.protocol << ".");
        return BIO_ERR;
    }

    gIpcEngine = this;
    if (opt.enableTls) {
        result = PrepareTlsDecrypter(opt);
        if (result != BIO_OK) {
            NET_LOG_ERROR("Failed to prepare tls decrypter, result:" << result << ".");
            return result;
        }
    }

    result = AssignIpcServiceOptions(opt, isOobSvr);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to assign ipc service options, result:" << HcomApi::Instance().ubs_hcom_err_str(result) << ".");
        return result;
    }

    result = HcomApi::Instance().ubs_hcom_service_start(mIpcService);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start ipc service, result:" << HcomApi::Instance().ubs_hcom_err_str(result) << ".");
        return BIO_ERR;
    }

    NET_LOG_INFO("Bio server Start ipc service success, protocol:" << opt.protocol <<
        ",net segment size:" << options.maxSendRecvDataSize);
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

    HcomApi::Instance().ubs_hcom_service_add_workergroup(mRpcService, 0, WKR_GRP_INDEX_DATA,
        opt.handlerCount, UNBOUND_CPU_RANGE, 0);

    HcomApi::Instance().ubs_hcom_service_set_heartbeat_opt(mRpcService, NO_1, NO_1, NO_1);
    HcomApi::Instance().ubs_hcom_service_set_max_send_recv_data_cnt(mRpcService, NO_1024);
    HcomApi::Instance().ubs_hcom_service_set_sq_size(mRpcService, NO_4096);
    HcomApi::Instance().ubs_hcom_service_set_rq_size(mRpcService, NO_2048);
    HcomApi::Instance().ubs_hcom_service_set_cq_depth(mRpcService, NO_2048);
    HcomApi::Instance().ubs_hcom_service_set_polling_batchsize(mRpcService, NO_1024);
    HcomApi::Instance().ubs_hcom_service_set_prepost_size(mRpcService, NO_1024);
    HcomApi::Instance().ubs_hcom_service_set_tcp_usr_timeout(mRpcService, NO_3);
    HcomApi::Instance().ubs_hcom_service_set_ipmask(mRpcService, ipMask.c_str());

    if (isOobSvr) {
        const std::string listenerUrl = "tcp://" + goodIps.at(0) + ":" + std::to_string(port);
        auto ret = HcomApi::Instance().ubs_hcom_service_bind(mRpcService, listenerUrl.c_str(), NewRpcChannel);
        if (UNLIKELY(ret != BIO_OK)) {
            NET_LOG_ERROR("Net tls callback has created.");
            return ret;
        }
    }

    if (ConfigureTls(mRpcService, mOptions) != BIO_OK) {
        return BIO_ERR;
    }
    auto &api = HcomApi::Instance();
    auto owner = reinterpret_cast<uint64_t>(this);
    api.ubs_hcom_service_register_broken_handler(mRpcService, BrokenChannel, C_CHANNEL_BROKEN_ALL, owner);
    api.ubs_hcom_service_register_handler(mRpcService, C_SERVICE_REQUEST_RECEIVED, ReceiveRequest, owner);
    api.ubs_hcom_service_register_handler(mRpcService, C_SERVICE_REQUEST_POSTED, PostedRequest, owner);
    api.ubs_hcom_service_register_handler(mRpcService, C_SERVICE_READWRITE_DONE, CompletedOneSide, owner);
    return BIO_OK;
}

BResult NetEngine::StartRpcService(const NetOptions &opt)
{
    int result = BIO_ERR;

    if (mRpcService != 0) {
        NET_LOG_INFO("Net rpc service has already created.");
        return BIO_OK;
    }
    mOptions = opt;
    bool isOobSvr = opt.role != Role::NET_CLIENT;
    ubs_hcom_service_options options{};
    options.maxSendRecvDataSize = opt.netSegmentSize * NO_1024;
    options.workerGroupId = 0;
    options.workerGroupThreadCount = opt.handlerCount;
    options.workerThreadPriority = 0;
    options.workerGroupMode = opt.isBusyLoop ? C_SERVICE_BUSY_POLLING :
                              C_SERVICE_EVENT_POLLING;
    result = HcomApi::Instance().ubs_hcom_service_create(static_cast<ubs_hcom_service_type>(opt.protocol),
        RPC_SERVICE_NAME.c_str(), options, &mRpcService);
    if (result != BIO_OK || mRpcService == 0) {
        NET_LOG_ERROR("Failed to create rpc service instance, protocol:" << opt.protocol << ".");
        return BIO_ERR;
    }

    gRpcEngine = this;
    if (opt.enableTls) {
        result = PrepareTlsDecrypter(opt);
        if (result != BIO_OK) {
            NET_LOG_ERROR("Failed to prepare tls decrypter, result:" << result << ".");
            return result;
        }
    }

    result = AssignRpcServiceOptions(opt, isOobSvr);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to assign rpc service options, result:" << HcomApi::Instance().ubs_hcom_err_str(result) << ".");
        return BIO_ERR;
    }

    result = HcomApi::Instance().ubs_hcom_service_start(mRpcService);
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to start rpc service, result:" << HcomApi::Instance().ubs_hcom_err_str(result) << ".");
        return BIO_ERR;
    }

    result = InitMemoryAllocator();
    if (result != BIO_OK) {
        NET_LOG_ERROR("Failed to init mr allocator, result:" << HcomApi::Instance().ubs_hcom_err_str(result) << ".");
        return BIO_ERR;
    }

    NET_LOG_INFO("Bio server start rpc service success, protocol:" << opt.protocol <<
        ", net segment Size:" << options.maxSendRecvDataSize);
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

void NetEngine::FillConnectOption(ConnectInfo &info, bool isCtrl, std::string &prefix, ubs_hcom_service_connect_options &op)
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
        op.mode = C_CLIENT_SELF_POLL_BUSY;
    }
    NetConnPayload payload(info.srcId);
    auto text = payload.ToPayloadStr(prefix);
    (void)memcpy_s(op.payLoad, sizeof(op.payLoad), text.c_str(), text.size() + 1);
}

BResult NetEngine::ConnectToPeer(ConnectMode mode, ConnectInfo &info, bool isCtrlPanel, ChannelPtr &ch)
{
    ubs_hcom_service netService = (mode == ConnectMode::CONNECT_IPC) ? mIpcService : mRpcService;
    if (netService == 0) {
        NET_LOG_ERROR("Net service not ready.");
        return BIO_ERR;
    }

    ubs_hcom_service_connect_options options{};
    std::string prefix;
    FillConnectOption(info, isCtrlPanel, prefix, options);
    int32_t result = 0;
    for (uint16_t i = 0; i < info.retryTimes; ++i) {
        if (mode == ConnectMode::CONNECT_IPC) {
            const std::string serverUrl = "uds://" + UDS_NAME;
#ifndef DEBUG_UT
            ubs_hcom_channel handle = 0;
            result = HcomApi::Instance().ubs_hcom_service_connect(netService, serverUrl.c_str(), &handle, options);
            if (result == BIO_OK) {
                ch = MakeNetChannel(handle, true);
            }
#else
            result = NetStub::Connect(serverUrl, ch, options);
#endif
        } else {
            const std::string serverUrl = "tcp://" + info.ip + ":" + std::to_string(info.port);
#ifndef DEBUG_UT
            ubs_hcom_channel handle = 0;
            result = HcomApi::Instance().ubs_hcom_service_connect(netService, serverUrl.c_str(), &handle, options);
            if (result == BIO_OK) {
                ch = MakeNetChannel(handle, true);
            }
#else
            result = NetStub::Connect(serverUrl, ch, options);
#endif
        }
        if (result == 0) {
            NET_LOG_INFO("Connect to peer success, ip " << info.ip << ", port " << info.port << ", dstNid " <<
                info.peerId.nid << ", payload " << options.payLoad << ".");
            break;
        }
    }
    if (result != 0) {
        NET_LOG_ERROR("Connect to peer failed, ret:" << HcomApi::Instance().ubs_hcom_err_str(result) << ", ip " << info.ip << ", port " <<
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
    if (config.decrypterLibPath.empty()) {
        RegisterDecryptHandler(static_cast<DecryptFunc>(TlsUtil::DefaultDecrypter));
        LOG_INFO("decrypter lib path is empty. use default func.");
        return BIO_OK;
    }

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
