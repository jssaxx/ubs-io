/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_CACHE_TP_H
#define BOOSTIO_BIO_CACHE_TP_H


#include "bio_tp_common.h"
namespace ock {
namespace bio {
namespace tp {
class CacheTp {
public:
    static void Register() noexcept;
    static void Deregister() noexcept;
};
}
}
}


#endif // BOOSTIO_BIO_CACHE_TP_H
