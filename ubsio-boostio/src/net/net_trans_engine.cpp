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
#include "net_log.h"
#include "net_trans_engine.h"

namespace ock {
namespace bio {

constexpr uint32_t TRANS_EXCUTE_POOL_SIZE = 4;
constexpr uint32_t TRANS_EXCUTE_POOL_QUEUE_SIZE = 1024;
constexpr uint32_t RPC_PORT_BUF_LEN = 16;

void* DlMfApi::mfHandle;
std::mutex DlMfApi::gMutex;
bool DlMfApi::gLoaded;
const char *DlMfApi::gMfLibName = "libmf_smem.so";

mfSmemTransConfigInitFunc DlMfApi::mfSmemTransConfigInit = nullptr;
mfSmemTransInitFunc DlMfApi::mfSmemTransInit = nullptr;
mfSmemTransUnInitFunc DlMfApi::mfSmemTransUnInit = nullptr;
mfSmemTransCreateFunc DlMfApi::mfSmemTransCreate = nullptr;
mfSmemTransDestroyFunc DlMfApi::mfSmemTransDestroy = nullptr;
mfSmemTransMallocFunc DlMfApi::mfSmemTransMalloc = nullptr;
mfSmemTransFreeFunc DlMfApi::mfSmemTransFree = nullptr;
mfSmemTransRegisterMemFunc DlMfApi::mfSmemTransRegisterMem = nullptr;
mfSmemTransBatchRegisterMemFunc DlMfApi::mfSmemTransBatchRegisterMem = nullptr;
mfSmemTransDeRegisterMemFunc DlMfApi::mfSmemTransDeRegisterMem = nullptr;
mfSmemTransWriteFunc DlMfApi::mfSmemTransWrite = nullptr;
mfSmemTransBatchWriteFunc DlMfApi::mfSmemTransBatchWrite = nullptr;
mfSmemTransReadFunc DlMfApi::mfSmemTransRead = nullptr;
mfSmemTransBatchReadFunc DlMfApi::mfSmemTransBatchRead = nullptr;
mfSemTransGetRpcPortFunc DlMfApi::mfSemTransGetRpcPort = nullptr;

int32_t DlMfApi::LoadLibrary(const std::string &libDirPath)
{
    std::lock_guard<std::mutex> guard(gMutex);
    if (gLoaded) {
        return BIO_OK;
    }

    std::string libPath = std::string(libDirPath) + gMfLibName;
    /* dlopen library */
    mfHandle = dlopen(libPath.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (mfHandle == nullptr) {
        NET_LOG_ERROR("Failed to open library [" << libPath << "], error: " << dlerror());
        return BIO_ERR;
    }

    /* load sym */
    DL_LOAD_SYM(mfSmemTransConfigInit, mfSmemTransConfigInitFunc, mfHandle, "smem_trans_config_init");
    DL_LOAD_SYM(mfSmemTransInit, mfSmemTransInitFunc, mfHandle, "smem_trans_init");
    DL_LOAD_SYM(mfSmemTransUnInit, mfSmemTransUnInitFunc, mfHandle, "smem_trans_uninit");
    DL_LOAD_SYM(mfSmemTransCreate, mfSmemTransCreateFunc, mfHandle, "smem_trans_create");
    DL_LOAD_SYM(mfSmemTransDestroy, mfSmemTransDestroyFunc, mfHandle, "smem_trans_destroy");
    DL_LOAD_SYM(mfSmemTransMalloc, mfSmemTransMallocFunc, mfHandle, "smem_trans_malloc");
    DL_LOAD_SYM(mfSmemTransFree, mfSmemTransFreeFunc, mfHandle, "smem_trans_free");
    DL_LOAD_SYM(mfSmemTransRegisterMem, mfSmemTransRegisterMemFunc, mfHandle, "smem_trans_register_mem");
    DL_LOAD_SYM(mfSmemTransBatchRegisterMem, mfSmemTransBatchRegisterMemFunc, mfHandle, "smem_trans_batch_register_mem");
    DL_LOAD_SYM(mfSmemTransDeRegisterMem, mfSmemTransDeRegisterMemFunc, mfHandle, "smem_trans_deregister_mem");
    DL_LOAD_SYM(mfSmemTransWrite, mfSmemTransWriteFunc, mfHandle, "smem_trans_write");
    DL_LOAD_SYM(mfSmemTransBatchWrite, mfSmemTransBatchWriteFunc, mfHandle, "smem_trans_batch_write");
    DL_LOAD_SYM(mfSmemTransRead, mfSmemTransReadFunc, mfHandle, "smem_trans_read");
    DL_LOAD_SYM(mfSmemTransBatchRead, mfSmemTransBatchReadFunc, mfHandle, "smem_trans_batch_read");
    DL_LOAD_SYM(mfSemTransGetRpcPort, mfSemTransGetRpcPortFunc, mfHandle, "smem_trans_get_rpc_port");
    
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
    mfSemTransGetRpcPort = nullptr;

    if (mfHandle != nullptr) {
        dlclose(mfHandle);
        mfHandle = nullptr;
    }
    gLoaded = false;
}

BResult MfTransEngine::Initialize(const NetOptions &opt)
{
    BResult ret = PreInit(opt);
    if (ret != BIO_OK) {
        return ret;
    }
    smem_trans_config_t config;
    ret = DlMfApi::MfSmemTransConfigInit(&config);
    if (ret != BIO_OK) {
        NET_LOG_ERROR( "Failed to init mf trans config, ret: " << ret);
        return ret;
    }
    // store server独立部署，融合部署需要选取一个节点将 startConfigServer 设计为true
    config.role = opt.isSender ? SMEM_TRANS_SENDER : SMEM_TRANS_RECEIVER;
    config.deviceId = opt.transDeviceId;
    if (opt.deviceTransType == "device_sdma") {
        config.dataOpType = SMEMB_DATA_OP_SDMA;
    } else if (opt.deviceTransType == "device_rdma") {
        config.dataOpType = SMEMB_DATA_OP_DEVICE_RDMA;
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
    // mExecutorPool可以取消
    // mExecutorPool = MakeRef<NetExecutorPool>("NetTransExecutor");
    // if (mExecutorPool == nullptr) {
    //     NET_LOG_ERROR("Failed to create NetExecutorPool");
    //     return BIO_ERR;
    // }
    // ret = mExecutorPool->Start(TRANS_EXCUTE_POOL_SIZE, TRANS_EXCUTE_POOL_QUEUE_SIZE);
    // if (ret != BIO_OK) {
    //     NET_LOG_ERROR("Failed to start NetExecutorPool, ret: " << ret);
    //     return ret;
    // }
    const std::chrono::seconds WAIT_TIME(10);
    std::this_thread::sleep_for(WAIT_TIME); // 等待初始化完成
    return BIO_OK;
}

BResult MfTransEngine::Destroy()
{
    if (mTransHandler != nullptr) {
        DlMfApi::MfSmemTransDestroy(mTransHandler, 0);
        mTransHandler = nullptr;
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

    if (param.remoteAddrs.size() != 1 || param.localAddrs.size() != 1 || param.lengths.size() != 1) {
        NET_LOG_ERROR("Read param size is not 1, plesase use BatchRead");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransRead(mTransHandler, param.remoteAddrs[0], param.localAddrs[0],
                                           param.lengths[0], SMEMB_COPY_GH2H, 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to read from mf trans, ret: " << ret);
        return ret;
    }

    return BIO_OK;
}

BResult MfTransEngine::BatchRead(std::vector<TransParam>& params)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }

    if (params.localesAddrs.size() != params.remotesAddrs.size() ||
        params.localesAddrs.size() != params.lengths.size()) {
        NET_LOG_ERROR("localesAddrs size is not equal to remotesAddrs size or lengths size");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransBatchRead(mTransHandler, params.remotesAddrs.data(),
                                                params.localesAddrs.data(), params.lengths.data(),
                                                params.localesAddrs.size(), SMEMB_COPY_GH2H, 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to batch read from mf trans, ret: " << ret);
        return ret;
    }
    return BIO_OK;
}

BResult MfTransEngine::Write(TransParam& param)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }
    if (param.remoteAddrs.size() != 1 || param.localAddrs.size() != 1 || param.lengths.size() != 1) {
        NET_LOG_ERROR("Write param size is not 1, plesase use BatchWrite");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransWrite(mTransHandler, param.localAddrs[0],
                                            param.remoteAddrs[0], param.lengths[0], SMEMB_COPY_H2G, 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to write to mf trans, ret: " << ret);
        return ret;
    }
    return BIO_OK;
}

BResult MfTransEngine::BatchWrite(std::vector<TransParam>& params)
{
    if (mTransHandler == nullptr) {
        NET_LOG_ERROR("mTransHandler is nullptr, please init trans");
        return BIO_ERR;
    }
    if (params.localesAddrs.size() != params.remotesAddrs.size() ||
        params.localesAddrs.size() != params.lengths.size()) {
        NET_LOG_ERROR("localesAddrs size is not equal to remotesAddrs size or lengths size");
        return BIO_ERR;
    }

    BResult ret = DlMfApi::MfSmemTransBatchWrite(mTransHandler, params.localesAddrs.data(),
                                                 params.remotesAddrs.data(), params.lengths.data(),
                                                 params.localesAddrs.size(), SMEMB_COPY_H2G, 0);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to batch write to mf trans, ret: " << ret);
        return ret;
    }
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

    size_t slashPos = opt.ipMask.find('/');
    std::string ip;
    if (slashPos != std::string::npos) {
        ip = opt.ipMask.substr(0, slashPos);
    } else {
        NET_LOG_ERROR("Invalid ipMask format: " << opt.ipMask << ", should be ip/mask, e.g. 192.168.1.100/24")
        return BIO_ERR;
    }
    char buffer[RPC_PORT_BUF_LEN];
    BResult ret = DlMfApi::MfSemTransGetRpcPort(buffer, RPC_PORT_BUF_LEN);
    if (ret != BIO_OK) {
        NET_LOG_ERROR("Failed to get rpc port from mf trans, ret: " << ret);
        return ret;
    }
    mLocalUniqueId = ip + ":" + std::string(buffer);
    mStoreUrl = opt.transStoreUrl;
    NET_LOG_INFO("PreInit success, mLocalUniqueId: " << mLocalUniqueId << ", mStoreUrl: " << mStoreUrl <<);
    return BIO_OK;
}

}
}