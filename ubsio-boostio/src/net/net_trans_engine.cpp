/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */


#include <string>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <random>
#include <stdexcept>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include "net_log.h"
#include "net_trans_engine.h"

namespace ock {
namespace bio {

constexpr uint32_t TRANS_EXCUTE_POOL_SIZE = 4;
constexpr uint32_t TRANS_EXCUTE_POOL_QUEUE_SIZE = 1024;
constexpr uint16_t INVALID_RPC_PORT = 0;
constexpr uint32_t MAX_TRANS_SEGMENT_SIZE = 1024 * 1024 * 1024; // 1G
constexpr uint64_t MAX_TRANS_MEM_SIZE = 40ULL * 1024 * 1024 * 1024; // 40G

void* DlMfApi::mfHandle;
std::mutex DlMfApi::gMutex;
bool DlMfApi::gLoaded;
const char *DlMfApi::gMfLibName = "libmf_smem.so";

MfSmemTransConfigInitFunc DlMfApi::mfSmemTransConfigInit = nullptr;
MfSmemTransInitFunc DlMfApi::mfSmemTransInit = nullptr;
MfSmemTransUnInitFunc DlMfApi::mfSmemTransUnInit = nullptr;
MfSmemTransCreateFunc DlMfApi::mfSmemTransCreate = nullptr;
MfSmemTransDestroyFunc DlMfApi::mfSmemTransDestroy = nullptr;
MfSmemTransMallocFunc DlMfApi::mfSmemTransMalloc = nullptr;
MfSmemTransFreeFunc DlMfApi::mfSmemTransFree = nullptr;
MfSmemTransRegisterMemFunc DlMfApi::mfSmemTransRegisterMem = nullptr;
MfSmemTransBatchRegisterMemFunc DlMfApi::mfSmemTransBatchRegisterMem = nullptr;
MfSmemTransDeRegisterMemFunc DlMfApi::mfSmemTransDeRegisterMem = nullptr;
MfSmemTransWriteFunc DlMfApi::mfSmemTransWrite = nullptr;
MfSmemTransBatchWriteFunc DlMfApi::mfSmemTransBatchWrite = nullptr;
MfSmemTransReadFunc DlMfApi::mfSmemTransRead = nullptr;
MfSmemTransBatchReadFunc DlMfApi::mfSmemTransBatchRead = nullptr;
MfSemSetExternLoggerFunc DlMfApi::mfSemSetExternLogger = nullptr;
MfSemSetLogLevelFunc DlMfApi::mfSemSetLogLevel = nullptr;

int32_t DlMfApi::LoadLibrary(const std::string &libDirPath)
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (gLoaded) {
        return BIO_OK;
    }

