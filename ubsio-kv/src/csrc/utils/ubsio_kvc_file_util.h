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

#ifndef UBSIO_KVC_FILE_UTIL_H
#define UBSIO_KVC_FILE_UTIL_H

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace ock {
namespace ubsio {

class FileUtil {
public:
    /**
     * @brief Check if file or dir exists
     */
    static bool Exist(const std::string &path);

    /**
     * @brief Get the realpath for security consideration
     */
    static bool CanonicalPath(std::string &path);
};

inline bool FileUtil::Exist(const std::string &path)
{
    return access(reinterpret_cast<const char *>(path.c_str()), 0) != -1;
}

inline bool FileUtil::CanonicalPath(std::string &path)
{
    if (path.empty() || path.size() > 4096L) {
        return false;
    }

    /* It will allocate memory to store path */
    char *realPath = realpath(path.c_str(), nullptr);
    if (realPath == nullptr) {
        return false;
    }

    path = realPath;
    free(realPath);
    return true;
}

} // namespace ubsio
} // namespace ock
#endif // UBSIO_KVC_FILE_UTIL_H