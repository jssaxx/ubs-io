/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */

#ifndef BIO_FUNCTIONS_H
#define BIO_FUNCTIONS_H

#include <cmath>
#include <cstring>
#include "securec.h"
#include "bio_log.h"
#include "bio_def.h"

namespace ock {
namespace bio {
inline void CopyKey(char *dstKey, const char *srcKey, uint32_t maxLen)
{
    auto keyLen = strlen(srcKey);
    auto ret = memcpy_s(dstKey, maxLen, srcKey, keyLen);
    dstKey[keyLen] = '\0';
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Copy Key failed, key:" << srcKey << ", len:" << sizeof(srcKey) << ".");
    }
}

}
}
#endif