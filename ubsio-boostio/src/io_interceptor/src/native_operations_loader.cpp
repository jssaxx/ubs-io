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

#include "native_operations_loader.h"

#include <dlfcn.h>
#include <cstdlib>

#include "ceptor_log.h"

using namespace ock::interceptor;

NativeOperations NativeOperationsLoader::operations = {nullptr};

bool NativeOperationsLoader::Initialize()
{
    static bool initialized = false;
    if (initialized) {
        INTERCEPTORLOG_DEBUG("Proxy has been loaded");
        return true;
    }
    LoadControlProxy();
    LoadFileIOProxy();
    LoadMetaProxy();
    LoadPathProxy();
    LoadFileStreamProxy();
    initialized = true;
    return true;
}

NativeOperations& NativeOperationsLoader::GetProxy()
{
    return operations;
}

template<typename T>
void NativeOperationsLoader::LoadProxy(const std::string& syscall, T& handle)
{
    handle = reinterpret_cast<T>(dlsym(RTLD_NEXT, syscall.c_str()));
    if (handle == nullptr) {
        INTERCEPTORLOG_WARN("Failed to load glibc symbol %s", syscall.c_str());
    }
}

void NativeOperationsLoader::LoadControlProxy()
{
    LoadProxy("rename", operations.rename);
    LoadProxy("access", operations.access);
    LoadProxy("open", operations.open);
    LoadProxy("open64", operations.open64);
    LoadProxy("openat", operations.openat);
    LoadProxy("openat64", operations.openat64);
    LoadProxy("creat", operations.creat);
    LoadProxy("creat64", operations.creat64);
    LoadProxy("close", operations.close);
    LoadProxy("lseek", operations.lseek);
    LoadProxy("lseek64", operations.lseek64);
    LoadProxy("truncate", operations.truncate);
    LoadProxy("truncate64", operations.truncate64);
    LoadProxy("ftruncate", operations.ftruncate);
    LoadProxy("ftruncate64", operations.ftruncate64);
    LoadProxy("dup", operations.dup);
    LoadProxy("dup2", operations.dup2);
    LoadProxy("dup3", operations.dup3);
    LoadProxy("remove", operations.remove);
    LoadProxy("fork", operations.fork);
}

void NativeOperationsLoader::LoadFileIOProxy()
{
    LoadProxy("read", operations.read);
    LoadProxy("write", operations.write);
    LoadProxy("pread", operations.pread);
    LoadProxy("pwrite", operations.pwrite);
    LoadProxy("pread64", operations.pread64);
    LoadProxy("pwrite64", operations.pwrite64);
    LoadProxy("readv", operations.readv);
    LoadProxy("writev", operations.writev);
    LoadProxy("preadv", operations.preadv);
    LoadProxy("preadv64", operations.preadv64);
    LoadProxy("pwritev", operations.pwritev);
    LoadProxy("pwritev64", operations.pwritev64);
    LoadProxy("fsync", operations.fsync);
    LoadProxy("sync", operations.sync);
    LoadProxy("syncfs", operations.syncfs);
}

void NativeOperationsLoader::LoadMetaProxy()
{
    LoadProxy("__xstat", operations.__xstat);
    LoadProxy("__xstat64", operations.__xstat64);
    LoadProxy("__lxstat", operations.__lxstat);
    LoadProxy("__lxstat64", operations.__lxstat64);
    LoadProxy("__fxstat", operations.__fxstat);
    LoadProxy("__fxstat64", operations.__fxstat64);
    LoadProxy("__fxstatat", operations.__fxstatat);
    LoadProxy("__fxstatat64", operations.__fxstatat64);
    LoadProxy("utimes", operations.utimes);
}

void NativeOperationsLoader::LoadPathProxy()
{
    LoadProxy("unlink", operations.unlink);
    LoadProxy("unlinkat", operations.unlinkat);
}

void NativeOperationsLoader::LoadFileStreamProxy()
{
    LoadProxy("fopen", operations.fopen);
    LoadProxy("fopen64", operations.fopen64);
    LoadProxy("fclose", operations.fclose);
    LoadProxy("fseek", operations.fseek);
    LoadProxy("fwrite", operations.fwrite);
    LoadProxy("fread", operations.fread);
    LoadProxy("fgetc", operations.fgetc);
    LoadProxy("fgets", operations.fgets);
    LoadProxy("fflush", operations.fflush);
    LoadProxy("ftell", operations.ftell);
    LoadProxy("rewind", operations.rewind);
}

bool ock::interceptor::InitNativeHook()
{
    return ock::interceptor::NativeOperationsLoader::\
        GetInstance().Initialize();
}
