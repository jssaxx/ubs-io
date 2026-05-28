/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "ceptor_env.h"

#include <unistd.h>
#include <climits>
#include <cstdlib>

namespace ock {
namespace interceptor {
namespace env {
std::string GetEnv(const std::string &key, const std::string &defaultVal)
{
    const char *val = getenv(key.c_str());
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

} // namespace env
} // namespace interceptor
} // namespace ock