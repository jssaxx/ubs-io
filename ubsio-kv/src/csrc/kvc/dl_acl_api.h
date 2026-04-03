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

#ifndef DL_ACL_API_H
#define DL_ACL_API_H

#include <dlfcn.h>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>
#include "ubsio_kvc_err.h"

namespace ock {
namespace ubsio {

enum class AclrtMemLocationType {
    ACL_MEM_LOCATION_TYPE_HOST = 0,   // Host内存
    ACL_MEM_LOCATION_TYPE_DEVICE,     // Device内存
};

using AclrtMemLocation = struct AclrtMemLocation;
struct AclrtMemLocation {
    uint32_t id;
    AclrtMemLocationType type;    // 内存所在位置
};
using AclrtMemcpyBatchAttr = struct AclrtMemcpyBatchAttr;
struct AclrtMemcpyBatchAttr {
    AclrtMemLocation dstLoc;
    AclrtMemLocation srcLoc;
    uint8_t rsv[16];
};

constexpr uint32_t ACL_MEMCPY_HOST_TO_HOST = 0;
constexpr uint32_t ACL_MEMCPY_HOST_TO_DEVICE = 1;
constexpr uint32_t ACL_MEMCPY_DEVICE_TO_HOST = 2;
constexpr uint32_t ACL_MEMCPY_DEVICE_TO_DEVICE = 3;

using AclrtSetDeviceFunc = int32_t (*)(int32_t);
using AclrtGetDeviceFunc = int32_t (*)(int32_t *);
using AclrtCreateStreamFunc = int (*)(void **);
using AclrtCreateStreamWithConfigFunc = int (*)(void **, int32_t, uint32_t);
using AclrtDestroyStreamFunc = int (*)(void *);
using AclrtSynchronizeStreamFunc = int (*)(void *);
using AclrtMallocFunc = int32_t (*)(void **, size_t, uint32_t);
using AclrtFreeFunc = int (*)(void *);
using AclrtMallocHostFunc = int32_t (*)(void **, size_t);
using AclrtFreeHostFunc = int (*)(void *);
using AclrtMemcpyFunc = int32_t (*)(void *, size_t, const void *, size_t, uint32_t);
using AclrtMemcpyAsyncFunc = int32_t (*)(void *, size_t, const void *, size_t, uint32_t, void *);
using AclrtMemcpy2dFunc = int32_t (*)(void *, size_t, const void *, size_t, size_t, size_t, uint32_t);
using AclrtMemcpy2dAsyncFunc = int32_t (*)(void *, size_t, const void *, size_t, size_t, size_t, uint32_t, void *);
using AclrtMemsetFunc = int32_t (*)(void *, size_t, int32_t, size_t);
using RtDeviceGetBareTgidFunc = int32_t (*)(uint32_t *);
using RtGetDeviceInfoFunc = int32_t (*)(uint32_t, int32_t, int32_t, int64_t *val);
using AclrtMemcpyBatchFunc = int32_t (*)(void **, size_t *, void **, size_t *, size_t,
                                         AclrtMemcpyBatchAttr *, size_t *, size_t, size_t *);

class ACLApi {
public:
    ACLApi() = delete;
    static int32_t LoadLibrary();
    static void CleanupLibrary();

    static inline int32_t AclrtSetDevice(int32_t deviceId, bool force = false)
    {
        if (pAclrtSetDevice == nullptr) {
            return DFC_ERR;
        }
        if (force) {
            return pAclrtSetDevice(deviceId);
        }
        int32_t nowDeviceId = -1;
        if (AclrtGetDevice(&nowDeviceId) == 0 && nowDeviceId == deviceId) {
            return DFC_OK;
        } else {
            return pAclrtSetDevice(deviceId);
        }
    }

    static inline int32_t AclrtGetDevice(int32_t *deviceId)
    {
        if (pAclrtGetDevice == nullptr) {
            return DFC_ERR;
        }
        return pAclrtGetDevice(deviceId);
    }

    static inline int32_t AclrtCreateStream(void **stream)
    {
        if (pAclrtCreateStream == nullptr) {
            return DFC_ERR;
        }
        return pAclrtCreateStream(stream);
    }

    static inline int32_t AclrtCreateStreamWithConfig(void **stream, uint32_t prot, uint32_t config)
    {
        if (pAclrtCreateStreamWithConfig == nullptr) {
            return DFC_ERR;
        }
        return pAclrtCreateStreamWithConfig(stream, prot, config);
    }

    static inline int32_t AclrtDestroyStream(void *stream)
    {
        if (pAclrtDestroyStream == nullptr) {
            return DFC_ERR;
        }
        return pAclrtDestroyStream(stream);
    }

    static inline int32_t AclrtSynchronizeStream(void *stream)
    {
        if (pAclrtSynchronizeStream == nullptr) {
            return DFC_ERR;
        }
        return pAclrtSynchronizeStream(stream);
    }