    std::string libPath = std::string(libDirPath)+ "/" + gMfLibName;
    /* dlopen library */
    mfHandle = dlopen(libPath.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (mfHandle == nullptr) {
        NET_LOG_ERROR("Failed to open library [" << libPath << "], error: " << dlerror());
        return BIO_ERR;
    }

    /* load sym */
    DL_LOAD_SYM(mfSmemTransConfigInit, MfSmemTransConfigInitFunc, mfHandle, "smem_trans_config_init");
    DL_LOAD_SYM(mfSmemTransInit, MfSmemTransInitFunc, mfHandle, "smem_trans_init");
    DL_LOAD_SYM(mfSmemTransUnInit, MfSmemTransUnInitFunc, mfHandle, "smem_trans_uninit");
    DL_LOAD_SYM(mfSmemTransCreate, MfSmemTransCreateFunc, mfHandle, "smem_trans_create");
    DL_LOAD_SYM(mfSmemTransDestroy, MfSmemTransDestroyFunc, mfHandle, "smem_trans_destroy");
    DL_LOAD_SYM(mfSmemTransMalloc, MfSmemTransMallocFunc, mfHandle, "smem_trans_malloc");
    DL_LOAD_SYM(mfSmemTransFree, MfSmemTransFreeFunc, mfHandle, "smem_trans_free");
    DL_LOAD_SYM(mfSmemTransRegisterMem, MfSmemTransRegisterMemFunc, mfHandle, "smem_trans_register_mem");
    DL_LOAD_SYM(mfSmemTransBatchRegisterMem, MfSmemTransBatchRegisterMemFunc,
                mfHandle, "smem_trans_batch_register_mem");
    DL_LOAD_SYM(mfSmemTransDeRegisterMem, MfSmemTransDeRegisterMemFunc, mfHandle, "smem_trans_deregister_mem");
    DL_LOAD_SYM(mfSmemTransWrite, MfSmemTransWriteFunc, mfHandle, "smem_trans_write");
    DL_LOAD_SYM(mfSmemTransBatchWrite, MfSmemTransBatchWriteFunc, mfHandle, "smem_trans_batch_write");
    DL_LOAD_SYM(mfSmemTransRead, MfSmemTransReadFunc, mfHandle, "smem_trans_read");
    DL_LOAD_SYM(mfSmemTransBatchRead, MfSmemTransBatchReadFunc, mfHandle, "smem_trans_batch_read");
    DL_LOAD_SYM(mfSemSetExternLogger, MfSemSetExternLoggerFunc, mfHandle, "smem_set_extern_logger");
    DL_LOAD_SYM(mfSemSetLogLevel, MfSemSetLogLevelFunc, mfHandle, "smem_set_log_level");
    
    gLoaded = true;
    return BIO_OK;
}

void DlMfApi::CleanupLibrary()
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (!gLoaded) {
        return;
    }
    mfSmemTransConfigInit = nullptr;
    mfSmemTransInit = nullptr;
    mfSmemTransUnInit = nullptr;
    mfSmemTransCreate = nullptr;
    mfSmemTransDestroy = nullptr;
    mfSmemTransMalloc = nullptr;
    mfSmemTransFree = nullptr;
    mfSmemTransRegisterMem = nullptr;
    mfSmemTransBatchRegisterMem = nullptr;
    mfSmemTransDeRegisterMem = nullptr;
    mfSmemTransWrite = nullptr;
    mfSmemTransBatchWrite = nullptr;
    mfSmemTransRead = nullptr;
    mfSmemTransBatchRead = nullptr;
    mfSemSetExternLogger = nullptr;
    mfSemSetLogLevel = nullptr;

    if (mfHandle != nullptr) {
        dlclose(mfHandle);
        mfHandle = nullptr;
    }
    gLoaded = false;
}

BResult MfTransEngine::Initialize(const NetOptions &opt)
{
    NET_LOG_INFO("Start iniitalize MfTransEngine, transDeviceId: " << opt.transDeviceId << ", deviceTransType: " <<
        opt.deviceTransType << ", ipMask: " << opt.ipMask << ", transStoreUrl: " << opt.transStoreUrl <<
        ", transMemSize: " << opt.transMemSize << ", transSegmentSize: " << opt.transSegmentSize <<
        ", isSender: " << opt.isSender);
    if (opt.transDeviceId < 0) {
        NET_LOG_WARN("transDeviceId is: " << opt.transDeviceId << ", will not use device transfer");
        return BIO_OK;
    }
    BResult ret = PreInit(opt);
    if (ret != BIO_OK) {
        return ret;
    }
    smem_trans_config_t config;
    ret = DlMfApi::MfSmemTransConfigInit(&config);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to init mf trans config, ret: " << ret);
        return ret;
    }
    // store server独立部署，融合部署需要选取一个节点将 startConfigServer 设计为true
    config.role = opt.isSender ? SMEM_TRANS_SENDER : SMEM_TRANS_RECEIVER;
    config.deviceId = opt.transDeviceId;
    if (opt.deviceTransType == "device_sdma") {
        config.dataOpType = SMEMB_DATA_OP_SDMA;
    } else {
        NET_LOG_ERROR("Unsupported deviceTransType: " << opt.deviceTransType);
        return BIO_ERR;
    }

    ret = DlMfApi::MfSmemTransInit(&config);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to init mf trans, ret: " << ret);
        return ret;
    }

    mTransHandler = DlMfApi::MfSmemTransCreate(mStoreUrl.c_str(), mLocalUniqueId.c_str(), &config);
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("Failed to create mf trans");
        return BIO_ERR;
    }
    
    const std::chrono::seconds waitTime(10);
    std::this_thread::sleep_for(waitTime); // 等待初始化完成
    ret = InitMsgBlockPool(opt);
    if (ret != BIO_OK) {
        return ret;
    }
    NET_LOG_INFO("MfTransEngine initialize success");
    return BIO_OK;
}

