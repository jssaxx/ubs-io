/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_DEF_H
#define BOOSTIO_BIO_DEF_H

namespace ock {
namespace bio {
#ifndef LIKELY
#define LIKELY(x) (__builtin_expect(!!(x), 1) != 0)
#endif

#ifndef UNLIKELY
#define UNLIKELY(x) (__builtin_expect(!!(x), 0) != 0)
#endif

#define KB_UNIT (1024)
}
}

#endif // BOOSTIO_BIO_DEF_H
