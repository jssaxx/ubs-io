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

#ifndef BOOSTIO_DL_CEPHSYSTEM_H
#define BOOSTIO_DL_CEPHSYSTEM_H

#include <dlfcn.h>
#include <string>
#include <unordered_map>
#include "file_system.h"

/* Opaque Ceph types ? resolved at runtime via dlopen */
typedef void *rados_t;
typedef void *rados_ioctx_t;
typedef void *rados_list_ctx_t;

namespace ock {
namespace bio {

/*
 * DlCephSystem ? dlopen-based Ceph RADOS backend.
 *
 * Loads librados.so.2 at runtime via dlopen/dlsym ? no build-time dependency on librados-devel.
 * If the library is absent, Init() returns BIO_UFS_IOERR gracefully
 * without crashing the process.
 */
class DlCephSystem : public FileSystem {
public:
    /* rados function pointer types */
    using RadosCreate2Fn = int (*)(rados_t *, const char *, const char *, uint64_t);
    using RadosConfReadFileFn = int (*)(rados_t, const char *);
    using RadosShutdownFn = void (*)(rados_t);
    using RadosConnectFn = int (*)(rados_t);
    using RadosPoolLookupFn = int (*)(rados_t, const char *);
    using RadosPoolCreateFn = int (*)(rados_t, const char *);
    using RadosIoCtxCreateFn = int (*)(rados_t, const char *, rados_ioctx_t *);
    using RadosIoCtxDestroyFn = void (*)(rados_ioctx_t);
    using RadosWriteFn = int (*)(rados_ioctx_t, const char *, const char *, size_t, uint64_t);
    using RadosReadFn = int (*)(rados_ioctx_t, const char *, char *, size_t, uint64_t);
    using RadosRemoveFn = int (*)(rados_ioctx_t, const char *);
    using RadosStatFn = int (*)(rados_ioctx_t, const char *, uint64_t *, time_t *);
    using RadosNobjectsListOpenFn = int (*)(rados_ioctx_t, rados_list_ctx_t *);
    using RadosNobjectsListNextFn = int (*)(rados_list_ctx_t, const char **, const char **, const char **);
    using RadosNobjectsListCloseFn = void (*)(rados_list_ctx_t);

    ~DlCephSystem()
    {
        if (mLibHandle != nullptr) {
            dlclose(mLibHandle);
        }
    }

    BResult Init() override;
    void Stop() override;
    BResult Put(const char *key, const char *value, const size_t len) override;
    BResult Get(const char *key, char *value, const size_t len, const uint64_t off) override;
    BResult Delete(const char *key) override;
    BResult Stat(const char *key, ObjStat &objStat) override;
    BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) override;

#ifdef DEBUG_UT
    void SetRadosCreate2(RadosCreate2Fn fn) { mRadosCreate2 = fn; }
    void SetRadosConfReadFile(RadosConfReadFileFn fn) { mRadosConfReadFile = fn; }
    void SetRadosShutdown(RadosShutdownFn fn) { mRadosShutdown = fn; }
    void SetRadosConnect(RadosConnectFn fn) { mRadosConnect = fn; }
    void SetRadosPoolLookup(RadosPoolLookupFn fn) { mRadosPoolLookup = fn; }
    void SetRadosPoolCreate(RadosPoolCreateFn fn) { mRadosPoolCreate = fn; }
    void SetRadosIoCtxCreate(RadosIoCtxCreateFn fn) { mRadosIoCtxCreate = fn; }
    void SetRadosIoCtxDestroy(RadosIoCtxDestroyFn fn) { mRadosIoCtxDestroy = fn; }
    void SetRadosWrite(RadosWriteFn fn) { mRadosWrite = fn; }
    void SetRadosRead(RadosReadFn fn) { mRadosRead = fn; }
    void SetRadosRemove(RadosRemoveFn fn) { mRadosRemove = fn; }
    void SetRadosStat(RadosStatFn fn) { mRadosStat = fn; }
    void SetRadosNobjectsListOpen(RadosNobjectsListOpenFn fn) { mRadosNobjectsListOpen = fn; }
    void SetRadosNobjectsListNext(RadosNobjectsListNextFn fn) { mRadosNobjectsListNext = fn; }
    void SetRadosNobjectsListClose(RadosNobjectsListCloseFn fn) { mRadosNobjectsListClose = fn; }
#endif

private:
    void *LoadFunction(const char *name);
    BResult LoadCephLibrary();
    BResult InitOperations();
    void LoadCephConfig();

    RadosCreate2Fn mRadosCreate2 = nullptr;
    RadosConfReadFileFn mRadosConfReadFile = nullptr;
    RadosShutdownFn mRadosShutdown = nullptr;
    RadosConnectFn mRadosConnect = nullptr;
    RadosPoolLookupFn mRadosPoolLookup = nullptr;
    RadosPoolCreateFn mRadosPoolCreate = nullptr;
    RadosIoCtxCreateFn mRadosIoCtxCreate = nullptr;
    RadosIoCtxDestroyFn mRadosIoCtxDestroy = nullptr;
    RadosWriteFn mRadosWrite = nullptr;
    RadosReadFn mRadosRead = nullptr;
    RadosRemoveFn mRadosRemove = nullptr;
    RadosStatFn mRadosStat = nullptr;
    RadosNobjectsListOpenFn mRadosNobjectsListOpen = nullptr;
    RadosNobjectsListNextFn mRadosNobjectsListNext = nullptr;
    RadosNobjectsListCloseFn mRadosNobjectsListClose = nullptr;

    void *mLibHandle = nullptr;
    rados_t mConn = nullptr;
    rados_ioctx_t mIoCtx = nullptr;

    std::string mCfgPath;
    std::string mCluster;
    std::string mUser;
    std::string mPool;
};

}
}

#endif // BOOSTIO_DL_CEPHSYSTEM_H
