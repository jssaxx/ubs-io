/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBSIO_NDS_DL_API_H
#define UBSIO_NDS_DL_API_H

#include <dlfcn.h>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include "ubsio_kvc_err.h"

namespace ock {
namespace ubsio {

static constexpr int IO_URING_MAX_DEPTH = 4096;

struct NdsFileid {
    int fd;
    int deviceID;
};
using nds_fileid_t = NdsFileid;

using NdsInitFunc = int (*)(int);
using NdsInitAsyncFunc = int (*)(int);
using NdsUninitFunc = int (*)(void);
using NdsOpenFunc = int (*)(const char*, int, ...);
using NdsRegmemFunc = int (*)(nds_fileid_t, const void*, size_t);
using NdsUnregmemFunc = int (*)(nds_fileid_t, const void*, size_t);
using NdsReadFunc = ssize_t (*)(nds_fileid_t, void*, off_t, size_t, off_t);
using NdsReadvBatchFunc = ssize_t (*)(nds_fileid_t, const struct iovec*, size_t, off_t, size_t);

class NdsApi {
public:
    NdsApi() = delete;
    static int32_t LoadLibrary();
    static void CleanupLibrary();

    static inline int NdsInit(int deviceID)
    {
        if (pNdsInit == nullptr) {
            LOG_ERROR("nds_init not available");
            return UBSIO_KVC_ERR;
        }
        return pNdsInit(deviceID);
    }

    static inline int NdsInitAsync(int deviceID)
    {
        if (pNdsInitAsync == nullptr) {
            LOG_ERROR("nds_init_async not available");
            return UBSIO_KVC_ERR;
        }
        return pNdsInitAsync(deviceID);
    }

    static inline int NdsUninit()
    {
        if (pNdsUninit == nullptr) {
            LOG_ERROR("nds_uninit not available");
            return UBSIO_KVC_ERR;
        }
        return pNdsUninit();
    }

    static inline int NdsOpen(const char* path, int oflag, ...)
    {
        if (pNdsOpen == nullptr) {
            LOG_ERROR("nds_open not available");
            return UBSIO_KVC_ERR;
        }
        return pNdsOpen(path, oflag);
    }

    static inline int NdsRegmem(nds_fileid_t fid, const void* addr, size_t len)
    {
        if (pNdsRegmem == nullptr) {
            LOG_ERROR("nds_regmem not available");
            return UBSIO_KVC_ERR;
        }
        return pNdsRegmem(fid, addr, len);
    }

    static inline int NdsUnregmem(nds_fileid_t fid, const void* addr, size_t len)
    {
        if (pNdsUnregmem == nullptr) {
            LOG_ERROR("nds_unregmem not available");
            return UBSIO_KVC_ERR;
        }
        return pNdsUnregmem(fid, addr, len);
    }

    static inline ssize_t NdsRead(nds_fileid_t fid, void* buf, off_t buf_offset, size_t nbyte, off_t f_offset)
    {
        if (pNdsRead == nullptr) {
            LOG_ERROR("nds_read not available");
            return -1;
        }
        return pNdsRead(fid, buf, buf_offset, nbyte, f_offset);
    }

    static inline ssize_t NdsReadvBatch(nds_fileid_t fid, const struct iovec* iovs, size_t iov_cnt, off_t f_offset, size_t ring_id = 0)
    {
        if (pNdsReadvBatch == nullptr) {
            LOG_ERROR("nds_readv_batch not available");
            return -1;
        }
        return pNdsReadvBatch(fid, iovs, iov_cnt, f_offset, ring_id);
    }

private:
    static std::mutex gMutex;
    static bool gLoaded;
    static void *ndsHandle;
    static NdsInitFunc pNdsInit;
    static NdsInitAsyncFunc pNdsInitAsync;
    static NdsUninitFunc pNdsUninit;
    static NdsOpenFunc pNdsOpen;
    static NdsRegmemFunc pNdsRegmem;
    static NdsUnregmemFunc pNdsUnregmem;
    static NdsReadFunc pNdsRead;
    static NdsReadvBatchFunc pNdsReadvBatch;
};

}  // namespace ubsio
}  // namespace ock

#endif // UBSIO_NDS_DL_API_H