    static inline int32_t AclrtMalloc(void **ptr, size_t count, uint32_t type)
    {
        if (pAclrtMalloc == nullptr) {
            return DFC_ERR;
        }
        return pAclrtMalloc(ptr, count, type);
    }

    static inline int32_t AclrtFree(void *ptr)
    {
        if (pAclrtFree == nullptr) {
            return DFC_ERR;
        }
        return pAclrtFree(ptr);
    }

    static inline int32_t AclrtMallocHost(void **ptr, size_t count)
    {
        if (pAclrtMallocHost == nullptr) {
            return DFC_ERR;
        }
        return pAclrtMallocHost(ptr, count);
    }

    static inline int32_t AclrtFreeHost(void *ptr)
    {
        if (pAclrtFreeHost == nullptr) {
            return DFC_ERR;
        }
        return pAclrtFreeHost(ptr);
    }

    static inline int32_t AclrtMemcpy(void *dst, size_t destMax, const void *src, size_t count, uint32_t kind)
    {
        if (pAclrtMemcpy == nullptr) {
            return DFC_ERR;
        }
        return pAclrtMemcpy(dst, destMax, src, count, kind);
    }

    static inline int32_t AclrtMemcpyAsync(void *dst, size_t destMax, const void *src, size_t count, uint32_t kind,
                                          void *stream)
    {
        if (pAclrtMemcpyAsync == nullptr) {
            return DFC_ERR;
        }
        return pAclrtMemcpyAsync(dst, destMax, src, count, kind, stream);
    }

    static inline int32_t AclrtMemcpyBatch(void **dsts, size_t *destMax,
                                          void **srcs, size_t *sizes, size_t numBatches,
                                          AclrtMemcpyBatchAttr *attrs, size_t *attrsIndexes,
                                          size_t numAttrs, size_t *failIndex)
    {
        if (pAclrtMemcpyBatch == nullptr) {
            return DFC_ERR;
        }
        return pAclrtMemcpyBatch(dsts, destMax, srcs, sizes, numBatches, attrs, attrsIndexes, numAttrs, failIndex);
    }

    static inline int32_t AclrtMemcpy2d(void *dst, size_t dpitch, const void *src, size_t spitch,
                                       size_t width, size_t height, uint32_t kind)
    {
        if (pAclrtMemcpy2d == nullptr) {
            return DFC_ERR;
        }
        return pAclrtMemcpy2d(dst, dpitch, src, spitch, width, height, kind);
    }

    static inline int32_t AclrtMemcpy2dAsync(void *dst, size_t dpitch, const void *src, size_t spitch,
                                            size_t width, size_t height, uint32_t kind, void *stream)
    {
        if (pAclrtMemcpy2dAsync == nullptr) {
            return DFC_ERR;
        }
        return pAclrtMemcpy2dAsync(dst, dpitch, src, spitch, width, height, kind, stream);
    }

    static inline int32_t AclrtMemset(void *dst, size_t destMax, int32_t value, size_t count)
    {
        if (pAclrtMemset == nullptr) {
            return DFC_ERR;
        }
        return pAclrtMemset(dst, destMax, value, count);
    }

    static inline int32_t RtGetDeviceInfo(uint32_t deviceId, int32_t moduleType, int32_t infoType, int64_t *val)
    {
        if (pRtGetDeviceInfo == nullptr) {
            return DFC_ERR;
        }
        return pRtGetDeviceInfo(deviceId, moduleType, infoType, val);
    }

private:
    static std::mutex gMutex;
    static bool gLoaded;
    static void *aclHandle;
    static AclrtGetDeviceFunc pAclrtGetDevice;
    static AclrtSetDeviceFunc pAclrtSetDevice;
    static AclrtCreateStreamFunc pAclrtCreateStream;
    static AclrtCreateStreamWithConfigFunc pAclrtCreateStreamWithConfig;
    static AclrtDestroyStreamFunc pAclrtDestroyStream;
    static AclrtSynchronizeStreamFunc pAclrtSynchronizeStream;
    static AclrtMallocFunc pAclrtMalloc;
    static AclrtFreeFunc pAclrtFree;
    static AclrtMallocHostFunc pAclrtMallocHost;
    static AclrtFreeHostFunc pAclrtFreeHost;
    static AclrtMemcpyFunc pAclrtMemcpy;
    static AclrtMemcpyBatchFunc pAclrtMemcpyBatch;
    static AclrtMemcpyAsyncFunc pAclrtMemcpyAsync;
    static AclrtMemcpy2dFunc pAclrtMemcpy2d;
    static AclrtMemcpy2dAsyncFunc pAclrtMemcpy2dAsync;
    static AclrtMemsetFunc pAclrtMemset;
    static RtGetDeviceInfoFunc pRtGetDeviceInfo;
};

}  // namespace ubsio
}  // namespace ock
#endif  // DL_ACL_API_H