/*
*  Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
 */

#include "filestream_interceptor.h"
#include "interceptor_symbol_visibility.h"
#include "ceptor_log.h"

using namespace ock::interceptor;

extern "C" {
INTERCEPTOR_API FILE* fopen(const char* filename, const char* mode)
{
    INTERCEPTORLOG_DEBUG("Hooking fopen path(%s) mode(%s).", filename, mode);
    return HookFopen(filename, mode);
}

INTERCEPTOR_API FILE* fopen64(const char* file, const char* mode)
{
    INTERCEPTORLOG_DEBUG("Hooking fopen64 path(%s) mode(%s).", file, mode);
    return HookFopen64(file, mode);
}

INTERCEPTOR_API int fclose(FILE* fp)
{
    INTERCEPTORLOG_INFO("Hooking fclose.");
    return HookFclose(fp);
}

INTERCEPTOR_API size_t fread(void* buf, size_t size, size_t count, FILE* fp)
{
    INTERCEPTORLOG_DEBUG("Hooking fread size(%u), count(%u).", size, count);
    return HookFread(buf, size, count, fp);
}

INTERCEPTOR_API size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* fp)
{
    INTERCEPTORLOG_INFO("Hooking fwrite size(%u), count(%u).", size, nmemb);
    return HookFwrite(ptr, size, nmemb, fp);
}

INTERCEPTOR_API int fgetc(FILE* fp)
{
    INTERCEPTORLOG_DEBUG("Hooking fgetc.");
    return HookFgetc(fp);
}

INTERCEPTOR_API char* fgets(char* str, int n, FILE* fp)
{
    INTERCEPTORLOG_DEBUG("Hooking fgets.");
    return HookFgets(str, n, fp);
}

INTERCEPTOR_API long int ftell(FILE* fp)
{
    long int ret = HookFtell(fp);
    INTERCEPTORLOG_DEBUG("Hooking ftell.");
    return ret;
}

INTERCEPTOR_API int fflush(FILE* fp)
{
    INTERCEPTORLOG_DEBUG("Hooking fflush.");
    return HookFflush(fp);
}

INTERCEPTOR_API void rewind(FILE* fp)
{
    INTERCEPTORLOG_DEBUG("Hooking rewind.");
    HookRewind(fp);
}

INTERCEPTOR_API int fseek(FILE* fp, long offset, int whence)
{
    INTERCEPTORLOG_DEBUG("Hooking fseek offset(%ld), whence(%d)", offset, whence);
    return HookFseek(fp, offset, whence);
}
};