/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
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
static const std::string PARENT = "..";
static const std::string SELF = ".";

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
