/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#ifndef BIO_TRACEPOINT_H
#define BIO_TRACEPOINT_H
#ifdef __aarch64__
#include "tracepoint.h"
#endif

namespace ock {
namespace bio {
namespace tp {
class TracePointManager {
public:
    static int Initialize() noexcept;
    static void Destroy() noexcept;

private:
    static int RegisterAllPoints() noexcept;
    static void RemoveAllPoints() noexcept;
};
}
}
}

#endif // BIO_TRACEPOINT_H
