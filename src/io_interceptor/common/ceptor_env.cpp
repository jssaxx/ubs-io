/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
 */

#include "ceptor_env.h"

#include <climits>
#include <cstdlib>
#include <unistd.h>

namespace ock {
namespace interceptor {
namespace env {
std::string GetEnv(const std::string& key, const std::string& defaultVal)
{
    const char* val = getenv(key.c_str());
    if (val == nullptr) {
        return defaultVal;
    }
    return val;
}

std::string GetCWD()
{
    char cwd[PATH_MAX];
    if (getcwd(cwd, PATH_MAX) == nullptr) {
        return "";
    }
    return cwd;
}

}
}
}