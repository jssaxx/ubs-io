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

#ifndef BIO_NET_TRANS_ENGINE_H
#define BIO_NET_TRANS_ENGINE_H

#include <cstdint>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <vector>
#include "bio_err.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"
#include "net_common.h"
#include "net_executor_pool.h"

namespace ock {
namespace bio {
#define DL_LOAD_SYM(TARGET_FUNC_VAR, TARGET_FUNC_TYPE, FILE_HANDLE, SYMBOL_NAME)           \
    do {                                                                                   \
        TARGET_FUNC_VAR = (TARGET_FUNC_TYPE)dlsym(FILE_HANDLE, SYMBOL_NAME);               \
        if ((TARGET_FUNC_VAR) == nullptr) {                                                \
            NET_LOG_ERROR("Failed to call dlsym to load SYMBOL_NAME, error" << dlerror());     \
            dlclose(FILE_HANDLE);                                                          \
            return BIO_ERR;                                                                \
        }                                                                                  \
    } while (0)

typedef enum {
    SMEMB_DATA_OP_SDMA = 1U << 0,
    SMEMB_DATA_OP_HOST_RDMA = 1U << 1,
    SMEMB_DATA_OP_HOST_TCP = 1U << 2,
    SMEMB_DATA_OP_DEVICE_RDMA = 1U << 3,
    SMEMB_DATA_OP_HOST_URMA = 1U << 4,
    SMEMB_DATA_OP_BUTT
} smem_bm_data_op_type;

/**
* @brief Data copy direction
*/
typedef enum {
    SMEMB_COPY_L2G = 0,  /* copy data from local hbm to global space */
    SMEMB_COPY_G2L = 1,  /* copy data from global space to local hbm */
    SMEMB_COPY_G2H = 2,  /* copy data from global space to local host dram */
    SMEMB_COPY_H2G = 3,  /* copy data from local host dram to global space */
    SMEMB_COPY_L2GH = 4, /* copy data from local hbm to global host space */
    SMEMB_COPY_GH2L = 5, /* copy data from global host space to local hbm */
    SMEMB_COPY_GH2H = 6, /* copy data from global host space to host memory */
    SMEMB_COPY_H2GH = 7, /* copy data from host memory to global host space */
    SMEMB_COPY_G2G = 8,  /* copy data from global space to global space */
    SMEMB_COPY_AUTO = 9, /* data copy direction is automatically selected */
    /* add here */
    SMEMB_COPY_BUTT
} smem_bm_copy_type;

/*
 * @brief Transfer role, i.e. sender/receiver
 */
typedef enum {
    SMEM_TRANS_NONE = 0, /* no role */
    SMEM_TRANS_SENDER,   /* sender */
    SMEM_TRANS_RECEIVER, /* receiver */
    SMEM_TRANS_BOTH,     /* both sender and receiver */
    SMEM_TRANS_BUTT
} smem_trans_role_t;

/**
 * @brief Transfer config
 */
typedef struct {
    smem_trans_role_t role;          /* transfer role */
    uint32_t initTimeout;            /* func timeout, default 120 seconds */
    uint32_t deviceId;               /* npu device id */
    uint32_t flags;                  /* optional flags */
    smem_bm_data_op_type dataOpType; /* data operation type */
    bool startConfigServer;          /* whether to start config store, default false */
} smem_trans_config_t;

typedef void *smem_trans_t;

typedef struct {
    std::string remoteUniqueId;
    std::vector<void*> localAddrs;
    std::vector<void*> remoteAddrs;
    std::vector<size_t> dataSizes;
} TransParam;

using mfSmemTransConfigInitFunc = int32_t (*)(smem_trans_config_t *);
using mfSmemTransInitFunc = int32_t (*)(smem_trans_config_t *);
using mfSmemTransUnInitFunc = void (*)(uint32_t);
using mfSmemTransCreateFunc = smem_trans_t (*)(const char *, const char *, const smem_trans_config_t *);
using mfSmemTransDestroyFunc = void (*)(smem_trans_t, uint32_t);
using mfSmemTransMallocFunc = void* (*)(smem_trans_t, size_t);
using mfSmemTransFreeFunc = int32_t (*)(smem_trans_t, void *);
using mfSmemTransRegisterMemFunc = int32_t (*)(smem_trans_t, void *, size_t, uint32_t);
using mfSmemTransBatchRegisterMemFunc = int32_t (*)(smem_trans_t, void **, size_t *, uint32_t, uint32_t);
using mfSmemTransDeRegisterMemFunc = int32_t (*)(smem_trans_t, void *);
using mfSmemTransWriteFunc = int32_t (*)(smem_trans_t, const void *, const char *, void *, size_t, int32_t, uint32_t);
using mfSmemTransBatchWriteFunc = int32_t (*)(smem_trans_t, const void **, const char *,
                                              void **, size_t *, uint32_t, int32_t, uint32_t);
using mfSmemTransReadFunc = int32_t (*)(smem_trans_t, void *, const char *, const void *, size_t, int32_t, uint32_t);
using mfSmemTransBatchReadFunc = int32_t (*)(smem_trans_t, void **, const char *,
                                             const void **, size_t *, uint32_t, int32_t, uint32_t);
using mfSemTransGetRpcPortFunc = int32_t (*)(const char *, int32_t *);

class DlMfApi {
public:
    static int32_t LoadLibrary(const std::string &libDirPath);
    static void CleanupLibrary();