void MfTransEngine::Destroy()
{
    if (mTransMemBase != nullptr) {
        (void)FreeMem(mTransMemBase);
    }
    if (mTransHandler != nullptr) {
        DlMfApi::MfSmemTransDestroy(mTransHandler, 0);
        mTransHandler = nullptr;
    }
    if (socFd != -1) {
        close(socFd);
        socFd = -1;
    }
}

BResult MfTransEngine::MallocMem(size_t size, void*& address)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }

    address = DlMfApi::MfSmemTransMalloc(mTransHandler, size);
    if (address == nullptr) {
        NET_LOG_ERROR("Failed to malloc mem from mf trans, size: " << size);
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult MfTransEngine::FreeMem(void* address)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }
    BResult ret = DlMfApi::MfSmemTransFree(mTransHandler, address);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to free mem to mf trans, ret: " << ret);
        return ret;
    }
    return BIO_OK;
}

BResult MfTransEngine::RegisterMem(void* address, size_t size)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransRegisterMem(mTransHandler, address, size, 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to register mem to mf trans, ret: " << ret);
        return ret;
    }
    return BIO_OK;
}

BResult MfTransEngine::BatchRegisterMem(std::vector<void*>& addresses, std::vector<size_t>& sizes)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }

    if (addresses.size() != sizes.size()) {
        NET_LOG_ERROR("addresses size is not equal to sizes size");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransBatchRegisterMem(mTransHandler, addresses.data(),
                                                       sizes.data(), addresses.size(), 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to batch register mem to mf trans, ret: " << ret);
        return ret;
    }
    return BIO_OK;
}

BResult MfTransEngine::Read(TransParam& param)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }

    if (param.remoteAddrs.size() != 1 || param.localAddrs.size() != 1 || param.dataSizes.size() != 1) {
        NET_LOG_ERROR("Read param size is not 1, plesase use BatchRead");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransRead(mTransHandler, param.localAddrs[0], param.remoteUniqueId.c_str(),
                                           param.remoteAddrs[0], param.dataSizes[0], SMEMB_COPY_GH2H, 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to read from mf trans, ret: " << ret);
        return ret;
    }
    NET_LOG_INFO("Read success, local addr: " << param.localAddrs[0] << ", remote addr: " <<
                 param.remoteAddrs[0] << ", size: " << param.dataSizes[0]);
    return BIO_OK;
}

BResult MfTransEngine::BatchRead(TransParam& param)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }

    if (param.localAddrs.size() != param.remoteAddrs.size() ||
        param.localAddrs.size() != param.dataSizes.size()) {
        NET_LOG_ERROR("localAddrs size is not equal to remoteAddrs size or dataSizes size");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransBatchRead(mTransHandler, param.localAddrs.data(), param.remoteUniqueId.c_str(),
                                                const_cast<const void**>(param.remoteAddrs.data()),
                                                param.dataSizes.data(), param.localAddrs.size(),
                                                SMEMB_COPY_GH2H, 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to batch read from mf trans, ret: " << ret);
        return ret;
    }
    NET_LOG_INFO("BatchRead success, size: " << param.localAddrs.size() << ", localAddrs: " << param.localAddrs[0]);
    return BIO_OK;
}

BResult MfTransEngine::Write(TransParam& param)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }
    if (param.remoteAddrs.size() != 1 || param.localAddrs.size() != 1 || param.dataSizes.size() != 1) {
        NET_LOG_ERROR("Write param size is not 1, plesase use BatchWrite");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransWrite(mTransHandler, param.localAddrs[0], param.remoteUniqueId.c_str(),
                                            param.remoteAddrs[0], param.dataSizes[0], SMEMB_COPY_H2G, 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to write to mf trans, ret: " << ret);
        return ret;
    }
    NET_LOG_INFO("Write success, local addr: " << param.localAddrs[0] << ", remote addr: " <<
                 param.remoteAddrs[0] << ", size: " << param.dataSizes[0]);
    return BIO_OK;
}

