/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
 */

#include "filestream_interceptor.h"

#include "native_operations_loader.h"
#include "proxy_operations_loader.h"

namespace ock {
namespace interceptor {
static inline bool CheckPointer(const void* ptr)
{
    if (ptr == nullptr) {
        errno = EFAULT;
        return false;
    }
    return true;
}

static inline bool CheckPath(const char* path)
{
    if (path == nullptr) {
        errno = EFAULT;
        return false;
    }
    if (path[0] == '\0') {
        errno = ENOENT;
        return false;
    }

    return true;
}

FILE* HookFopen(const char* file, const char* mode)
{
    if (!CheckPath(file) ||
        !CheckPointer(mode) ||
        !InitNativeHook() ||
        CHECKNATIVEFUNC(fopen)) {
        return nullptr;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fopen)) {
        return NATIVE(fopen)(file, mode);
    }
    return PROXY(fopen)(file, mode);
}

FILE* HookFopen64(const char* file, const char* mode)
{
    if (!CheckPath(file) ||
        !CheckPointer(mode) ||
        !InitNativeHook() ||
        CHECKNATIVEFUNC(fopen64)) {
        return nullptr;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fopen64)) {
        return NATIVE(fopen64)(file, mode);
    }
    return PROXY(fopen64)(file, mode);
}

int HookFclose(FILE* stream)
{
    if (!CheckPointer(stream) || !InitNativeHook() || CHECKNATIVEFUNC(fclose)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fclose)) {
        return NATIVE(fclose)(stream);
    }
    return PROXY(fclose)(stream);
}

size_t HookFread(void* ptr, size_t size, size_t count, FILE* stream)
{
    if (!CheckPointer(ptr) || !CheckPointer(stream) ||
        !InitNativeHook() || CHECKNATIVEFUNC(fread)) {
        return 0;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fread)) {
        return NATIVE(fread)(ptr, size, count, stream);
    }
    return PROXY(fread)(ptr, size, count, stream);
}

size_t HookFwrite(const void* ptr, size_t size, size_t nitems, FILE* stream)
{
    if (!CheckPointer(ptr) || !CheckPointer(stream) ||
        !InitNativeHook() || CHECKNATIVEFUNC(fwrite)) {
        return 0;
    }

    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fwrite)) {
        return NATIVE(fwrite)(ptr, size, nitems, stream);
    }
    return PROXY(fwrite)(ptr, size, nitems, stream);
}

int HookFgetc(FILE* stream)
{
    if (!CheckPointer(stream) || !InitNativeHook() || CHECKNATIVEFUNC(fgetc)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fgetc)) {
        return NATIVE(fgetc)(stream);
    }
    return PROXY(fgetc)(stream);
}

char* HookFgets(char* s, int n, FILE* stream)
{
    if (!CheckPointer(s) || !CheckPointer(stream) ||
        !InitNativeHook() || CHECKNATIVEFUNC(fgets)) {
        return nullptr;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fgets)) {
        return NATIVE(fgets)(s, n, stream);
    }
    return PROXY(fgets)(s, n, stream);
}

long int HookFtell(FILE* stream)
{
    if (!CheckPointer(stream) || !InitNativeHook() || CHECKNATIVEFUNC(ftell)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(ftell)) {
        return NATIVE(ftell)(stream);
    }
    return PROXY(ftell)(stream);
}

int HookFflush(FILE* stream)
{
    if (!CheckPointer(stream) || !InitNativeHook() || CHECKNATIVEFUNC(fflush)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fflush)) {
        return NATIVE(fflush)(stream);
    }
    return PROXY(fflush)(stream);
}

void HookRewind(FILE* stream)
{
    if (!CheckPointer(stream) || !InitNativeHook() || CHECKNATIVEFUNC(rewind)) {
        return;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(rewind)) {
        NATIVE(rewind)(stream);
        return;
    }
    PROXY(rewind)(stream);
}

int HookFseek(FILE* stream, long offset, int whence)
{
    if (!CheckPointer(stream) || !InitNativeHook() || CHECKNATIVEFUNC(fseek)) {
        return -1;
    }
    if (CHECKPROXYLOADED || CHECKPROXYFUNC(fseek)) {
        return NATIVE(fseek)(stream, offset, whence);
    }
    return PROXY(fseek)(stream, offset, whence);
}
}
}