    static inline BResult MfSmemTransConfigInit(smem_trans_config_t *config)
    {
        if (mfSmemTransConfigInit == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransConfigInit(config);
    }

    static inline BResult MfSmemTransInit(smem_trans_config_t *config)
    {
        if (mfSmemTransInit == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransInit(config);
    }

    static inline BResult MfSmemTransUnInit(uint32_t flags)
    {
        if (mfSmemTransUnInit == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        mfSmemTransUnInit(flags);
        return BIO_OK;

    }

    static inline smem_trans_t MfSmemTransCreate(const char *storeUrl, const char *uniqueId,
                                                 const smem_trans_config_t *config)
    {
        if (mfSmemTransCreate == nullptr) {
            return nullptr;
        }
        return mfSmemTransCreate(storeUrl, uniqueId, config);
    }

    static inline BResult MfSmemTransDestroy(smem_trans_t handle, uint32_t flags)
    {
        if (mfSmemTransDestroy == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        mfSmemTransDestroy(handle, flags);
        return BIO_OK;
    }

    static inline void* MfSmemTransMalloc(smem_trans_t handle, size_t capacity)
    {
        if (mfSmemTransMalloc == nullptr) {
            return nullptr;
        }
        return mfSmemTransMalloc(handle, capacity);
    }

    static inline BResult MfSmemTransFree(smem_trans_t handle, void *address)
    {
        if (mfSmemTransFree == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransFree(handle, address);
    }

    static inline BResult MfSmemTransRegisterMem(smem_trans_t handle, void *address, size_t capacity, uint32_t flags)
    {
        if (mfSmemTransRegisterMem == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransRegisterMem(handle, address, capacity, flags);
    }

    static inline BResult MfSmemTransBatchRegisterMem(smem_trans_t handle, void *addresses[], size_t capacities[],
                                                      uint32_t count, uint32_t flags)
    {
        if (mfSmemTransBatchRegisterMem == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransBatchRegisterMem(handle, addresses, capacities, count, flags);
    }

    static inline BResult MfSmemTransDeRegisterMem(smem_trans_t handle, void *address)
    {
        if (mfSmemTransDeRegisterMem == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransDeRegisterMem(handle, address);
    }

    static inline BResult MfSmemTransWrite(smem_trans_t handle, const void *localAddr, const char *remoteUniqueId,
                                           void *remoteAddr, size_t dataSize, int32_t opcode, uint32_t flags)
    {
        if (mfSmemTransWrite == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransWrite(handle, localAddr, remoteUniqueId, remoteAddr, dataSize, opcode, flags);
    }

    static inline BResult MfSmemTransBatchWrite(smem_trans_t handle, const void *localAddrs[],
                                                const char *remoteUniqueId, void *remoteAddrs[],
                                                size_t dataSizes[], uint32_t batchSize, int32_t opcode, uint32_t flags)
    {
        if (mfSmemTransBatchWrite == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransBatchWrite(handle, localAddrs, remoteUniqueId, remoteAddrs, dataSizes, batchSize, opcode, flags);

    }

    static inline BResult MfSmemTransRead(smem_trans_t handle, void *localAddr, const char *remoteUniqueId,
                                          const void *remoteAddr, size_t dataSize, int32_t opcode, uint32_t flags)
    {
        if (mfSmemTransRead == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransRead(handle, localAddr, remoteUniqueId, remoteAddr, dataSize, opcode, flags);
    }

    static inline BResult MfSmemTransBatchRead(smem_trans_t handle, void *localAddrs[],
                                               const char *remoteUniqueId, const void *remoteAddrs[],
                                               size_t dataSizes[], uint32_t batchSize, int32_t opcode, uint32_t flags)
    {
        if (mfSmemTransBatchRead == nullptr) {
            return BIO_UNDER_API_UNLOAD;
        }
        return mfSmemTransBatchRead(handle, localAddrs, remoteUniqueId, remoteAddrs, dataSizes, batchSize, opcode, flags);
    }

private:
    static void* mfHandle;
    static std::mutex gMutex;
    static bool gLoaded;
    static const char *gMfLibName;

    static mfSmemTransConfigInitFunc mfSmemTransConfigInit;
    static mfSmemTransInitFunc mfSmemTransInit;
    static mfSmemTransUnInitFunc mfSmemTransUnInit;
    static mfSmemTransCreateFunc mfSmemTransCreate;
    static mfSmemTransDestroyFunc mfSmemTransDestroy;
    static mfSmemTransMallocFunc mfSmemTransMalloc;
    static mfSmemTransFreeFunc mfSmemTransFree;
    static mfSmemTransRegisterMemFunc mfSmemTransRegisterMem;
    static mfSmemTransBatchRegisterMemFunc mfSmemTransBatchRegisterMem;
    static mfSmemTransDeRegisterMemFunc mfSmemTransDeRegisterMem;
    static mfSmemTransWriteFunc mfSmemTransWrite;
    static mfSmemTransBatchWriteFunc mfSmemTransBatchWrite;
    static mfSmemTransReadFunc mfSmemTransRead;
    static mfSmemTransBatchReadFunc mfSmemTransBatchRead;
};

class NetTransEngine {
public:
    virtual BResult Initialize(const NetOptions &opt) = 0;

    virtual BResult Destroy() = 0;

    virtual BResult MallocMem(size_t size, void*& address) = 0;

    virtual BResult FreeMem(void* address) = 0;

    virtual BResult RegisterMem(void* address, size_t size) = 0;

    virtual BResult BatchRegisterMem(std::vector<void*>& addresses, std::vector<size_t>& sizes) override;

    virtual BResult Read(TransParam& param) = 0;

    virtual BResult Write(TransParam& param) = 0;

    virtual BResult BatchRead(TransParam& param) = 0;

    virtual BResult BatchWrite(TransParam& param) = 0;

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    DEFINE_REF_COUNT_VARIABLE
};

using NetTransEnginePtr = Ref<NetTransEngine>;


class MfTransEngine : public NetTransEngine {
public:
    MfTransEngine() = default;
    ~MfTransEngine()
    {
        Destroy();
    }
    BResult Initialize(const NetOptions &opt) override;

    BResult Destroy() override;

    BResult MallocMem(size_t size, void*& address) override;

    BResult FreeMem(void* address) override;

    // register dram or hbm addr to trans
    BResult RegisterMem(void* address, size_t size) override;

    // register multiple dram or hbm addr to trans
    BResult BatchRegisterMem(std::vector<void*>& addresses, std::vector<size_t>& sizes) override;

    BResult Read(TransParam& param) override;

    BResult Write(TransParam& param) override;

    BResult BatchRead(TransParam& param) override;

    BResult BatchWrite(TransParam& param) override;

    inline std::string GetLocalUniqueId() const
    {
        return mLocalUniqueId;
    }

private:
    BResult PreInit(const NetOptions &opt);

    BResult BindTcpPortV4(int32_t &sockfd, int32_t port);

    BResult BindTcpPortV6(int32_t &sockfd, int32_t port);

    uint16_t FindAvailableTcpPort(int32_t &sockfd)

private:
    void* mTransHandler = nullptr;
    //NetExecutorPoolPtr mExecutorPool = nullptr;
    std::string mLocalUniqueId = ""; // ip:port_pid
    std::string mStoreUrl = ""; // meta store url, format: tcp://ip:port
};

}
} 


#endif //BIO_NET_TRANS_ENGINE_H