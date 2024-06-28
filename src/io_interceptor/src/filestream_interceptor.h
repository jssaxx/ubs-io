/*
*  Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
 */

#ifndef FILESTREAM_INTERCEPTOR_H
#define FILESTREAM_INTERCEPTOR_H

#include <cstdio>

namespace ock {
namespace interceptor {
    FILE* HookFopen(const char* file, const char* mode);

    FILE* HookFopen64(const char* file, const char* mode);

    int HookFclose(FILE* stream);

    size_t HookFread(void* ptr, size_t size, size_t count, FILE* stream);

    size_t HookFwrite(const void* ptr, size_t size, size_t nitems, FILE* stream);

    int HookFgetc(FILE* stream);

    char* HookFgets(char* s, int n, FILE* stream);

    int HookFputs(const char* s, FILE* stream);

    long int HookFtell(FILE* stream);

    int HookFflush(FILE* stream);

    void HookRewind(FILE* stream);

    int HookFseek(FILE* stream, long offset, int whence);
}
}
#endif // FILESTREAM_INTERCEPTOR_H