BResult MfTransEngine::BatchWrite(TransParam& param)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }
    if (param.localAddrs.size() != param.remoteAddrs.size() ||
        param.localAddrs.size() != param.dataSizes.size()) {
        NET_LOG_ERROR("localAddrs size is not equal to remoteAddrs size or dataSizes size");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransBatchWrite(mTransHandler, const_cast<const void**>(param.localAddrs.data()),
                                                 param.remoteUniqueId.c_str(),
                                                 param.remoteAddrs.data(), param.dataSizes.data(),
                                                 param.localAddrs.size(), SMEMB_COPY_H2G, 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to batch write to mf trans, ret: " << ret);
        return ret;
    }
    NET_LOG_INFO("BatchWrite success, size: " << param.localAddrs.size() << ", localAddrs: " << param.localAddrs[0]);
    return BIO_OK;
}

BResult MfTransEngine::PreInit(const NetOptions &opt)
{
    // 加载mf动态库, 通过环境变量获取mf
    const char* mfLibPath = std::getenv("MEMFABRIC_HYBRID_EXTEND_LIB_PATH");
    if (mfLibPath == nullptr) {
        NET_LOG_ERROR("MEMFABRIC_HYBRID_EXTEND_LIB_PATH is empty");
        return BIO_ERR;
    }
    std::string libDirPath = std::string(mfLibPath);
    if (DlMfApi::LoadLibrary(libDirPath) != BIO_OK) {
        NET_LOG_ERROR("Failed to load mf dynamic library");
        return BIO_ERR;
    }
    if (opt.isSender) {
        auto logFunc = [](int level, const char *message) { Logger::gInstance->Log(level, message); };
        int32_t logLevel = Logger::gInstance->GetLogLevel();
        NET_LOG_WARN("Set mf log level to: " << logLevel);
        auto ret = DlMfApi::MfSmemSetLogLevel(logLevel);
        if (ret != BIO_OK) {
            NET_LOG_ERROR("Failed to set mf log level, ret: " << ret);
            return BIO_ERR;
        }
        ret = DlMfApi::MfSmemSetExternLogger(logFunc);
        if (ret != BIO_OK) {
            NET_LOG_ERROR("Failed to set mf extern logger, ret: " << ret);
            return BIO_ERR;
        }
        NET_LOG_WARN("Set mf log success");
    }
    
    size_t slashPos = opt.ipMask.find('/');
    std::string ip;
    if (slashPos != std::string::npos) {
        ip = opt.ipMask.substr(0, slashPos);
    } else {
        NET_LOG_ERROR("Invalid ipMask format: " << opt.ipMask << ", should be ip/mask, e.g. 192.168.1.100/24");
        return BIO_ERR;
    }
    int32_t socketFd = -1;
    uint16_t port = FindAvailableTcpPort(socketFd);
    if (port == INVALID_RPC_PORT) {
        NET_LOG_ERROR("Failed to get rpc port , port: " << port);
        return BIO_ERR;
    }

    mLocalUniqueId = ip + ":" + std::to_string(port);
    mStoreUrl = opt.transStoreUrl;
    NET_LOG_INFO("PreInit success, mLocalUniqueId: " << mLocalUniqueId << ", mStoreUrl: " << mStoreUrl);
    socFd = socketFd;
    return BIO_OK;
}

BResult MfTransEngine::InitMsgBlockPool(const NetOptions &opt)
{
    if (opt.transMemSize > MAX_TRANS_MEM_SIZE || opt.transSegmentSize > MAX_TRANS_SEGMENT_SIZE ||
        opt.transMemSize < opt.transSegmentSize) {
        NET_LOG_ERROR("transMemSize or transSegmentSize is too large, transMemSize: " <<
                      opt.transMemSize << ", transSegmentSize: " << opt.transSegmentSize);
        return BIO_ERR;
    }
    mTransMemSize = opt.transMemSize;
    mTransSegmentSize = opt.transSegmentSize;
    mMsgBlookPool = MakeRef<NetBlockPool>();
    if (mMsgBlookPool == nullptr) {
        NET_LOG_ERROR("Failed to create msg block pool");
        return BIO_ERR;
    }
    auto ret = MallocMem(mTransMemSize, mTransMemBase);
    if (ret != BIO_OK) {
        return ret;
    }
    ret = mMsgBlookPool->Start(reinterpret_cast<uintptr_t>(mTransMemBase), mTransSegmentSize,
                               mTransMemSize / mTransSegmentSize);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to start msg block pool, ret: " << ret);
        (void)FreeMem(mTransMemBase);
        return ret;
    }
    NET_LOG_ERROR("InitMsgBlockPool success");
    return BIO_OK;
}

