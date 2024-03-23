/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */

#ifndef BIO_FUNCTIONS_H
#define BIO_FUNCTIONS_H

#include "securec.h"
#include "bio_log.h"
#include "bio_def.h"
#include "bio_types.h"
#include "bio_str_util.h"

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

inline bool validateRatios(const std::string &value, std::string &errMsg)
{
    if (value.empty()) {
        errMsg = "ratio should not be empty";
        return false;
    }

    std::vector<std::string> ratios;
    StrUtil::Split(value, ":", ratios);
    if (ratios.size() != NO_2) {
        errMsg = "ratio should like 4:6";
        return false;
    }

    long ratio0 = NO_U64_0;
    long ratio1 = NO_U64_0;
    if (!StrUtil::StrToLong(ratios[NO_U64_0], ratio0) || !StrUtil::StrToLong(ratios[NO_1], ratio1)) {
        errMsg = "ratio should like 4:6";
        return false;
    }

    if (ratio0 < NO_U64_0 || ratio0 > NO_10 || ratio1 < NO_U64_0 || ratio1 > NO_10) {
        errMsg = "ratios should be in range 0 to 10";
        return false;
    }

    if (ratio0 + ratio1 != NO_10) {
        errMsg = "sum of ratios must equal 10";
        return false;
    }
    return true;
}

}
}
#endif