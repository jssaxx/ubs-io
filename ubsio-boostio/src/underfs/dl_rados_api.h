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

#ifndef BOOSTIO_DL_RADOS_API_H
#define BOOSTIO_DL_RADOS_API_H

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

namespace ock {
namespace bio {
using rados_t = void*;
using rados_ioctx_t = void*;
using rados_list_ctx_t = void*;

using MethodRadosCreate2 = int (*)(rados_t*, const char*, const char*, uint64_t);
using MethodRadosConfReadFile = int (*)(rados_t, const char*);
using MethodRadosConnect = int (*)(rados_t);
using MethodRadosShutdown = void (*)(rados_t);
using MethodRadosPoolLookup = int64_t (*)(rados_t, const char*);
using MethodRadosPoolCreate = int (*)(rados_t, const char*);
using MethodRadosIoctxCreate = int (*)(rados_t, const char*, rados_ioctx_t*);
using MethodRadosIoctxDestroy = void (*)(rados_ioctx_t);
using MethodRadosWrite = int (*)(rados_ioctx_t, const char*, const char*, size_t, uint64_t);
using MethodRadosRead = int (*)(rados_ioctx_t, const char*, char*, size_t, uint64_t);
using MethodRadosRemove = int (*)(rados_ioctx_t, const char*);
using MethodRadosStat = int (*)(rados_ioctx_t, const char*, uint64_t*, time_t*);
using MethodRadosNobjectsListOpen = int (*)(rados_ioctx_t, rados_list_ctx_t*);
using MethodRadosNobjectsListNext = int (*)(rados_list_ctx_t, const char**, const char**, const char**);
using MethodRadosNobjectsListClose = void (*)(rados_list_ctx_t);

class DlRadosApi {
public:
    static MethodRadosCreate2 radosCreate2;
    static MethodRadosConfReadFile radosConfReadFile;
    static MethodRadosConnect radosConnect;
    static MethodRadosShutdown radosShutdown;
    static MethodRadosPoolLookup radosPoolLookup;
    static MethodRadosPoolCreate radosPoolCreate;
    static MethodRadosIoctxCreate radosIoctxCreate;
    static MethodRadosIoctxDestroy radosIoctxDestroy;
    static MethodRadosWrite radosWrite;
    static MethodRadosRead radosRead;
    static MethodRadosRemove radosRemove;
    static MethodRadosStat radosStat;
    static MethodRadosNobjectsListOpen radosNobjectsListOpen;
    static MethodRadosNobjectsListNext radosNobjectsListNext;
    static MethodRadosNobjectsListClose radosNobjectsListClose;

    static int LoadRadosApiDl(const std::string &libPath = "");

private:
    static const char* gRadosLibNames[];
    static constexpr size_t gRadosLibNameCount = 2;
    static bool gStarted;

    static bool IsRadosMethodLoaded();
    static int GetLibPath(std::string libDir, std::string &radosPath);
    static int LoadRadosByPath(const std::string &radosPath);
    static int LoadRadosMethod(void *handle);
};
}
}

#endif // BOOSTIO_DL_RADOS_API_H