BResult MfTransEngine::AllocOneBlock(uintptr_t& address)
{
    if (mMsgBlookPool == nullptr) {
        NET_LOG_ERROR("mMsgBlookPool is nullptr, please init trans");
        return BIO_ERR;
    }
    auto ret = mMsgBlookPool->AllocOne(address);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to alloc one block, ret: " << ret);
        return ret;
    }
    return BIO_OK;
}

BResult MfTransEngine::AllocBlocks(uint32_t count, std::vector<uintptr_t> &addresses)
{
    if (mMsgBlookPool == nullptr) {
        NET_LOG_ERROR("mMsgBlookPool is nullptr, please init trans");
        return BIO_ERR;
    }
    auto ret = mMsgBlookPool->AllocMany(count, addresses);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to alloc many blocks, ret: " << ret);
        return ret;
    }
    return BIO_OK;
}

BResult MfTransEngine::FreeOneBlock(uintptr_t address)
{
    if (mMsgBlookPool == nullptr) {
        NET_LOG_ERROR("mMsgBlookPool is nullptr, please init trans");
        return BIO_ERR;
    }
    mMsgBlookPool->ReleaseOne(address);
    return BIO_OK;
}
    
BResult MfTransEngine::FreeBlocks(std::vector<uintptr_t> &addresses)
{
    if (mMsgBlookPool == nullptr) {
        NET_LOG_ERROR("mMsgBlookPool is nullptr, please init trans");
        return BIO_ERR;
    }
    mMsgBlookPool->ReleaseMany(addresses);
    return BIO_OK;
}

BResult MfTransEngine::BindTcpPortV4(int32_t &sockfd, int32_t port)
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd != -1) {
        int32_t on_v4 = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on_v4, sizeof(on_v4)) == 0) {
            sockaddr_in bind_address_v4{};
            bind_address_v4.sin_family = AF_INET;
            bind_address_v4.sin_port = htons(port);
            bind_address_v4.sin_addr.s_addr = INADDR_ANY;

            if (bind(sockfd, reinterpret_cast<sockaddr *>(&bind_address_v4), sizeof(bind_address_v4)) == 0) {
                return 0;
            }
        }
        close(sockfd);
        sockfd = -1;
    }
    return -1;
}

BResult MfTransEngine::BindTcpPortV6(int32_t &sockfd, int32_t port)
{
    sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sockfd != -1) {
        int32_t on_v6 = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on_v6, sizeof(on_v6)) == 0) {
            sockaddr_in6 bind_address_v6{};
            bind_address_v6.sin6_family = AF_INET6;
            bind_address_v6.sin6_port = htons(port);
            bind_address_v6.sin6_addr = in6addr_any;

            if (bind(sockfd, reinterpret_cast<sockaddr *>(&bind_address_v6), sizeof(bind_address_v6)) == 0) {
                return 0;
            }
        }
        close(sockfd);
        sockfd = -1;
    }
    return -1;
}

uint16_t MfTransEngine::FindAvailableTcpPort(int32_t &sockfd)
{
    static std::random_device rd;
    const int32_t minPort = 15000;
    const int32_t maxPort = 25000;
    const int32_t maxAttempts = 1000;
    const int32_t offsetBit = 32;
    uint64_t seed = 1;
    seed |= static_cast<uint64_t>(getpid()) << offsetBit;
    seed |= static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()) & 0xFFFFFFFFULL;
    static std::mt19937_64 gen(seed);
    std::uniform_int_distribution<> dis(minPort, maxPort);

    bool supportsIpv6 = false;
    int32_t sockfdCheck = socket(AF_INET6, SOCK_STREAM, 0);
    if (sockfdCheck != -1) {
        supportsIpv6 = true;
        close(sockfdCheck);
    }

    for (int32_t attempt = 0; attempt < maxAttempts; ++attempt) {
        int32_t port = dis(gen);
        auto ret = BindTcpPortV4(sockfd, port);
        if (ret == 0) {
            return port;
        }
        if (supportsIpv6) {
            ret = BindTcpPortV6(sockfd, port);
            if (ret == 0) {
                return port;
            }
        }
    }
    NET_LOG_ERROR("Not find a available tcp port");
    return 0;
}

}
}