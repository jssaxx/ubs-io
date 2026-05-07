/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */

#ifndef mms_FUNCTIONS_H
#define mms_FUNCTIONS_H

#include <sys/sysinfo.h>
#include "securec.h"
#include "mms_log.h"
#include "mms_def.h"
#include "mms_types.h"
#include "mms_str_util.h"

namespace ock {
namespace mms {
inline void CopyKey(char *dstKey, const char *srcKey, uint32_t maxLen)
{
    auto keyLen = strlen(srcKey);
    auto ret = memcpy_s(dstKey, maxLen, srcKey, keyLen);
    dstKey[keyLen] = '\0';
    if (UNLIKELY(ret != 0)) {
        LOG_ERROR("Copy Key failed, ret:" << ret << ".");
    }
}

inline bool ValidateRatios(std::string name, const std::string &value, std::string &errMsg)
{
    if (value.empty()) {
        errMsg = "Invalid value for <" + name + ">, it should not be empty";
        return false;
    }

    std::vector<std::string> ratios;
    StrUtil::Split(value, ":", ratios);
    if (ratios.size() != NO_2) {
        errMsg = "Invalid value for <" + name + ">, it should like 4:6";
        return false;
    }

    long ratio0 = NO_U64_0;
    long ratio1 = NO_U64_0;
    if (!StrUtil::StrToLong(ratios[NO_U64_0], ratio0) || !StrUtil::StrToLong(ratios[NO_1], ratio1)) {
        errMsg = "Invalid value for <" + name + ">, it should like 4:6";
        return false;
    }

    if (ratio0 < NO_U64_0 || ratio0 > NO_10 || ratio1 < NO_U64_0 || ratio1 > NO_10) {
        errMsg = "Invalid value for <" + name + ">, ratio should be in range 0 to 10";
        return false;
    }

    if (ratio0 + ratio1 != NO_10) {
        errMsg = "Invalid value for <" + name + ">, sum of ratios must equal 10";
        return false;
    }
    return true;
}

inline uint64_t GetSysFreeMemCap()
{
    struct sysinfo info;
    if (sysinfo(&info) != NO_U64_0) {
        return 0;
    }
    return info.freeram;
}

}
}
#endif
