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

#include "interceptor_path.h"

#include <cstring>
#include <sstream>
#include <list>
#include <climits>
#include <unistd.h>

namespace ock {
namespace bio {
static const char DELIMITER = '/';

std::string AddPrefix(const std::string &prefix, const char *rawPath)
{
    if (rawPath == nullptr) {
        return prefix;
    }
    size_t pathLen = strlen(rawPath);
    std::string path;
    path.reserve(prefix.size() + pathLen + 1);
    path.append(prefix);
    path.push_back(DELIMITER);
    path.append(rawPath);
    return path;
}

std::string GetPathNoPrefix(const std::string &path, const std::string &prefix)
{
    if (path.compare(0, prefix.size(), prefix) == 0) {
        if (path.length() == prefix.length()) {
            return "/";
        }
        return path.substr(prefix.length());
    }
    return path;
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
