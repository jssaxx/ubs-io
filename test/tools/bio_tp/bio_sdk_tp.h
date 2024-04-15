/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#ifndef BIO_SDK_TP_H
#define BIO_SDK_TP_H

#include "bio_tp_common.h"
namespace ock {
namespace bio {
namespace tp {
class SdkTp {
public:
    static void Register() noexcept;
    static void Deregister() noexcept;
};
}
}
}
#endif // BIO_SDK_TP_H
